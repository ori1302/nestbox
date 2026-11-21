import Foundation

/// GET /v1, as far as the app uses it: what the pet does and why, and the device.
struct PetStatus: Decodable, Equatable {
    let pet: String?
    let doing: String?
    let why: String?
    let device: String?
    let id: String?

    /// A simulator: "simulator" in its device name, or an id starting with "sim-" (the companion helper's rule too).
    var isSimulator: Bool { (device?.contains("simulator") ?? false) || (id?.hasPrefix("sim-") ?? false) }

    private struct Now: Decodable {
        let pet: String?
        let doing: String?
        let why: String?
    }

    private struct Device: Decodable {
        let name: String?
        let id: String?
    }

    private enum CodingKeys: String, CodingKey {
        case now, device
    }

    init(pet: String?, doing: String?, why: String?, device: String?, id: String?) {
        self.pet = pet
        self.doing = doing
        self.why = why
        self.device = device
        self.id = id
    }

    init(from decoder: Decoder) throws {
        let fields = try decoder.container(keyedBy: CodingKeys.self)
        let now = try fields.decodeIfPresent(Now.self, forKey: .now)
        let device = try fields.decodeIfPresent(Device.self, forKey: .device)
        self.init(pet: now?.pet, doing: now?.doing, why: now?.why, device: device?.name, id: device?.id)
    }
}

/// A pet on the device (GET /v1/pets): its name in the API, and its title.
struct DevicePet: Decodable, Equatable {
    let id: String
    let name: String

    private enum CodingKeys: String, CodingKey {
        case name, title
    }

    init(from decoder: Decoder) throws {
        let fields = try decoder.container(keyedBy: CodingKeys.self)
        id = try fields.decode(String.self, forKey: .name)
        name = try fields.decodeIfPresent(String.self, forKey: .title) ?? id
    }
}

/// GET /v1/pets.
struct PetList: Decodable {
    let pets: [DevicePet]
    let current: String?

    private enum CodingKeys: String, CodingKey {
        case pets
        case current = "out"
    }
}

/// A POST /v1/tell the app sends: some of its keys.
struct PetMessage: Encodable {
    var from: String?
    var state: String?
    var doing: String?
    var say: String?
    var sign: String?
    var seconds: Int?

    private enum CodingKeys: String, CodingKey {
        case from, state, say, sign
        case doing = "do"
        case seconds = "for"
    }
}

/// POST /v1: brings out another pet.
struct PetChoice: Encodable {
    let pet: String
}

/// POST /v1 {"wifi": "forget"}: the pet leaves its Wi-Fi and starts setup with a phone again.
struct ForgetWiFi: Encodable {
    let wifi = "forget"
}

/// The pet's answer: what it does now.
struct MessageReply: Decodable {
    let ok: Bool?
    let now: Now?

    struct Now: Decodable {
        let doing: String?
    }

    var doing: String? { now?.doing }
}

/// The HTTP statuses the app tells apart; `noAnswer` stands for no reply at all.
enum HTTPStatus {
    static let ok = 200, unauthorized = 401, noAnswer = 0
}

/// HTTP to the pet with short timeouts. Completions run on the main actor.
@MainActor
enum PetClient {
    private static let session: URLSession = {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 1.5
        config.timeoutIntervalForResource = 2
        return URLSession(configuration: config)
    }()

    static func url(_ base: URL, _ path: String) -> URL {
        base.appendingPathComponent(path.hasPrefix("/") ? String(path.dropFirst()) : path)
    }

    /// The decoded body of a 200 answer, else nil.
    static func get<T: Decodable>(_ type: T.Type, _ path: String, from base: URL,
                                  completion: @escaping @MainActor (T?) -> Void) {
        Task {
            let (status, data) = await perform(URLRequest(url: url(base, path)))
            completion(status == HTTPStatus.ok ? data.flatMap { try? JSONDecoder().decode(type, from: $0) } : nil)
        }
    }

    /// The HTTP status (0 without an answer) and the body as text.
    static func getText(_ path: String, from base: URL, completion: @escaping @MainActor (Int, String?) -> Void) {
        Task {
            let (status, data) = await perform(URLRequest(url: url(base, path)))
            completion(status, data.map { String(decoding: $0, as: UTF8.self) })
        }
    }

    /// The key from pairing, as the helper keeps it in ~/.nestbox/device ("key=..."), if there is one.
    static var key: String? {
        let file = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".nestbox/device")
        guard let text = try? String(contentsOf: file, encoding: .utf8) else { return nil }
        return text.split(separator: "\n").first { $0.hasPrefix("key=") }.map { String($0.dropFirst(4)) }
    }

    /// POSTs a body to `path` (default POST /v1/tell), with the key if there is one; the HTTP status and the
    /// pet's reply.
    static func post<Body: Encodable>(_ body: Body, _ path: String = "/v1/tell", to base: URL,
                                      completion: @escaping @MainActor (Int, MessageReply?) -> Void) {
        guard let body = try? JSONEncoder().encode(body) else {
            completion(0, nil)
            return
        }
        var request = URLRequest(url: url(base, path))
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        if let key { request.setValue("Bearer " + key, forHTTPHeaderField: "Authorization") }
        request.httpBody = body
        Task {
            let (status, data) = await perform(request)
            completion(status, data.flatMap { try? JSONDecoder().decode(MessageReply.self, from: $0) })
        }
    }

    private static func perform(_ request: URLRequest) async -> (Int, Data?) {
        guard let (data, response) = try? await session.data(for: request) else { return (0, nil) }
        return ((response as? HTTPURLResponse)?.statusCode ?? 0, data)
    }
}
