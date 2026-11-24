import Foundation

/// The pet's part of GET /v1 about webhooks (device.relay), on firmware that keeps the relay's socket itself.
struct RelayStatus: Decodable, Equatable {
    /// "off", "waiting", "connecting", "connected", "refused" or "replaced".
    let state: String
    /// The pet holds a mailbox key.
    let set: Bool
    /// Webhooks are received (not paused).
    let on: Bool
}

/// POST /v1 {"relay": ...}: the keys for the pet, "on", or "forget".
private struct RelayKeys: Encodable {
    struct Keys: Encodable {
        var mailbox: String?
        var secret: String?
        var on: Bool?
    }
    let relay: Keys
}

private struct RelayForget: Encodable {
    let relay = "forget"
}

/// Webhooks through the pet: the pet keeps its own socket to the relay, so webhooks reach it with the Mac asleep or
/// off. This app makes the pet's mailbox on the relay with the user's access key, hands the pet only the mailbox key
/// and the secret (POST /v1 with the device's key; the access key stays on this Mac), and keeps a copy of the URL
/// and secret for the menu. The pet and the developer bridge (`WebhookBridge`) use mailboxes of their own, so they
/// never take a socket from each other.
///
/// In the secret store, beside the bridge's: "access" (shared with the bridge), "pet-mailbox", "pet-secret",
/// "pet-holder" (the id of the pet that has them) and "pet-forget" (a pet that still has to be told to forget).
@MainActor
final class PetWebhooks {
    enum State: Equatable {
        case noAccessKey
        case noPet            // no pet answers
        case unsupported      // the pet's firmware has no relay link
        case notPaired        // the pet answering isn't the paired one, which is the one that gets the keys
        case needsKey         // the pet wants its key from pairing before it takes the relay's keys
        case refused          // the relay doesn't accept the access key
        case full             // every mailbox the access key may have is in use
        case handing          // giving the pet its keys
        case pet(RelayStatus) // what the pet reports
    }

    private static let retryHandOver: TimeInterval = 30

    let relay: URL
    private let secrets: SecretStore
    var onNote: ((String) -> Void)?
    var onChange: (() -> Void)?
    /// The bridge's mailbox moved to the pet: the bridge must stop (it would take the pet's socket).
    var onAdoptBridgeMailbox: (() -> Void)?
    /// The address of another pet on the network, by its id, to tell it to forget keys it no longer should have.
    var addressOfPet: ((String) -> URL?)?

    private(set) var problem: State?
    private var handing = false
    private var force = false
    private var lastTry = Date.distantPast

    init(relay: URL, secrets: SecretStore) {
        self.relay = relay
        self.secrets = secrets
    }

    var hasAccessKey: Bool { secrets.get("access") != nil }
    var secret: String? { secrets.get("pet-secret") }
    var url: URL? { secrets.get("pet-mailbox").map { relay.appendingPathComponent(Webhook.id(of: $0)) } }
    /// The bridge has a mailbox an older app made, still to move to the pet.
    var bridgeHasOldMailbox: Bool { secrets.get("pet-mailbox") == nil && secrets.get("mailbox") != nil }

    /// What the menu shows, from the pet's last GET /v1.
    func state(_ status: PetStatus?, mayHold: Bool) -> State {
        if !hasAccessKey { return .noAccessKey }
        if let problem { return problem }
        if handing { return .handing }
        guard let status else { return .noPet }
        guard let relay = status.relay else { return .unsupported }
        if !mayHold && !relay.set { return .notPaired }
        return .pet(relay)
    }

    /// Saves the access key (nil removes it) and gives the paired pet its keys again, even if it has some.
    func setAccessKey(_ key: String?, pet: URL?, status: PetStatus?, receiving: Bool) {
        let key = key?.trimmingCharacters(in: .whitespacesAndNewlines)
        secrets.set("access", key?.isEmpty == false ? key : nil)
        secrets.set("pet-forget", nil)
        problem = nil
        force = true
        onChange?()
        follow(status, pet: pet, receiving: receiving)
    }

    /// After each GET /v1 of a pet that may hold the keys: a pet that has none, or was refused, gets them (at most
    /// every 30 s); a pet that should forget its keys is told again.
    func follow(_ status: PetStatus?, pet: URL?, receiving: Bool = true) {
        guard let pet, let relay = status?.relay, !handing else { return }
        if secrets.get("pet-forget") != nil {
            guard relay.set else {
                secrets.set("pet-forget", nil)
                return
            }
            PetClient.post(RelayForget(), "/v1", to: pet) { [weak self] status, _ in
                if status == HTTPStatus.ok { self?.secrets.set("pet-forget", nil) }
            }
            return
        }
        guard hasAccessKey else { return }
        let due = force || Date().timeIntervalSince(lastTry) > Self.retryHandOver
        guard due, force || !relay.set || relay.state == "refused" else { return }
        force = false
        if bridgeHasOldMailbox, let mailbox = secrets.get("mailbox"), let secret = secrets.get("secret") {
            // A URL the bridge made before the pet could hold one: the pet takes it over, so services keep working.
            onAdoptBridgeMailbox?()
            secrets.set("pet-mailbox", mailbox)
            secrets.set("pet-secret", secret)
            secrets.set("mailbox", nil)
            secrets.set("secret", nil)
            onNote?("webhooks: this Mac's webhook URL now goes to the pet")
        }
        handOver(to: pet, id: status?.id, on: relay.set ? relay.on : receiving, fresh: false)
    }

    /// Pauses or resumes webhooks on the pet; the keys stay.
    func setReceiving(_ on: Bool, pet: URL?, done: @escaping @MainActor (Int) -> Void) {
        guard let pet else { return done(HTTPStatus.noAnswer) }
        PetClient.post(RelayKeys(relay: .init(on: on)), "/v1", to: pet) { [weak self] status, _ in
            self?.onNote?("webhooks \(on ? "received" : "paused") on the pet: \(status)")
            done(status)
        }
    }

    /// Deletes the pet's mailbox on the relay, so its URL stops working, takes the keys off the pet (again later if it
    /// doesn't answer now) and forgets them here, with the access key unless the bridge still uses it. Without an
    /// answer from the relay (or a refusal other than "gone" or "not yours any more") nothing is forgotten. `done` gets
    /// the relay's answer: 204, 404 or 401, 0 for none, nil when there was no mailbox.
    func remove(pet: URL?, keepAccessKey: Bool, done: @escaping @MainActor (Int?) -> Void) {
        let forget = { [weak self] in
            guard let self else { return }
            for account in ["pet-mailbox", "pet-secret", "pet-holder"] + (keepAccessKey ? [] : ["access"]) {
                self.secrets.set(account, nil)
            }
            self.problem = nil
            self.secrets.set("pet-forget", "1")
            if let pet {
                PetClient.post(RelayForget(), "/v1", to: pet) { [weak self] status, _ in
                    if status == HTTPStatus.ok { self?.secrets.set("pet-forget", nil) }
                }
            }
            self.onChange?()
        }
        guard let access = secrets.get("access"), let mailbox = secrets.get("pet-mailbox") else {
            forget()
            return done(nil)
        }
        var request = URLRequest(url: relay.appendingPathComponent("mailboxes/" + Webhook.id(of: mailbox)))
        request.httpMethod = "DELETE"
        request.setValue("Bearer " + access, forHTTPHeaderField: "Authorization")
        Task {
            let status = await statusOf(request)
            if [204, 404, 401].contains(status) { forget() }
            done(status)
        }
    }

    /// After pairing: try again at once.
    func paired() {
        if problem == .needsKey { problem = nil }
        force = true
        onChange?()
    }

    // MARK: - Handing over

    /// Makes the mailbox on the relay (harmless when it is already this user's), then gives the pet its key and the
    /// secret. A 409 means the mailbox is another user's (the access key changed) or the allowance is used up: once,
    /// a fresh mailbox is tried.
    private func handOver(to pet: URL, id: String?, on: Bool, fresh: Bool) {
        guard let access = secrets.get("access") else { return }
        if fresh || secrets.get("pet-mailbox") == nil {
            secrets.set("pet-mailbox", Webhook.randomKey())
            secrets.set("pet-secret", Webhook.randomKey())
        }
        if secrets.get("pet-secret") == nil { secrets.set("pet-secret", Webhook.randomKey()) }
        guard let mailbox = secrets.get("pet-mailbox"), let secret = secrets.get("pet-secret") else { return }
        handing = true
        lastTry = Date()
        onChange?()
        var request = URLRequest(url: relay.appendingPathComponent("mailboxes"))
        request.httpMethod = "POST"
        request.setValue("Bearer " + access, forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try? JSONEncoder().encode(["id": Webhook.id(of: mailbox)])
        Task {
            let made = await statusOf(request)
            if made == 409 && !fresh {
                handing = false
                return handOver(to: pet, id: id, on: on, fresh: true)
            }
            guard made == 201 else {
                handing = false
                switch made {
                case 401: give(.refused, "the relay did not accept the access key")
                case 409: give(.full, "every mailbox this access key may have is in use")
                default: onNote?("webhooks: the relay did not answer (\(made)); trying again soon")
                }
                onChange?()
                return
            }
            let keys = RelayKeys(relay: .init(mailbox: mailbox, secret: secret, on: on))
            PetClient.post(keys, "/v1", to: pet) { [weak self] status, reply in
                guard let self else { return }
                self.handing = false
                let warnings = reply?.warnings ?? []
                switch status {
                case HTTPStatus.ok where warnings.isEmpty:
                    self.problem = nil
                    self.onNote?("webhooks: the pet has its keys")
                    self.letGoOfOldPet(newHolder: id)
                case HTTPStatus.ok: self.onNote?("webhooks: the pet did not take its keys: " + warnings.joined(separator: "; "))
                case HTTPStatus.unauthorized: self.give(.needsKey, "the pet wants its key: pair with its button")
                default: self.onNote?("webhooks: the pet did not take its keys (\(status)); trying again soon")
                }
                self.onChange?()
            }
        }
    }

    /// The keys moved to another pet (a new pairing): the one that had them is told to forget, so the two don't take
    /// the URL from each other at every restart.
    private func letGoOfOldPet(newHolder: String?) {
        let old = secrets.get("pet-holder")
        secrets.set("pet-holder", newHolder)
        guard let old, old != newHolder, let address = addressOfPet?(old) else { return }
        PetClient.post(RelayForget(), "/v1", to: address) { [weak self] status, _ in
            self?.onNote?("webhooks: the pet that had them before was told to forget them (\(status))")
        }
    }

    private func give(_ state: State, _ note: String) {
        problem = state
        onNote?("webhooks: " + note)
    }

    private func statusOf(_ request: URLRequest) async -> Int {
        guard let (_, response) = try? await URLSession(configuration: .ephemeral).data(for: request) else { return 0 }
        return (response as? HTTPURLResponse)?.statusCode ?? 0
    }
}
