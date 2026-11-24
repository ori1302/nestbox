import Foundation

/// Keeps the Socket Mode connection to Slack open while the user has given the two tokens, and shows a bubble on the
/// pet when a message calls them (SlackCall). Tokens live in the secret store ("app" and "user"); names are asked
/// of Slack once a session.
@MainActor
final class SlackWatch {
    enum State: Equatable {
        case off                // no tokens
        case idle               // nothing ticked: not connected
        case checking           // asking Slack about new tokens
        case connecting
        case connected
        case refused(String)    // Slack turned a token down; what to do, in words
        case unreachable        // no answer; trying again
    }

    /// Who the user token belongs to, from auth.test.
    struct Account: Equatable {
        let userID: String
        let name: String
        let team: String
    }

    private static let pingEvery: TimeInterval = 30
    private static let jumpTime: TimeInterval = 3     // the pet turning to the user and its surprised jump
    private static let silentAfter: TimeInterval = 75    // no frame and no pong for this long: connect again
    /// A "disconnect" this soon after a hello waits its turn like a failure, so a loop of them can't spin.
    private static let shortLived: TimeInterval = 30
    private static let longestWait: TimeInterval = 60
    /// Slack's errors that mean a token is wrong or no longer allowed, so trying again won't help.
    private static let refusals: Set<String> = ["invalid_auth", "not_authed", "token_revoked", "token_expired",
                                                "account_inactive", "missing_scope", "no_permission",
                                                "not_allowed_token_type", "team_access_not_granted",
                                                "ekm_access_denied"]

    let api: URL
    private let secrets: SecretStore
    var onTell: ((SlackTell) -> Void)?
    var onNote: ((String) -> Void)?
    var onChange: (() -> Void)?

    /// What calls the pet. Ticking nothing disconnects.
    var wants = SlackWants() {
        didSet {
            guard wants != oldValue else { return }
            if wants.any != oldValue.any { start() }
            onChange?()
        }
    }
    private(set) var state: State = .off {
        didSet { if state != oldValue { onChange?() } }
    }
    private(set) var account: Account?
    /// Connections Slack has for this app, this one included, as its last hello said.
    private(set) var connections = 0
    /// Two hellos in a row counted more than one connection: another copy of the app (another Mac) shares the
    /// messages. One alone may count a connection a sleeping Mac could not close.
    private(set) var shared = false
    private(set) var dnd: SlackDND? {
        didSet { if dnd != oldValue { onChange?() } }
    }

    private var task: URLSessionWebSocketTask?
    private var generation = 0
    private var pinger: Timer?
    private var retry: Timer?
    private var wait: TimeInterval = 2
    private var limited = false             // Slack asked to slow down: waits are not cut short
    private var heard = Date()
    private var helloAt = Date.distantPast
    private var sayHello = false
    private let session = URLSession(configuration: .ephemeral)

    /// A bubble on the pet now, by conversation (or thread): how many messages it counts, since and until when.
    private var showing: [String: (count: Int, since: Date, until: Date)] = [:]
    private var names: [String: String] = [:]      // user id: name
    private var places: [String: String] = [:]     // channel id: "#name"
    private var myThreads: [String] = []           // "channel/thread_ts" the user wrote in, newest last
    private var seen: [String] = []                // event ids, against Slack sending one again

    init(api: URL, secrets: SecretStore) {
        self.api = api
        self.secrets = secrets
    }

    /// The tokens, read once and kept here, so menus refreshing don't read the file each time.
    private lazy var tokens: (app: String, user: String)? = {
        guard let app = secrets.get("app"), let user = secrets.get("user") else { return nil }
        return (app, user)
    }()

    var hasTokens: Bool { tokens != nil }
    /// Until when Slack's Do Not Disturb keeps the pet quiet, or nil.
    var quietUntil: Date? { dnd?.quietUntil(Date()) }

    // MARK: - Setting up

    /// Checks the tokens with Slack and, when both work, saves them and connects. An empty token keeps the saved
    /// one. `done` gets nil, or what is wrong in words.
    func connect(appToken: String, userToken: String, done: @escaping @MainActor (String?) -> Void) {
        let app = appToken.trimmingCharacters(in: .whitespacesAndNewlines).nonEmpty ?? tokens?.app ?? ""
        let user = userToken.trimmingCharacters(in: .whitespacesAndNewlines).nonEmpty ?? tokens?.user ?? ""
        if let problem = Slack.problem(appToken: app) ?? Slack.problem(userToken: user) { return done(problem) }
        stop()
        state = .checking
        let mine = generation
        let interrupted = "The connection changed while checking. Click Connect again."
        Task {
            let who = await call("auth.test", token: user)
            guard mine == generation else { return done(interrupted) }
            guard who.ok, let me = who.decode(AuthTest.self) else {
                start()
                return done(Self.words(who.error, token: .user))
            }
            let link = await call("apps.connections.open", token: app)
            guard mine == generation else { return done(interrupted) }
            guard link.ok, let url = link.decode(Link.self).flatMap({ URL(string: $0.url) }) else {
                start()
                return done(Self.words(link.error, token: .app))
            }
            guard secrets.set("app", app), secrets.set("user", user) else {
                secrets.set("app", nil)
                secrets.set("user", nil)
                tokens = nil
                start()
                return done("The tokens couldn't be saved on this Mac: check there is space on the disk, and try "
                            + "again.")
            }
            tokens = (app, user)
            if account?.userID != me.userID {
                names = [:]
                places = [:]
                myThreads = []
            }
            account = Account(userID: me.userID, name: me.user, team: me.team)
            onNote?("slack: the tokens work, for \(me.user) in \(me.team)")
            sayHello = true
            guard wants.any else {
                state = .idle
                return done(nil)
            }
            state = .connecting
            await learnDND(user, mine)
            guard mine == generation else { return done(nil) }    // saved; whatever came since connects with them
            socket(url, mine)
            done(nil)
        }
    }

    /// Closes the socket and forgets the tokens.
    func disconnect() {
        stop()
        showing = [:]
        secrets.set("app", nil)
        secrets.set("user", nil)
        tokens = nil
        account = nil
        shared = false
        dnd = nil
        names = [:]
        places = [:]
        myThreads = []
        state = .off
        onNote?("slack: disconnected; the tokens are forgotten on this Mac")
    }

    // MARK: - The connection

    /// Connects with the saved tokens, unless there are none or nothing is ticked.
    func start() {
        stop()
        guard let tokens else {
            state = .off
            return
        }
        let (app, user) = tokens
        guard wants.any else {
            state = .idle
            return
        }
        state = .connecting
        let mine = generation
        Task {
            let who = await call("auth.test", token: user)
            guard mine == generation else { return }
            guard who.ok, let me = who.decode(AuthTest.self) else { return failed(who, .user) }
            account = Account(userID: me.userID, name: me.user, team: me.team)
            await learnDND(user, mine)
            let link = await call("apps.connections.open", token: app)
            guard mine == generation else { return }
            guard link.ok, let url = link.decode(Link.self).flatMap({ URL(string: $0.url) }) else {
                return failed(link, .app)
            }
            socket(url, mine)
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

    /// The Mac woke or changed network: connect again at once.
    func reachAgain() {
        guard [.connected, .connecting, .unreachable].contains(state) else { return }
        if !limited { wait = 2 }
        start()
    }

    private func socket(_ url: URL, _ mine: Int) {
        let task = session.webSocketTask(with: url)
        self.task = task
        task.resume()
        heard = Date()
        receive(task, mine)
        pinger = Timer(timeInterval: Self.pingEvery, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, mine == self.generation else { return }
                if Date().timeIntervalSince(self.heard) > Self.silentAfter { return self.again("Slack went quiet") }
                if self.state == .connected { self.wait = 2 }    // up for a while: the next failure starts over
                task.sendPing { error in
                    Task { @MainActor [weak self] in
                        guard let self, mine == self.generation else { return }
                        if error == nil { self.heard = Date() } else { self.again("Slack stopped answering") }
                    }
                }
            }
        }
        RunLoop.main.add(pinger!, forMode: .common)
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
                again("the connection closed")
            }
        }
    }

    private func take(_ text: String) {
        guard let envelope = try? JSONDecoder().decode(SlackEnvelope.self, from: Data(text.utf8)) else { return }
        if let id = envelope.envelopeID, let ack = try? JSONEncoder().encode(["envelope_id": id]) {
            task?.send(.string(String(decoding: ack, as: UTF8.self))) { _ in }
        }
        switch envelope.type {
        case "hello":
            state = .connected
            limited = false
            helloAt = Date()
            let count = envelope.numConnections ?? 1
            shared = count > 1 && connections > 1
            connections = count
            onNote?("slack: connected" + (count > 1 ? "; Slack counts \(count) connections for this app" : ""))
            onChange?()
            if sayHello {
                sayHello = false
                onTell?(.hello)
            }
        case "disconnect":
            if envelope.reason == "link_disabled" {
                give("Socket Mode is off for the app in Slack: turn it on under Socket Mode")
            } else {
                let reason = envelope.reason ?? "no reason"
                if Date().timeIntervalSince(helloAt) < Self.shortLived {
                    again("Slack asked for a new connection again (\(reason))")
                } else {
                    onNote?("slack: Slack asked for a new connection (\(reason))")
                    start()
                }
            }
        case "events_api":
            guard let event = envelope.payload?.event else { return }
            if let id = envelope.payload?.eventID {
                guard !seen.contains(id) else { return }
                seen.append(id)
                if seen.count > 200 { seen.removeFirst(100) }
            }
            take(event)
        default:
            break
        }
    }

    private func take(_ event: SlackEvent) {
        if event.type == "dnd_updated" {
            dnd = event.dnd
            onNote?("slack: Do Not Disturb " + (quietUntil.map { "until " + Self.clock($0) } ?? "off"))
            return
        }
        guard let me = account?.userID, event.isWritten, let channel = event.channel else { return }
        if event.user == me {
            if event.isReply, let thread = event.threadTS { remember(thread: channel + "/" + thread) }
            return
        }
        guard let call = SlackCall.of(event, me: me, myThreads: Set(myThreads), wants: wants) else { return }
        if let until = quietUntil {
            onNote?("slack: not shown, Do Not Disturb until " + Self.clock(until))
            return
        }
        let direct = call == .directMessage || call == .groupMessage
        let id = !direct && event.isReply ? channel + "-" + (event.threadTS ?? "") : channel
        Task {
            if dnd?.isStale(Date()) == true, let token = tokens?.user { await learnDND(token, generation) }
            if let until = quietUntil {
                onNote?("slack: not shown, Do Not Disturb until " + Self.clock(until))
                return
            }
            let who = await name(of: event)
            let place = direct ? nil : await place(of: channel, type: event.channelType)
            // Disconnected, unticked or Do Not Disturb while the names came: no bubble after all.
            guard account?.userID == me, SlackCall.of(event, me: me, myThreads: Set(myThreads), wants: wants) == call,
                  quietUntil == nil else { return }
            let now = Date()
            showing = showing.filter { $0.value.until > now }
            let count = (showing[id]?.count ?? 0) + 1
            let since = showing[id]?.since ?? now
            showing[id] = (count, since, now.addingTimeInterval(TimeInterval(SlackTell.seconds)))
            // A tell replaces the one before it, so the count waits for the first message's jump to finish; then only
            // the latest count goes.
            let wait = since.addingTimeInterval(Self.jumpTime).timeIntervalSince(now)
            if count > 1, wait > 0 {
                try? await Task.sleep(for: .seconds(wait))
                guard showing[id]?.count == count, account?.userID == me, quietUntil == nil else { return }
            }
            onTell?(.call(call, id: id, who: who, place: place, count: count))
        }
    }

    private func remember(thread: String) {
        myThreads.removeAll { $0 == thread }
        myThreads.append(thread)
        if myThreads.count > 300 { myThreads.removeFirst(100) }
    }

    // MARK: - Names

    private func name(of event: SlackEvent) async -> String {
        guard let id = event.user else { return event.botName ?? event.username ?? "An app" }
        if let known = names[id] { return known }
        guard let token = tokens?.user else { return "Someone" }
        guard let user = await call("users.info", token: token, ["user": id]).decode(UserInfo.self)?.user else {
            return event.botName ?? "Someone"
        }
        let name = [user.profile?.displayName, user.profile?.realName, user.realName, user.name]
            .compactMap { $0?.nonEmpty }.first ?? "Someone"
        names[id] = name
        return name
    }

    private func place(of channel: String, type: String?) async -> String? {
        if type == "im" { return "a DM" }
        if type == "mpim" { return "a group DM" }
        if let known = places[channel] { return known }
        guard let token = tokens?.user,
              let name = await call("conversations.info", token: token, ["channel": channel])
                .decode(Conversation.self)?.channel.name?.nonEmpty else { return nil }
        places[channel] = "#" + name
        return "#" + name
    }

    private func learnDND(_ token: String, _ mine: Int) async {
        let answer = await call("dnd.info", token: token)
        guard mine == generation, answer.ok else { return }
        dnd = answer.decode(SlackDND.self)
    }

    // MARK: - Slack's Web API

    private struct Answer {
        var ok = false
        var error: String?
        var data = Data()
        var retryAfter: TimeInterval?

        func decode<T: Decodable>(_ type: T.Type) -> T? { ok ? try? JSONDecoder().decode(type, from: data) : nil }
    }

    private struct Reply: Decodable {
        let ok: Bool
        let error: String?
    }

    private struct AuthTest: Decodable {
        let userID: String
        let user: String
        let team: String

        private enum CodingKeys: String, CodingKey {
            case userID = "user_id", user, team
        }
    }

    private struct Link: Decodable {
        let url: String
    }

    private struct UserInfo: Decodable {
        struct User: Decodable {
            struct Profile: Decodable {
                let displayName: String?
                let realName: String?

                private enum CodingKeys: String, CodingKey {
                    case displayName = "display_name", realName = "real_name"
                }
            }
            let name: String?
            let realName: String?
            let profile: Profile?

            private enum CodingKeys: String, CodingKey {
                case name, profile
                case realName = "real_name"
            }
        }
        let user: User
    }

    private struct Conversation: Decodable {
        struct Channel: Decodable {
            let name: String?
        }
        let channel: Channel
    }

    /// A Web API method, its arguments form-encoded, with a token. "no_answer" and "ratelimited" stand for no reply
    /// and HTTP 429.
    private func call(_ method: String, token: String, _ arguments: [String: String] = [:]) async -> Answer {
        var request = URLRequest(url: api.appendingPathComponent(method))
        request.httpMethod = "POST"
        request.timeoutInterval = 15
        request.setValue("Bearer " + token, forHTTPHeaderField: "Authorization")
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        let allowed = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: "-._~"))
        request.httpBody = Data(arguments.map {
            "\($0.key)=\($0.value.addingPercentEncoding(withAllowedCharacters: allowed) ?? "")"
        }.joined(separator: "&").utf8)
        guard let (data, response) = try? await session.data(for: request) else { return Answer(error: "no_answer") }
        if let http = response as? HTTPURLResponse, http.statusCode == 429 {
            let after = http.value(forHTTPHeaderField: "Retry-After").flatMap(TimeInterval.init)
            return Answer(error: "ratelimited", retryAfter: after ?? 60)
        }
        guard let reply = try? JSONDecoder().decode(Reply.self, from: data) else { return Answer(error: "no_answer") }
        return Answer(ok: reply.ok, error: reply.error, data: data)
    }

    // MARK: - When it goes wrong

    private enum Token { case app, user }

    /// What to do about Slack's error for a token, in words.
    private static func words(_ error: String?, token: Token) -> String {
        switch (error, token) {
        case ("no_answer", _), (nil, _):
            return "Slack didn't answer. Check your network and try again."
        case ("ratelimited", _):
            return "Slack asked to slow down. Try again in a minute."
        case ("token_revoked", .user), ("token_expired", .user):
            return "That user token no longer works. Install the app again (Install App) and copy the new one."
        case ("missing_scope", _), ("no_permission", _):
            return "The app is missing a permission. Create it again with Create App in Slack, which fills them in."
        case ("account_inactive", _):
            return "Slack says this account is deactivated."
        case (_, .user):
            return "Slack didn't accept the User OAuth Token. Copy it again from Install App."
        case (_, .app):
            return "Slack didn't accept the app-level token. Make one under Basic Information › App-Level Tokens, "
                + "with the scope connections:write."
        }
    }

    private func failed(_ answer: Answer, _ token: Token) {
        if let error = answer.error, Self.refusals.contains(error) {
            give(Self.words(error, token: token))
        } else {
            if let after = answer.retryAfter {
                limited = true
                wait = max(wait, min(after, 10 * 60))
            }
            again("Slack didn't answer (\(answer.error ?? "no reply"))")
        }
    }

    /// Stops trying until the user does something.
    private func give(_ words: String) {
        stop()
        state = .refused(words)
        onNote?("slack: " + words)
    }

    /// Tries again after a growing wait (2 s up to a minute).
    private func again(_ note: String) {
        stop()
        state = .unreachable
        onNote?("slack: \(note); trying again in \(Int(wait)) s")
        let mine = generation
        retry = Timer(timeInterval: wait, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, mine == self.generation else { return }
                self.start()
            }
        }
        RunLoop.main.add(retry!, forMode: .common)
        if !limited { wait = min(wait * 2, Self.longestWait) }
    }

    // MARK: - Small things

    static func clock(_ date: Date) -> String {
        let format = DateFormatter()
        format.dateStyle = Calendar.current.isDateInToday(date) ? .none : .short
        format.timeStyle = .short
        return format.string(from: date)
    }
}

private extension String {
    var nonEmpty: String? { isEmpty ? nil : self }
}
