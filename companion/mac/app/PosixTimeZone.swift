import Foundation

enum PosixTimeZone {
    private static let zoneInfo = "/usr/share/zoneinfo/"
    private static let newline = UInt8(ascii: "\n")

    /// The zone as a POSIX TZ rule, e.g. Europe/Berlin -> CET-1CEST,M3.5.0,M10.5.0/3: the footer line of its
    /// TZif file (version 2 and later). Nil if there is none.
    static func rule(for identifier: String = TimeZone.current.identifier) -> String? {
        guard !identifier.contains(".."), let data = FileManager.default.contents(atPath: zoneInfo + identifier),
              data.count > 6, data.starts(with: Array("TZif".utf8)), data[data.startIndex + 4] != 0,
              data.last == newline
        else { return nil }
        let body = data.dropLast()
        guard let lineStart = body.lastIndex(of: newline) else { return nil }
        let rule = String(decoding: body[body.index(after: lineStart)...], as: UTF8.self)
        return !rule.isEmpty && rule.unicodeScalars.allSatisfy({ $0.isASCII && $0.value > 32 }) ? rule : nil
    }
}
