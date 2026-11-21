import Foundation

/// How the firmware reads `wifi <ssid> <password>` (firmware/esp32/src/console.cpp): what the pet will get.
func firmwareWiFiCommand(_ raw: String) -> (ssid: String, pass: String)? {
    let line = raw.trimmingCharacters(in: .whitespacesAndNewlines)
    guard line.hasPrefix("wifi ") else { return nil }
    let rest = String(line.dropFirst(5)).trimmingCharacters(in: .whitespacesAndNewlines)
    var ssid = "", pass = ""
    if rest.hasPrefix("\"") {
        let body = rest.dropFirst()
        guard let end = body.firstIndex(of: "\"") else { return nil }
        ssid = String(body[..<end])
        pass = String(body[body.index(after: end)...])
    } else if let space = rest.firstIndex(of: " ") {
        ssid = String(rest[..<space])
        pass = String(rest[rest.index(after: space)...])
    } else {
        ssid = rest
    }
    return (ssid, pass.trimmingCharacters(in: .whitespacesAndNewlines))
}

/// A pty that answers like the firmware's USB console: `now`, `tz`, `wifi`, `wifi <ssid> <password>`, with
/// unrelated log lines in between. It joins only with the right password. A mute console answers nothing.
/// Its state is behind `lock`: the console runs on a thread of its own.
final class FakeConsole: @unchecked Sendable {
    let path: String
    private let master: Int32
    private let device: String, id: String, host: String, password: String, mute: Bool
    private let lock = NSLock()
    private var receivedTZ: String?
    private var received: (ssid: String, pass: String)?
    private var running = true

    var tz: String? { lock.withLock { receivedTZ } }
    var got: (ssid: String, pass: String)? { lock.withLock { received } }
    private var isRunning: Bool { lock.withLock { running } }

    init?(device: String, id: String, host: String, password: String, mute: Bool = false) {
        master = posix_openpt(O_RDWR | O_NOCTTY)
        guard master >= 0, grantpt(master) == 0, unlockpt(master) == 0, let name = ptsname(master) else { return nil }
        path = String(cString: name)
        self.device = device
        self.id = id
        self.host = host
        self.password = password
        self.mute = mute
        Thread.detachNewThread { [self] in serve() }
    }

    func stop() {
        lock.withLock { running = false }
        close(master)
    }

    private func say(_ text: String) {
        let bytes = Array((text + "\r\n").utf8)
        _ = bytes.withUnsafeBufferPointer { write(master, $0.baseAddress, $0.count) }
    }

    // net::status() on a network.
    private func online(_ ssid: String) -> String {
        ConsoleReply.onlinePrefix + ssid + ConsoleReply.addressSeparator + "10.0.0.9" + ConsoleReply.hostPrefix + host
            + "), signal -48 dBm"
    }

    private func serve() {
        var buffer = [UInt8](), ssid = ""
        var joinAt: Date?, joined = false
        while isRunning {
            var p = pollfd(fd: master, events: Int16(POLLIN), revents: 0)
            if poll(&p, 1, 100) > 0 {
                var chunk = [UInt8](repeating: 0, count: 256)
                let n = read(master, &chunk, chunk.count)
                if n <= 0 {
                    if errno == EAGAIN || errno == EIO { usleep(50_000); continue }
                    return
                }
                buffer += chunk[0..<n]
            }
            while let end = buffer.firstIndex(where: { $0 == 10 || $0 == 13 }) {
                let line = String(decoding: buffer[..<end], as: UTF8.self).trimmingCharacters(in: .whitespaces)
                buffer.removeSubrange(...end)
                if line.isEmpty { continue }
                if mute {
                    if let command = firmwareWiFiCommand(line) { lock.withLock { received = command } }
                    continue
                }
                say("{\"press\":\"single\"} -> {\"ok\": true, \"input\": \"petted\"}")    // unrelated log output
                if line == "now" {
                    let idField = id.isEmpty ? "" : ", \"id\": \"\(id)\""
                    say("{\"ok\": true, \"now\": {\"pet\": \"nibble\", \"doing\": \"stand\"}, "
                        + "\"device\": {\"name\": \"\(device)\"\(idField)}}")
                } else if line.hasPrefix("tz ") {
                    lock.withLock { receivedTZ = String(line.dropFirst(3)) }
                    say("time: (not set yet), zone \(line.dropFirst(3))")
                } else if line == "wifi" {
                    let joining = ConsoleReply.joiningPrefix + ssid + "\"..."
                    say(ConsoleReply.wifiPrefix
                        + (joinAt == nil ? "no network set: wifi <ssid> <password>" : joined ? online(ssid) : joining))
                } else if let command = firmwareWiFiCommand(line) {
                    lock.withLock { received = command }
                    ssid = command.ssid
                    joinAt = command.pass == password ? Date() : .distantFuture
                    joined = false
                    let detail = command.pass.isEmpty ? " with no password" : ""
                    say(ConsoleReply.wifiPrefix + ConsoleReply.joiningPrefix + command.ssid
                        + "\"\(detail) (remembered)")
                } else {
                    say("unknown command; type help")
                }
            }
            if let at = joinAt, at.timeIntervalSinceNow < -1.5 {
                say(ConsoleReply.wifiPrefix + online(ssid))
                joinAt = .distantFuture
                joined = true
            }
        }
    }
}
