import Foundation

enum ProcessTable {
    private static let maxProcesses = 8192
    private static let pathBufferSize = 4 * Int(MAXPATHLEN)    // PROC_PIDPATHINFO_MAXSIZE, a macro Swift can't see

    /// The processes running the executable at `path`.
    static func pids(runningExecutableAt path: String) -> [pid_t] {
        var resolved = [CChar](repeating: 0, count: Int(PATH_MAX))
        // proc_pidpath reports the resolved path (/private/tmp, not /tmp).
        let wanted = realpath(path, &resolved) != nil ? string(resolved) : path
        var pids = [pid_t](repeating: 0, count: maxProcesses)
        let count = Int(proc_listallpids(&pids, Int32(pids.count * MemoryLayout<pid_t>.size)))
        var buffer = [CChar](repeating: 0, count: pathBufferSize)
        return pids.prefix(max(0, count)).filter { pid in
            pid > 0 && proc_pidpath(pid, &buffer, UInt32(buffer.count)) > 0 && string(buffer) == wanted
        }
    }

    private static func string(_ cString: [CChar]) -> String {
        String(decoding: cString.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
    }
}
