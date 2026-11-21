import Foundation

/// The tagged lines the helper and the simulator print with `--machine`: `@nestbox {json}`, one event per line.
enum MachineLine {
    static let tag = "@nestbox "

    /// The line's JSON decoded as `T`; nil for an untagged line or one that does not decode.
    static func decode<T: Decodable>(_ type: T.Type, from line: String) -> T? {
        guard line.hasPrefix(tag) else { return nil }
        return try? JSONDecoder().decode(type, from: Data(line.dropFirst(tag.count).utf8))
    }
}
