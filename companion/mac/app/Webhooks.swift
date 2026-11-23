import CryptoKit
import Foundation
import Security

/// Webhooks from the internet, through the NestBox relay (hooks.nestbox.pet): a service posts a tell to this Mac's
/// webhook URL with the secret in `X-NestBox-Secret`, the relay hands it over the socket this app keeps open, and
/// the app passes it to the pet as POST /v1/tell. The relay is in the middle by design: what a service sends passes
/// through it, so nothing personal or sensitive belongs in a webhook.
///
/// Three secrets, all in the login keychain: the access key the relay's owner gave the user; the mailbox key, made
/// here, whose sha256 is the URL's id (receiving needs the key itself, which only travels to the relay); and the
/// webhook secret, made here, which every webhook must carry.
enum Webhook {
    /// The URL's id for a mailbox key: base64url(sha256(key)), as the relay computes it.
    static func id(of mailboxKey: String) -> String { base64url(Data(SHA256.hash(data: Data(mailboxKey.utf8)))) }

    /// 32 random bytes, base64url.
    static func randomKey() -> String {
        var bytes = [UInt8](repeating: 0, count: 32)
        _ = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        return base64url(Data(bytes))
    }

    /// What the relay sends for each webhook.
    struct Frame: Decodable {
        let headers: [String: String]
        let body: String
    }

    /// The tell in a relayed webhook when its secret is right and its body a JSON object; nil drops it.
    static func tell(in frame: String, secret: String) -> Data? {
        guard let frame = try? JSONDecoder().decode(Frame.self, from: Data(frame.utf8)),
              let given = frame.headers["x-nestbox-secret"], same(given, secret) else { return nil }
        let body = Data(frame.body.utf8)
        guard (try? JSONSerialization.jsonObject(with: body)) is [String: Any] else { return nil }
        return body
    }

    /// Compares in time that does not depend on where the strings differ.
    static func same(_ a: String, _ b: String) -> Bool {
        let x = Array(a.utf8), y = Array(b.utf8)
        guard x.count == y.count else { return false }
        return zip(x, y).reduce(UInt8(0)) { $0 | ($1.0 ^ $1.1) } == 0
    }

    private static func base64url(_ data: Data) -> String {
        data.base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }
}

/// Generic passwords in the login keychain under one service.
struct KeychainSecrets {
    let service: String

    func get(_ account: String) -> String? {
        let query: [CFString: Any] = [kSecClass: kSecClassGenericPassword, kSecAttrService: service,
                                      kSecAttrAccount: account, kSecReturnData: true]
        var found: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &found) == errSecSuccess, let data = found as? Data
        else { return nil }
        return String(data: data, encoding: .utf8)
    }

    func set(_ account: String, _ value: String?) {
        let item: [CFString: Any] = [kSecClass: kSecClassGenericPassword, kSecAttrService: service,
                                     kSecAttrAccount: account]
        SecItemDelete(item as CFDictionary)
        guard let value else { return }
        var add = item
        add[kSecValueData] = Data(value.utf8)
        add[kSecAttrAccessible] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        SecItemAdd(add as CFDictionary, nil)
    }
}

/// Keeps this Mac's mailbox on the relay and its socket open, and hands each verified webhook to `onTell`.
@MainActor
final class WebhookBridge {
    enum State: Equatable {
        case off              // no access key
        case paused           // the user paused it: disconnected, so webhooks get 404; URL and secret kept
        case connecting
        case connected
        case refused          // the relay does not accept the access key
        case full             // the user's mailbox allowance is used up
        case removed          // the relay's owner removed the mailbox, or ended access
        case replaced         // another Mac connected with the same mailbox
        case unreachable      // no answer; trying again
    }

    static let defaultRelay = URL(string: "https://hooks.nestbox.pet")!
    private static let pingEvery: TimeInterval = 30
    private static let silentAfter: TimeInterval = 75    // no frame, not even a pong, for this long: reconnect
    private static let longestWait: TimeInterval = 60

    let relay: URL
    private let secrets: KeychainSecrets
    var onTell: ((Data) -> Void)?
    var onNote: ((String) -> Void)?
    var onChange: (() -> Void)?

    /// Paused by the user: nothing (a new URL, a new key, a network change) connects until `resume`.
    private(set) var paused = false
    private(set) var state: State = .off {
        didSet { if state != oldValue { onChange?() } }
    }
    private var task: URLSessionWebSocketTask?
    private var generation = 0
    private var pinger: Timer?
    private var retry: Timer?
    private var wait: TimeInterval = 2
    private var heard = Date()
    private let session = URLSession(configuration: .ephemeral)

    init(relay: URL, secrets: KeychainSecrets) {
        self.relay = relay
        self.secrets = secrets
    }

    var hasAccessKey: Bool { secrets.get("access") != nil }
    var secret: String? { secrets.get("secret") }
    /// This Mac's webhook URL, once it has a mailbox key.
    var url: URL? { secrets.get("mailbox").map { relay.appendingPathComponent(Webhook.id(of: $0)) } }

    /// Saves (or, with nil, removes) the access key and starts or stops.
    func setAccessKey(_ key: String?) {
        let key = key?.trimmingCharacters(in: .whitespacesAndNewlines)
        secrets.set("access", key?.isEmpty == false ? key : nil)
        stop()
        start()
    }

    /// Creates the mailbox (harmless when it exists) and connects. Without an access key it stays off.
    func start() {
        guard let access = secrets.get("access") else {
            state = .off
            return
        }
        guard !paused else {
            state = .paused
            return
        }
        if secrets.get("mailbox") == nil { secrets.set("mailbox", Webhook.randomKey()) }
        if secrets.get("secret") == nil { secrets.set("secret", Webhook.randomKey()) }
        guard let mailbox = secrets.get("mailbox") else { return }
        state = .connecting
        generation += 1
        let mine = generation
        var request = URLRequest(url: relay.appendingPathComponent("mailboxes"))
        request.httpMethod = "POST"
        request.setValue("Bearer " + access, forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try? JSONEncoder().encode(["id": Webhook.id(of: mailbox)])
        Task {
            let status = await statusOf(request)
            guard mine == generation else { return }
            switch status {
            case 201: connect(access: access, mailbox: mailbox)
            case 401: give(.refused, "the relay did not accept the access key")
            case 409: give(.full, "every mailbox this access key may have is in use; remove one on another Mac")
            default: again("the relay did not answer (\(status))")
            }
        }
    }

    /// Closes the socket and stops trying.
    func stop() {
        generation += 1
        pinger?.invalidate()
        retry?.invalidate()
        task?.cancel(with: .goingAway, reason: nil)
        task = nil
    }

    /// Disconnects and stays so: webhooks get 404 until `resume`.
    func pause() {
        paused = true
        stop()
        state = hasAccessKey ? .paused : .off
    }

    func resume() {
        paused = false
        start()
    }

    /// Deletes this Mac's mailbox on the relay, so its URL stops working and the access key may make another, and
    /// forgets the access key, the URL and the secret here. Without an answer from the relay (or a refusal other than
    /// "gone" or "not yours any more") nothing is forgotten, so the mailbox is never left behind unknown. `done` gets
    /// the relay's answer: 204, 404 or 401, 0 for none, nil when there was no mailbox to delete.
    func removeFromThisMac(_ done: @escaping @MainActor (Int?) -> Void) {
        let access = secrets.get("access"), mailbox = secrets.get("mailbox")
        stop()
        let forget = { [weak self] in
            guard let self else { return }
            for account in ["access", "mailbox", "secret"] { self.secrets.set(account, nil) }
            self.state = .off
        }
        guard let access, let mailbox else {
            forget()
            done(nil)
            return
        }
        var request = URLRequest(url: relay.appendingPathComponent("mailboxes/" + Webhook.id(of: mailbox)))
        request.httpMethod = "DELETE"
        request.setValue("Bearer " + access, forHTTPHeaderField: "Authorization")
        Task {
            let status = await statusOf(request)
            if [204, 404, 401].contains(status) { forget() } else { self.start() }
            done(status)
        }
    }

    /// The Mac woke or changed network: reconnect at once.
    func reachAgain() {
        guard hasAccessKey, state == .connected || state == .connecting || state == .unreachable else { return }
        stop()
        wait = 2
        start()
    }

    // MARK: - The socket

    private func connect(access: String, mailbox: String) {
        var components = URLComponents(url: relay.appendingPathComponent("connect"), resolvingAgainstBaseURL: false)!
        components.scheme = components.scheme == "http" ? "ws" : "wss"
        var request = URLRequest(url: components.url!)
        request.setValue("Bearer " + access, forHTTPHeaderField: "Authorization")
        request.setValue(mailbox, forHTTPHeaderField: "X-NestBox-Mailbox")
        let task = session.webSocketTask(with: request)
        self.task = task
        task.resume()
        heard = Date()
        send("ping")    // its "pong" says the socket is open
        let mine = generation
        pinger = Timer(timeInterval: Self.pingEvery, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, mine == self.generation else { return }
                if Date().timeIntervalSince(self.heard) > Self.silentAfter {
                    self.again("the relay went quiet")
                } else {
                    self.send("ping")
                }
            }
        }
        RunLoop.main.add(pinger!, forMode: .common)
        receive(task, mine)
    }

    private func receive(_ task: URLSessionWebSocketTask, _ mine: Int) {
        Task {
            do {
                let message = try await task.receive()
                guard mine == generation else { return }
                heard = Date()
                if case .string(let text) = message { take(text) }
                receive(task, mine)
            } catch {
                guard mine == generation else { return }
                closed(task)
            }
        }
    }

    private func take(_ text: String) {
        if text == "pong" {
            if state != .connected {
                state = .connected
                wait = 2
                onNote?("webhooks: connected to \(relay.host ?? "the relay")")
            }
            return
        }
        guard let secret = secrets.get("secret"), let tell = Webhook.tell(in: text, secret: secret) else {
            onNote?("webhooks: dropped one without the right secret, or not a JSON object")
            return
        }
        onTell?(tell)
    }

    private func closed(_ task: URLSessionWebSocketTask) {
        let status = (task.response as? HTTPURLResponse)?.statusCode
        switch task.closeCode.rawValue {
        case 4000: give(.replaced, "another Mac connected with this webhook URL; Remove Webhooks from This Mac… and "
                                   + "the access key again give this Mac its own")
        case 4001, 4003: give(.removed, "the relay removed this mailbox or ended the access key")
        default:
            if status == 401 { give(.removed, "the relay no longer knows this mailbox") }
            else { again("the connection closed") }
        }
    }

    private func send(_ text: String) {
        task?.send(.string(text)) { _ in }
    }

    /// Stops trying, in `final` state, with a note.
    private func give(_ final: State, _ note: String) {
        stop()
        state = final
        onNote?("webhooks: " + note)
    }

    /// Tries again after a growing wait (2 s up to a minute).
    private func again(_ note: String) {
        stop()
        state = .unreachable
        onNote?("webhooks: \(note); trying again in \(Int(wait)) s")
        let mine = generation
        retry = Timer(timeInterval: wait, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, mine == self.generation else { return }
                self.start()
            }
        }
        RunLoop.main.add(retry!, forMode: .common)
        wait = min(wait * 2, Self.longestWait)
    }

    private func statusOf(_ request: URLRequest) async -> Int {
        guard let (_, response) = try? await session.data(for: request) else { return 0 }
        return (response as? HTTPURLResponse)?.statusCode ?? 0
    }
}
