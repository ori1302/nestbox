import Foundation

/// What the pet window (nestbox-sim) reports with `--machine` (simulator/main.cpp).
enum SimulatorEvent: Decodable, Equatable {
    case listening(port: Int)
    case failed(reason: Reason, message: String)

    enum Reason: String, Decodable {
        case cannotListen = "cannot_listen"
        case noEggs = "no_eggs"
    }

    private enum CodingKeys: String, CodingKey {
        case event, port, reason, message
    }

    init(from decoder: Decoder) throws {
        let fields = try decoder.container(keyedBy: CodingKeys.self)
        switch try fields.decode(String.self, forKey: .event) {
        case "listening": self = .listening(port: try fields.decode(Int.self, forKey: .port))
        case "failed": self = .failed(reason: try fields.decode(Reason.self, forKey: .reason),
                                      message: try fields.decode(String.self, forKey: .message))
        case let other:
            throw DecodingError.dataCorruptedError(forKey: .event, in: fields,
                                                   debugDescription: "unknown simulator event \(other)")
        }
    }
}
