import Foundation

/// A line-based serial console: raw bytes, no echo, lines ending in \n.
final class SerialConsole {
    private static let newline = UInt8(ascii: "\n")
    private static let writeRetryMilliseconds: Int32 = 500

    private let fd: Int32
    private var buffer = Data()
    /// The port went away.
    private(set) var isClosed = false

    init?(path: String) {
        fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard fd >= 0 else { return nil }
        var settings = termios()
        tcgetattr(fd, &settings)
        cfmakeraw(&settings)
        cfsetspeed(&settings, speed_t(B115200))    // not 1200: that rate resets the board into its bootloader
        settings.c_cflag |= tcflag_t(CLOCAL | CREAD)
        tcsetattr(fd, TCSANOW, &settings)
        tcflush(fd, TCIOFLUSH)                       // discard input buffered before opening
    }

    deinit { close(fd) }

    func send(_ line: String) -> Bool {
        let bytes = Array((line + "\n").utf8)
        var offset = 0
        while offset < bytes.count {
            let n = bytes[offset...].withUnsafeBufferPointer { write(fd, $0.baseAddress, $0.count) }
            if n > 0 { offset += n; continue }
            if n < 0 && errno == EAGAIN {
                var p = pollfd(fd: fd, events: Int16(POLLOUT), revents: 0)
                _ = poll(&p, 1, Self.writeRetryMilliseconds)
                continue
            }
            isClosed = true
            return false
        }
        return true
    }

    /// The next line without its \r, or nil after `timeout` seconds or once the port is closed.
    func readLine(timeout: TimeInterval) -> String? {
        let deadline = Date().addingTimeInterval(timeout)
        while true {
            if let nl = buffer.firstIndex(of: Self.newline) {
                let line = String(decoding: buffer[buffer.startIndex..<nl], as: UTF8.self)
                    .trimmingCharacters(in: CharacterSet(charactersIn: "\r"))
                buffer.removeSubrange(buffer.startIndex...nl)
                return line
            }
            let left = deadline.timeIntervalSinceNow
            if left <= 0 || isClosed { return nil }
            var p = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
            guard poll(&p, 1, Int32(left * 1000) + 1) > 0 else { continue }
            var chunk = [UInt8](repeating: 0, count: 512)
            let n = read(fd, &chunk, chunk.count)
            if n > 0 { buffer.append(contentsOf: chunk[0..<n]) }
            else if n == 0 || (errno != EAGAIN && errno != EINTR) { isClosed = true }
        }
    }
}
