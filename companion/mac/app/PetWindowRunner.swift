import Foundation

/// Runs the developer pet window (nestbox-sim) that the menu shows and hides.
@MainActor
final class PetWindowRunner {
    let executable: URL?
    private let arguments: [String]

    var onChange: (() -> Void)?
    var onChildrenChanged: (() -> Void)?

    private(set) var process: ChildProcess?
    private(set) var url: URL?
    private(set) var note: String?
    /// The screen to show the pet on (a simulator display id), used from the next start; nil: the default.
    var screen: String?

    init(executable: URL?, arguments: [String]) {
        self.executable = executable
        self.arguments = arguments + ["--machine"]
    }

    var isRunning: Bool { process?.isRunning ?? false }

    func start() {
        guard let executable else {
            note = "no pet window on this Mac (build/nestbox-sim)"
            onChange?()
            return
        }
        let child = ChildProcess(executable: executable, arguments: (screen.map { ["--display", $0] } ?? []) + arguments,
                                 workingDirectory: executable.deletingLastPathComponent())
        var failure: String?
        child.onLine = { [weak self] line in
            switch MachineLine.decode(SimulatorEvent.self, from: line) {
            case .listening(let port):
                self?.url = URL(string: "http://127.0.0.1:\(port)")
                self?.note = nil
                self?.onChange?()
            case .failed(let reason, let message):
                failure = Self.describe(reason, message)
            case nil:
                break
            }
        }
        child.onExit = { [weak self, weak child] status, requested, _ in
            guard let self, let child, self.process === child else { return }
            self.url = nil
            if !requested {    // closed from its own window (Q / Esc), or failed
                self.note = failure ?? (status == 0 ? nil : "pet window stopped (exit \(status))")
            }
            self.onChildrenChanged?()
            self.onChange?()
        }
        do {
            try child.start()
            process = child
            note = nil
        } catch {
            note = "could not start: \(error.localizedDescription)"
        }
        onChildrenChanged?()
        onChange?()
    }

    func stop() {
        process?.stop()
        process = nil
        url = nil
    }

    /// For quitting: returns once the pet window has exited.
    func stopAndWait() {
        process?.stopAndWait()
        process = nil
        url = nil
    }

    private static func describe(_ reason: SimulatorEvent.Reason, _ message: String) -> String {
        switch reason {
        case .cannotListen: return "cannot listen: \(message)"
        case .noEggs: return message
        }
    }
}
