/// The device console's replies that Wi-Fi setup reads. firmware/esp32/src/console_reply.h holds the same strings.
enum ConsoleReply {
    static let wifiPrefix = "wifi: "
    static let joiningPrefix = "joining \""
    static let onlinePrefix = "on \""
    static let addressSeparator = "\" as "
    static let hostPrefix = " (http://"

    /// The reply to `wifi <ssid> <password>`: `wifi: joining "<ssid>"...`.
    static func isJoining(_ line: String) -> Bool { line.hasPrefix(wifiPrefix + joiningPrefix) }

    /// The address and host name in `wifi: on "<ssid>" as <ip> (http://<host>), ...`, printed when the device
    /// joins and in answer to `wifi`; nil for any other line.
    static func online(_ line: String) -> (address: String, host: String)? {
        guard line.hasPrefix(wifiPrefix + onlinePrefix) else { return nil }
        let report = line.dropFirst(wifiPrefix.count)
        guard let at = report.range(of: addressSeparator, options: .backwards) else {
            return nil
        }
        let address = report[at.upperBound...].prefix { $0 != " " }
        var host = ""
        if let url = report.range(of: hostPrefix) {
            host = String(report[url.upperBound...].prefix { $0 != ")" && $0 != ":" && $0 != "/" })
        }
        return (String(address), host)
    }
}
