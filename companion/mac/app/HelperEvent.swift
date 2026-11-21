import Foundation

/// What the companion helper reports with `--machine` (companion/mac/cli/activity.cpp).
enum HelperEvent: Decodable, Equatable {
    case found(url: URL, id: String)
    case paired(id: String)
    case connected(url: URL)
    case lost(url: URL)
    case sent(status: Int)
    case needsKey
    case pairWaiting
    case pairDone
    case pairFailed(detail: String)

    private enum CodingKeys: String, CodingKey {
        case event, url, id, status, detail
    }

    init(from decoder: Decoder) throws {
        let fields = try decoder.container(keyedBy: CodingKeys.self)
        switch try fields.decode(String.self, forKey: .event) {
        case "found": self = .found(url: try fields.decode(URL.self, forKey: .url),
                                    id: try fields.decode(String.self, forKey: .id))
        case "paired": self = .paired(id: try fields.decode(String.self, forKey: .id))
        case "connected": self = .connected(url: try fields.decode(URL.self, forKey: .url))
        case "lost": self = .lost(url: try fields.decode(URL.self, forKey: .url))
        case "sent": self = .sent(status: try fields.decode(Int.self, forKey: .status))
        case "needs_key": self = .needsKey
        case "pair_waiting": self = .pairWaiting
        case "pair_done": self = .pairDone
        case "pair_failed": self = .pairFailed(detail: try fields.decodeIfPresent(String.self, forKey: .detail) ?? "")
        case let other:
            throw DecodingError.dataCorruptedError(forKey: .event, in: fields,
                                                   debugDescription: "unknown helper event \(other)")
        }
    }
}
