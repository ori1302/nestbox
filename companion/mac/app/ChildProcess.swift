import Foundation

/// A process the app runs: the companion helper or the pet window. Its stdout and stderr arrive line by line
/// on the main actor.
@MainActor
final class ChildProcess {
    private static let stopTimeout: TimeInterval = 1.5

    let executable: URL
    let arguments: [String]
    let workingDirectory: URL?
    var environment: [String: String]?
    var onLine: ((String) -> Void)?
    /// Called once it exits: its status, whether stop() asked for it, and how long it ran.
    var onExit: ((_ status: Int32, _ requested: Bool, _ ran: TimeInterval) -> Void)?

    private var process: Process?
    private var startedAt = Date()
    private var stopping = false

    init(executable: URL, arguments: [String], workingDirectory: URL? = nil) {
        self.executable = executable
        self.arguments = arguments
        self.workingDirectory = workingDirectory
    }

    var pid: pid_t? { process?.processIdentifier }
    var isRunning: Bool { process?.isRunning ?? false }

    func start() throws {
        let p = Process()
        p.executableURL = executable
        p.arguments = arguments
        if let workingDirectory { p.currentDirectoryURL = workingDirectory }
        if let environment { p.environment = environment }
        let output = Pipe()
        p.standardOutput = output
        p.standardError = output
        p.standardInput = FileHandle.nullDevice
        let lines = LineBuffer()
        output.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let chunk = handle.availableData
            if chunk.isEmpty {
                handle.readabilityHandler = nil    // end of file
                return
            }
            let complete = lines.append(chunk)
            guard !complete.isEmpty else { return }
            DispatchQueue.main.async {
                MainActor.assumeIsolated { complete.forEach { self?.onLine?($0) } }
            }
        }
        p.terminationHandler = { [weak self] ended in
            let status = ended.terminationStatus, id = ObjectIdentifier(ended)
            DispatchQueue.main.async {
                MainActor.assumeIsolated { self?.exited(id, status: status) }
            }
        }
        stopping = false
        startedAt = Date()
        try p.run()
        process = p
    }

    /// Sends SIGTERM and returns at once; SIGKILL follows if the process is still running 1.5 s later.
    func stop() {
        guard let p = process, p.isRunning else { return }
        stopping = true
        p.terminate()
        Task { [p] in
            try? await Task.sleep(for: .seconds(Self.stopTimeout))
            if p.isRunning { kill(p.processIdentifier, SIGKILL) }
        }
    }

    /// For quitting: SIGTERM, then SIGKILL if it is still running 1.5 s later; returns once it has exited.
    func stopAndWait() {
        guard let p = process, p.isRunning else { return }
        stopping = true
        p.terminate()
        let deadline = Date().addingTimeInterval(Self.stopTimeout)
        while p.isRunning && Date() < deadline { usleep(50_000) }
        if p.isRunning {
            kill(p.processIdentifier, SIGKILL)
            p.waitUntilExit()
        }
    }

    private func exited(_ id: ObjectIdentifier, status: Int32) {
        guard let process, ObjectIdentifier(process) == id else { return }
        onExit?(status, stopping, Date().timeIntervalSince(startedAt))
    }
}

/// Bytes from a pipe, cut into lines; called from the pipe's reading queue.
private final class LineBuffer: @unchecked Sendable {
    private static let newline = UInt8(ascii: "\n")
    private let lock = NSLock()
    private var partial = Data()

    /// Adds `chunk` and returns the lines it completed, without their newlines.
    func append(_ chunk: Data) -> [String] {
        lock.withLock {
            partial.append(chunk)
            var lines: [String] = []
            while let newline = partial.firstIndex(of: Self.newline) {
                lines.append(String(decoding: partial[partial.startIndex..<newline], as: UTF8.self))
                partial.removeSubrange(partial.startIndex...newline)
            }
            return lines
        }
    }
}
