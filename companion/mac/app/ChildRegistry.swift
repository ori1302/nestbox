import Foundation

/// A file of "pid path" lines for the running children, so the next launch can stop any that outlived a crash.
struct ChildRegistry {
    let file: URL

    @MainActor
    func save(_ children: [ChildProcess]) {
        let lines = children.compactMap { child -> String? in
            guard child.isRunning, let pid = child.pid else { return nil }
            return "\(pid) \(child.executable.path)"
        }
        let directory = file.deletingLastPathComponent()
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try? (lines.joined(separator: "\n") + "\n").write(to: file, atomically: true, encoding: .utf8)
    }

    func stopLeftovers() {
        guard let text = try? String(contentsOf: file, encoding: .utf8) else { return }
        for line in text.split(separator: "\n") {
            let parts = line.split(separator: " ", maxSplits: 1)
            guard parts.count == 2, let pid = pid_t(parts[0]) else { continue }
            // A pid may have been reused: only stop it if it still runs the recorded executable.
            if ProcessTable.pids(runningExecutableAt: String(parts[1])).contains(pid) { kill(pid, SIGTERM) }
        }
        try? FileManager.default.removeItem(at: file)
    }
}
