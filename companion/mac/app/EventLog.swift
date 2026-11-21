/// The companion's output and the app's own notes ("-- ..."), oldest first, capped.
@MainActor
final class EventLog {
    private static let capacity = 2000

    private(set) var lines: [String] = []
    var onAppend: ((String) -> Void)?

    func append(_ line: String) {
        lines.append(line)
        if lines.count > Self.capacity { lines.removeFirst(lines.count - Self.capacity) }
        onAppend?(line)
    }
}
