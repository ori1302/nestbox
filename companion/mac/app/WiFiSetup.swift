import Foundation

/// Puts a new pet on Wi-Fi over its USB serial console (TinyUSB CDC, product "NestBox"), which takes
/// `wifi <ssid> <password>` as typed (https://nestbox.pet/docs/firmware/). The password is sent to the pet only; it
/// is not stored, logged, or shown in "What It Sends".
@MainActor
final class WiFiSetup {
    enum Phase: Equatable, Sendable {
        case idle
        case sending
        case joining(network: String)
        case online(url: URL)          // as Bonjour found it
        case failed(reason: String)    // shown to the user

        var isBusy: Bool {
            switch self {
            case .sending, .joining: return true
            default: return false
            }
        }

        var isOnline: Bool {
            if case .online = self { return true }
            return false
        }
    }

    struct CommandError: Error, Equatable {
        let reason: String
    }

    private enum Timing {
        static let reply: TimeInterval = 3          // for the answers to `now` and to the wifi line
        static let poll: TimeInterval = 0.5
        static let askAgain: TimeInterval = 5       // repeats `wifi` until it reports being on
        static let lookAgain: TimeInterval = 2      // between Bonjour scans once it is on
        static let visibleWithin: TimeInterval = 20 // after it reports being on Wi-Fi, to appear on Bonjour
    }

    private enum Message {
        static let plugIn = "Plug your pet into this Mac with its USB cable."
        static let cannotOpen = "Can't open the pet's USB port. Close any serial monitor and try again."
        static let noAnswer = "Your pet didn't answer over USB. Unplug it, plug it back in, and try again."
        static let unplugged = "The pet was unplugged before it joined. Plug it back in and try again."
    }

    private(set) var phase: Phase = .idle
    private(set) var port: String?
    /// The pet that was plugged in, as Bonjour sees it once it is online.
    private(set) var found: NetworkPet?
    var onChange: (() -> Void)?
    var onOnline: ((NetworkPet) -> Void)?

    private let helper: URL
    private let serialOverride: String?
    private let joinTimeout: TimeInterval
    private let helperEnvironment: [String: String]?
    private let queue = DispatchQueue(label: "pet.nestbox.usb")

    /// `serialOverride` is a console to use instead of looking for the USB device (`--serial PATH`).
    init(helper: URL, serialOverride: String? = nil, joinTimeout: TimeInterval = 60,
         helperEnvironment: [String: String]? = nil) {
        self.helper = helper
        self.serialOverride = serialOverride
        self.joinTimeout = joinTimeout
        self.helperEnvironment = helperEnvironment
    }

    var isBusy: Bool { phase.isBusy }

    func scan() {
        let found = serialOverride ?? USBPet.ports().first
        guard found != port else { return }
        port = found
        onChange?()
    }

    /// The console line as the firmware parses it: a name with spaces, or starting with a quote, goes in quotes;
    /// the password is the rest of the line.
    nonisolated static func command(ssid: String, password: String) -> Result<String, CommandError> {
        func fail(_ reason: String) -> Result<String, CommandError> { .failure(CommandError(reason: reason)) }
        if ssid.isEmpty { return fail("Type the Wi-Fi network's name.") }
        if [ssid, password].contains(where: { $0.contains("\n") || $0.contains("\r") }) {
            return fail("The name and password must be one line each.")
        }
        if password != password.trimmingCharacters(in: .whitespaces) {
            return fail("The pet can't take a password that starts or ends with a space.")
        }
        let quoted = ssid.contains(" ") || ssid.hasPrefix("\"") || ssid != ssid.trimmingCharacters(in: .whitespaces)
        if quoted && ssid.contains("\"") {
            return fail("The pet can't take a network name that starts with a quote mark, "
                        + "or has both spaces and quote marks.")
        }
        let name = quoted ? "\"" + ssid + "\"" : ssid
        return .success("wifi " + name + (password.isEmpty ? "" : " " + password))
    }

    func join(ssid: String, password: String) {
        guard !isBusy else { return }
        let line: String
        switch Self.command(ssid: ssid, password: password) {
        case .failure(let error): fail(error.reason); return
        case .success(let command): line = command
        }
        guard let port else { fail(Message.plugIn); return }
        phase = .sending
        onChange?()
        let attempt = Attempt(port: port, line: line, ssid: ssid, helper: helper, environment: helperEnvironment,
                              timeout: joinTimeout) { [weak self] phase in
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    self?.phase = phase
                    self?.onChange?()
                }
            }
        }
        queue.async { [weak self] in
            let (result, pet) = attempt.run()
            DispatchQueue.main.async {
                MainActor.assumeIsolated { self?.finish(result, pet) }
            }
        }
    }

    private func finish(_ result: Phase, _ pet: NetworkPet?) {
        phase = result
        found = pet
        onChange?()
        if let pet { onOnline?(pet) }
    }

    private func fail(_ reason: String) {
        phase = .failed(reason: reason)
        onChange?()
    }

    /// One provisioning run, off the main actor: ask the pet who it is, give it the Mac's time zone, send the
    /// Wi-Fi line, then wait for that very pet on the network (by its id, else the host it reports, else its name).
    private struct Attempt: Sendable {
        let port: String
        let line: String
        let ssid: String
        let helper: URL
        let environment: [String: String]?
        let timeout: TimeInterval
        let progress: @Sendable (Phase) -> Void

        private struct Identity {
            let device: String
            let id: String
        }

        func run() -> (Phase, NetworkPet?) {
            guard let console = SerialConsole(path: port) else { return (.failed(reason: Message.cannotOpen), nil) }
            let noAnswer = Phase.failed(reason: Message.noAnswer)
            guard let identity = askIdentity(console) else { return (noAnswer, nil) }
            if let rule = PosixTimeZone.rule() { _ = console.send("tz " + rule) }
            guard console.send(line), awaitJoining(console) else { return (noAnswer, nil) }
            progress(.joining(network: ssid))
            return awaitOnline(console, identity: identity)
        }

        // `now` answers with GET /v1 as one JSON line, among log lines.
        private func askIdentity(_ console: SerialConsole) -> Identity? {
            guard console.send("now") else { return nil }
            let deadline = Date().addingTimeInterval(Timing.reply)
            while let line = console.readLine(timeout: deadline.timeIntervalSinceNow) {
                guard let state = try? JSONDecoder().decode(PetStatus.self, from: Data(line.utf8)),
                      let device = state.device
                else { continue }
                return Identity(device: device, id: state.id ?? "")
            }
            return nil
        }

        private func awaitJoining(_ console: SerialConsole) -> Bool {
            let deadline = Date().addingTimeInterval(Timing.reply)
            while let line = console.readLine(timeout: deadline.timeIntervalSinceNow) {
                if ConsoleReply.isJoining(line) { return true }
            }
            return false
        }

        // Online only once the pet itself says so (a Bonjour answer alone may be stale), then found by Bonjour.
        private func awaitOnline(_ console: SerialConsole, identity: Identity) -> (Phase, NetworkPet?) {
            var onlineAt: Date?
            var address = "", host = ""
            var nextAsk = Date().addingTimeInterval(Timing.askAgain), nextLook = Date()
            let deadline = Date().addingTimeInterval(timeout)
            while Date() < deadline {
                if let line = console.readLine(timeout: Timing.poll), let online = ConsoleReply.online(line) {
                    onlineAt = onlineAt ?? Date()
                    (address, host) = online
                }
                if console.isClosed { return (.failed(reason: Message.unplugged), nil) }
                if onlineAt == nil && Date() >= nextAsk {
                    _ = console.send("wifi")
                    nextAsk = Date().addingTimeInterval(Timing.askAgain)
                }
                if onlineAt != nil && Date() >= nextLook {
                    if let pet = match(NetworkPet.scan(helper: helper, environment: environment), identity, host) {
                        return (.online(url: pet.url), pet)
                    }
                    nextLook = Date().addingTimeInterval(Timing.lookAgain)
                }
                if let at = onlineAt, Date().timeIntervalSince(at) > Timing.visibleWithin {
                    return (.failed(reason: "Your pet joined “\(ssid)” (\(address)), but this Mac can't see it. "
                                    + "Is the Mac on the same network? NestBox also needs Local Network access: "
                                    + "System Settings › Privacy & Security › Local Network."), nil)
                }
            }
            return (.failed(reason: "Your pet couldn't join “\(ssid)”. Check the password, and that the network is "
                            + "2.4 GHz (the pet can't use 5 GHz)."), nil)
        }

        private func match(_ pets: [NetworkPet], _ identity: Identity, _ host: String) -> NetworkPet? {
            if !identity.id.isEmpty { return pets.first { $0.id == identity.id } }
            return pets.first { !host.isEmpty && ($0.host == host || $0.url.host == host) }
                ?? pets.first { $0.name == identity.device }
        }
    }
}
