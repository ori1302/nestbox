import AppKit
import Security

/// Drives the app's model, menu and windows against a pet started for the test: the bundled helper, GET /v1
/// and /v1/pets, switching pets, the companion's start, signals and restart after a crash, a quick test, the
/// page for coding agents, Bonjour discovery and pairing (with stand-in pets announced by dns-sd), Wi-Fi over USB
/// against a fake console, the pet window, and quitting.
///
/// It uses its own preferences suite and a temporary HOME for every helper it starts. After init it runs on a
/// thread of its own and reaches the app's main-actor objects through onMain().
final class SelfTest: @unchecked Sendable {
    private static let suite = "pet.nestbox.menubar.selftest"
    private static let pairingSuite = "pet.nestbox.menubar.selftest.pair"

    private let app: URL
    private let device: String
    private let bonjourName: String?
    private let shots: String?
    private let simulatorArguments: [String]
    private let tmp: URL
    private let defaults: UserDefaults
    private let model: Model
    private let fakeSlack: FakeSlack?
    private static let slackSecrets = "pet.nestbox.slack.selftest"
    private var failed = 0

    /// `shots`: a directory to also draw the windows into, as PNGs.
    @MainActor
    init(app: URL, paths: AppPaths, device: String, name: String?, simulatorArguments: [String], shots: String?) {
        self.app = app
        self.device = device
        bonjourName = name
        self.shots = shots
        self.simulatorArguments = simulatorArguments
        tmp = FileManager.default.temporaryDirectory.appendingPathComponent("nestbox-selftest-\(getpid())")
        UserDefaults().removePersistentDomain(forName: Self.suite)
        defaults = Self.suiteDefaults(Self.suite)
        // The relay's secrets and Slack's tokens go to a secrets folder and services of the test's own, never the
        // user's; the model reads both at its start. Slack is a stand-in.
        setenv("NESTBOX_SECRETS_DIR", tmp.appendingPathComponent("secrets").path, 1)
        if ProcessInfo.processInfo.environment["NESTBOX_RELAY_SECRETS"] == nil {
            setenv("NESTBOX_RELAY_SECRETS", "pet.nestbox.relay.selftest", 1)
        }
        fakeSlack = FakeSlack()
        if let fake = fakeSlack { setenv("NESTBOX_SLACK_API", fake.api.absoluteString, 1) }
        setenv("NESTBOX_SLACK_SECRETS", Self.slackSecrets, 1)
        for account in ["app", "user"] { SecretStore(service: Self.slackSecrets).set(account, nil) }
        model = Model(prefs: Preferences(defaults: defaults), paths: paths, device: URL(string: device), name: nil,
                      simulatorArguments: simulatorArguments, childrenFile: nil,
                      childHome: Self.makeHome(tmp.appendingPathComponent("home")))
    }

    private var bundledHelper: URL { app.appendingPathComponent(AppPaths.bundledHelper) }

    // MARK: - Harness

    /// The test's own preferences; never the app's.
    private static func suiteDefaults(_ name: String) -> UserDefaults {
        guard let defaults = UserDefaults(suiteName: name) else { preconditionFailure("no defaults suite \(name)") }
        return defaults
    }

    private static func makeHome(_ url: URL) -> String {
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url.path
    }

    private func helperEnvironment(home: String) -> [String: String] {
        ProcessInfo.processInfo.environment.merging(["HOME": home]) { $1 }
    }

    private func check(_ ok: Bool, _ what: String) {
        print((ok ? "PASS  " : "FAIL  ") + what)
        if !ok { failed += 1 }
    }

    private func onMain<T: Sendable>(_ work: @MainActor () -> T) -> T {
        DispatchQueue.main.sync { MainActor.assumeIsolated(work) }
    }

    /// Like `wait`, but the condition runs on this background thread (it may use onMain and the network).
    private func until(_ seconds: Double, _ condition: () -> Bool) -> Bool {
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline {
            if condition() { return true }
            usleep(100_000)
        }
        return condition()
    }

    private func wait(_ seconds: Double, _ condition: @MainActor () -> Bool) -> Bool {
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline {
            if onMain(condition) { return true }
            usleep(100_000)
        }
        return onMain(condition)
    }

    @MainActor
    private func failed(_ setup: WiFiSetup, with text: String) -> Bool {
        if case .failed(let reason) = setup.phase { return reason.contains(text) }
        return false
    }

    private func describe(_ pet: NetworkPet?) -> String {
        pet.map { "\($0.name) id=\($0.id) host=\($0.host)" } ?? ""
    }

    @MainActor
    private func lastSent(_ model: Model, after count: Int) -> String? {
        model.log.lines.dropFirst(count).last { $0.contains(" -> 200") }
    }

    @discardableResult
    private func run(_ executable: String, _ arguments: [String]) -> (Int32, String) {
        guard let run = runProcess(URL(fileURLWithPath: executable), arguments, mergeErrors: true) else {
            return (-1, "")
        }
        return (run.status, run.output)
    }

    /// Announces a stand-in pet on Bonjour with these TXT entries, at the test pet's port, until terminated.
    private func announce(_ name: String, _ txt: [String]) -> Process {
        let port = URL(string: device)?.port ?? 8099
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/dns-sd")
        process.arguments = ["-R", name, "_nestbox._tcp", "local", String(port)] + txt
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try? process.run()
        usleep(800_000)
        return process
    }

    // MARK: - The run

    /// Runs on a background thread while the main queue serves the model.
    func run() -> Int32 {
        bundle()
        petState()
        companion()
        menu()
        signalsAndRestart()
        musicSignal()
        calendarPlan()
        webhookChecks()
        relayEndToEnd()
        slackChecks()
        secretChecks()
        slackEndToEnd()
        quickTest()
        teaching()
        bonjour()
        pairing()
        wifiOverUSB()
        petWindow()
        quit()
        UserDefaults().removePersistentDomain(forName: Self.suite)
        try? FileManager.default.removeItem(at: tmp)
        print(failed == 0 ? "selftest: all passed" : "selftest: \(failed) failed")
        return failed == 0 ? 0 : 1
    }

    private func bundle() {
        let helper = model.paths.helper
        check(FileManager.default.isExecutableFile(atPath: helper.path), "companion binary at \(helper.path)")
        check(helper.standardizedFileURL == bundledHelper.standardizedFileURL,
              "the companion used is the one inside the app")
        check(run("/usr/bin/codesign", ["--verify", "--deep", "--strict", app.path]).0 == 0,
              "the app's signature holds, the companion's included")
        let archs = run("/usr/bin/lipo", ["-archs", bundledHelper.path]).1
            .trimmingCharacters(in: .whitespacesAndNewlines)
        check(archs.contains("arm64") && archs.contains("x86_64"),
              "the companion runs on Apple Silicon and Intel (\(archs))")
        print("      pet window (developer extra): \(model.paths.simulator?.path ?? "not on this Mac: hidden")")
    }

    private func petState() {
        let m = model
        onMain { m.pollStatus() }
        check(wait(5) { m.status != nil }, "GET /v1 answers")
        let status = onMain { m.status }
        check(status?.pet != nil && status?.doing != nil && status?.why != nil,
              "GET /v1 has the pet, what it does, why: "
                  + "\(status?.pet ?? "-") / \(status?.doing ?? "-") / \(status?.why ?? "-")")

        let fetched = Box(false)
        onMain { m.fetchPets { fetched.value = true } }
        check(wait(5) { fetched.value } && onMain { !m.pets.isEmpty && m.petOut != nil },
              "GET /v1/pets lists \(onMain { m.pets.map(\.id) }), current \(onMain { m.petOut ?? "-" })")

        let pets = onMain { m.pets }, first = onMain { m.petOut ?? "" }
        guard let other = pets.first(where: { $0.id != first }) else {
            print("SKIP  switch pet: the pet holds only one")
            return
        }
        let answer = Box(0)
        onMain { m.bringOut(other.id) { answer.value = $0 } }
        _ = wait(5) { answer.value != 0 }
        fetched.value = false
        onMain { m.fetchPets { fetched.value = true } }
        check(answer.value == 200 && wait(5) { fetched.value } && onMain { m.petOut } == other.id,
              "switch pet to \(other.id) (POST {\"pet\"} -> \(answer.value))")
        check(wait(4) { m.status?.pet == other.id }, "GET /v1 shows \(other.id)")
        answer.value = 0
        onMain { m.bringOut(first) { answer.value = $0 } }
        check(wait(5) { answer.value == 200 }, "switch back to \(first)")
    }

    private func companion() {
        let m = model
        onMain { m.begin() }
        check(wait(8) { m.companion.isRunning && lastSent(m, after: 0) != nil }, "companion started and posts 200s")
        let pid = onMain { m.companion.process?.pid } ?? 0
        check(kill(pid, 0) == 0 && onMain { ProcessTable.pids(runningExecutableAt: bundledHelper.path) }.contains(pid),
              "companion process \(pid) is running, from inside the app")
        let line = onMain { lastSent(m, after: 0) } ?? ""
        for field in ["\"from\"", "\"keys\": ", "\"idle\"", "\"state\"", "\"time\"", "\"next\""] {
            check(line.contains(field), "sends \(field.replacingOccurrences(of: ": ", with: ""))")
        }
        let saysConnected = { @MainActor in StatusText.lines(for: m).contains { $0.contains("Companion: connected") } }
        check(wait(4) { m.companion.isConnected && saysConnected() },
              "status says connected: \(onMain { StatusText.lines(for: m) })")
    }

    private func menu() {
        let m = model
        let fetched = Box(false)
        onMain { m.fetchPets { fetched.value = true } }
        _ = wait(5) { fetched.value }
        let text = onMain { menuText(MenuBar(model: m).menu) }
        print(text.split(separator: "\n").map { "      | " + $0 }.joined(separator: "\n"))
        let out = onMain { m.pets.first { $0.id == m.petOut }?.name ?? "?" }   // whichever pet is out
        check(text.contains("Developer") && text.contains("Show Pet Window") && text.contains("✓ \(out)")
              && text.contains("✓ Typing rate")
              && text.contains("Say hello") && text.contains("Companion: connected"),
              "the menu shows status, pets with the current one checked, signals, quick tests")
        guard let dir = shots else { return }
        let ok = onMain { () -> Bool in
            let wifi = WiFiSetup(helper: m.paths.helper)
            return savePNG(snapshot(SetupWindow(model: m, wifi: wifi), wifi: wifi), to: dir, as: "setup.png")
                && savePNG(snapshot(SendsWindow(model: m)), to: dir, as: "sends.png")
        }
        check(ok, "drew the setup and what-it-sends windows into \(dir)")
    }

    private func signalsAndRestart() {
        let m = model
        let pid1 = onMain { m.companion.process?.pid } ?? 0
        var mark = onMain { m.log.lines.count }
        onMain {
            m.setSignal(.idle, enabled: false)
            m.setSignal(.keys, enabled: false)
        }
        let pid2 = onMain { m.companion.process?.pid } ?? 0
        check(pid2 != pid1 && wait(3) { kill(pid1, 0) != 0 },
              "switching signals restarts the companion (\(pid1) -> \(pid2))")
        let arguments = onMain { m.companion.process?.arguments ?? [] }
        check(arguments.contains("--no-idle") && arguments.contains("--no-keys"),
              "restarted with --no-idle --no-keys")
        check(wait(6) { lastSent(m, after: mark) != nil }, "posts again after the restart")
        let off = onMain { lastSent(m, after: mark) } ?? ""
        check(!off.contains("\"idle\"") && !off.contains("\"keys\""),
              "idle off: no idle; typing off: no keys  [\(off.prefix(110))]")
        check(onMain { !m.prefs.isEnabled(.idle) && !m.prefs.isEnabled(.keys) && m.prefs.isEnabled(.lock) },
              "signal switches are kept in preferences")

        mark = onMain { m.log.lines.count }
        kill(pid2, SIGKILL)
        let restarted = { @MainActor in (m.companion.process?.pid ?? pid2) != pid2 && m.companion.isRunning }
        check(wait(10) { restarted() && lastSent(m, after: mark) != nil },
              "companion killed -> restarted (\(pid2) -> \(onMain { m.companion.process?.pid } ?? 0)) and posting")
    }

    // Music starts off; on, the companion is started with --music; off again, the song's note is ended at once.
    private func musicSignal() {
        let m = model
        check(onMain { !m.prefs.isEnabled(.music) && !(m.companion.process?.arguments ?? []).contains("--music") },
              "music is off to begin with: no --music")
        onMain { m.setSignal(.music, enabled: true) }
        check(onMain { (m.companion.process?.arguments ?? []).contains("--music") }, "music on: restarted with --music")
        let mark = onMain { m.log.lines.count }
        onMain { m.setSignal(.music, enabled: false) }
        let over = { @MainActor in m.log.lines.dropFirst(mark).contains { $0.hasPrefix("-- music over: ") } }
        check(onMain { !(m.companion.process?.arguments ?? []).contains("--music") } && wait(5) { over() },
              "music off: restarted without --music, and told the pet the music is over  "
              + "[\(onMain { m.log.lines.dropFirst(mark).last { $0.hasPrefix("-- music over: ") } } ?? "-")]")
    }

    // What the calendar tells: generic words and countdowns, from times alone.
    private func calendarPlan() {
        let now = Date(timeIntervalSince1970: 1_800_000_000)
        func at(_ minutes: Double) -> Date { now.addingTimeInterval(minutes * 60) }
        let meeting = CalendarEvent(start: at(-10), end: at(20))
        var tell = CalendarTell.at(now, events: [meeting])
        check(tell.state == "quiet" && tell.doing == "watch" && tell.line == "in a meeting" && tell.gauge == "timer"
                  && tell.seconds == 20 * 60,
              "in a meeting: quiet, watching its desk, \"in a meeting\", a countdown to its end")
        let justStarted = CalendarTell.at(now, events: [CalendarEvent(start: now.addingTimeInterval(-20), end: at(30))])
        check(justStarted.starts(after: CalendarTell.at(at(-1), events: []), at: now) && !tell.starts(after: nil, at: now)
                  && !justStarted.starts(after: justStarted, at: now),
              "\"Meeting time\" once, for a meeting that has just started, not one ten minutes in or one already told")
        tell = CalendarTell.at(now, events: [meeting, CalendarEvent(start: at(-5), end: at(45))])
        check(tell.state == "quiet" && tell.seconds == 45 * 60, "overlapping meetings: quiet until the last one ends")
        tell = CalendarTell.at(now, events: [CalendarEvent(start: at(3), end: at(33))])
        check(tell.state == "on" && tell.line == "meeting soon" && tell.seconds == 3 * 60,
              "3 minutes before one: \"meeting soon\", a countdown to its start")
        check(CalendarTell.at(now, events: [CalendarEvent(start: at(6), end: at(30))]) == .over,
              "6 minutes before: nothing yet")
        let skipped = [CalendarEvent(start: at(-5), end: at(5), allDay: true),
                       CalendarEvent(start: at(-5), end: at(5), free: true),
                       CalendarEvent(start: at(-5), end: at(5), declined: true),
                       CalendarEvent(start: at(-5), end: at(5), canceled: true)]
        check(CalendarTell.at(now, events: skipped) == .over, "all-day, free, declined and canceled events are not meetings")
        check(CalendarTell.nextChange(after: now, events: [CalendarEvent(start: at(3), end: at(33))]) == at(3),
              "the next change is the meeting's start, so the countdown hands over to the quiet with no gap")
        check(CalendarTell.nextChange(after: now, events: [meeting, CalendarEvent(start: at(8), end: at(30))]) == at(3),
              "or a heads-up starting, whichever comes first")
        let body = (try? JSONEncoder().encode(CalendarTell.at(now, events: [meeting])))
            .flatMap { String(data: $0, encoding: .utf8) } ?? ""
        check(body.contains("\"for\":1200") && !body.contains("until"), "the tell says \"for\" in seconds  [\(body)]")
    }

    // The webhook bridge's own checks: the URL's id as the relay computes it, and what a relayed webhook must carry.
    private func webhookChecks() {
        check(Webhook.id(of: "abc") == "ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0",
              "a mailbox's id is base64url(sha256(key)), as the relay computes it")
        let key = Webhook.randomKey()
        check(key.count == 43 && key != Webhook.randomKey(), "keys are 32 random bytes")
        func frame(_ secret: String?, _ body: String) -> String {
            let headers = secret.map { ["x-nestbox-secret": $0] } ?? [:]
            let json = try? JSONSerialization.data(withJSONObject: ["headers": headers, "body": body])
            return json.map { String(decoding: $0, as: UTF8.self) } ?? ""
        }
        let tell = #"{"from":"ci","state":"done"}"#
        check(Webhook.tell(in: frame("s3cret", tell), secret: "s3cret") == Data(tell.utf8),
              "a webhook with the secret: its tell, as it came")
        check(Webhook.tell(in: frame("wrong", tell), secret: "s3cret") == nil
                  && Webhook.tell(in: frame(nil, tell), secret: "s3cret") == nil
                  && Webhook.tell(in: frame("s3cre", tell), secret: "s3cret") == nil,
              "a wrong, missing or shorter secret: dropped")
        check(Webhook.tell(in: frame("s3cret", "[1,2]"), secret: "s3cret") == nil
                  && Webhook.tell(in: frame("s3cret", "not json"), secret: "s3cret") == nil
                  && Webhook.tell(in: "garbage", secret: "s3cret") == nil,
              "a body that is not a JSON object, or a frame that is not the relay's: dropped")
    }

    // Through a real relay (the relay run locally with `wrangler dev`), when NESTBOX_RELAY and
    // NESTBOX_RELAY_TEST_KEY are set, and the pet under test was started with NESTBOX_RELAY too. First the usual way:
    // the pet gets its keys and holds the socket itself (a URL the bridge made before is handed to it, a webhook,
    // a wrong secret, pausing, removing). Then Developer › Webhooks Through This Mac at the same time, on a mailbox of
    // its own, so neither takes the other's socket. The test secrets (NESTBOX_RELAY_SECRETS) are emptied at
    // the end.
    private func relayEndToEnd() {
        let env = ProcessInfo.processInfo.environment
        guard let key = env["NESTBOX_RELAY_TEST_KEY"], env["NESTBOX_RELAY"] != nil,
              let service = env["NESTBOX_RELAY_SECRETS"] else {
            print("skip  webhooks through a relay (no NESTBOX_RELAY, NESTBOX_RELAY_TEST_KEY, NESTBOX_RELAY_SECRETS)")
            return
        }
        let m = model
        let secrets = SecretStore(service: service)
        let petPage = URL(string: device)!.appendingPathComponent("v1")
        let relay = { self.onMain { () -> RelayStatus? in
            if case .pet(let r) = m.webhookState { return r }
            return nil
        } }
        guard until(5, { onMain { m.status?.relay != nil } }) else {
            print("skip  webhooks through the pet (the pet under test has no relay link: start it with NESTBOX_RELAY)")
            return
        }

        // A mailbox the bridge made on an older app: the pet takes it over, URL and secret unchanged.
        let oldMailbox = Webhook.randomKey(), oldSecret = Webhook.randomKey()
        _ = onMain { secrets.set("mailbox", oldMailbox) }
        _ = onMain { secrets.set("secret", oldSecret) }
        onMain { m.setWebhooksAccessKey(key) }
        check(until(15) { relay()?.state == "connected" }, "the pet gets its keys and connects to the relay itself")
        check(onMain { m.petWebhooks.secret == oldSecret && m.petWebhooks.url?.lastPathComponent == Webhook.id(of: oldMailbox) }
                  && secrets.get("mailbox") == nil && onMain { !m.prefs.webhooksThroughMac },
              "the bridge's old webhook URL and secret moved to the pet, and the bridge is off")
        check(!get(petPage).contains(oldMailbox) && !get(petPage).contains(key), "GET /v1 shows neither key")
        guard let url = onMain({ m.petWebhooks.url }), let secret = onMain({ m.petWebhooks.secret }) else { return }
        let sent = post(url, secret: secret, #"{"from":"relaytest","state":"working","line":"from the internet"}"#)
        check(sent == 202 && until(5) { get(petPage).contains("from the internet") },
              "a webhook with the secret: 202 from the relay, and the pet shows it")
        let mark = onMain { m.log.lines.count }
        check(post(url, secret: "not-the-secret", #"{"from":"intruder","state":"working"}"#) == 202
                  && !until(2) { get(petPage).contains("intruder") }
                  && !onMain({ m.log.lines.dropFirst(mark).contains { $0.contains("(webhook)") } }),
              "a wrong secret is dropped by the pet; nothing passes through this Mac")
        onMain { m.setReceivingWebhooks(false) }
        check(until(5) { relay()?.on == false } && until(5) {
            post(url, secret: secret, #"{"from":"relaytest","state":"over"}"#) == 404
        }, "Receive Webhooks off: the pet lets go, and webhooks get 404")
        onMain { m.setReceivingWebhooks(true) }
        check(until(15) { relay()?.state == "connected"
                      && post(url, secret: secret, #"{"from":"relaytest","state":"over"}"#) == 202 },
              "Receive Webhooks on: the pet connects again, and the same URL works")

        // The bridge beside it, on a mailbox of its own.
        onMain { m.setWebhooksThroughMac(true) }
        check(wait(10) { m.webhooks.state == .connected } && onMain { m.webhooks.url != url },
              "Webhooks Through This Mac: connected, with a URL of its own")
        if let bridgeURL = onMain({ m.webhooks.url }), let bridgeSecret = onMain({ m.webhooks.secret }) {
            let mark = onMain { m.log.lines.count }
            check(post(bridgeURL, secret: bridgeSecret, #"{"from":"bridgetest","say":"via the Mac"}"#) == 202
                      && wait(5) { m.log.lines.dropFirst(mark).contains { $0.contains("bridgetest") && $0.hasSuffix("-> 200 (webhook)") } },
                  "a webhook to the Mac's URL goes through the Mac to the pet")
        }
        let petConnected = { self.get(petPage).contains(#""relay": {"state": "connected""#) }
        check(petConnected() && !until(4) { !petConnected() } && onMain { m.webhooks.state == .connected },
              "the pet and the Mac stay connected side by side: no collision")
        onMain { m.setWebhooksThroughMac(false) }
        check(wait(5) { m.webhooks.state == .paused }, "Webhooks Through This Mac off")
        onMain { m.webhooks.removeFromThisMac { _ in } }
        _ = until(5) { secrets.get("mailbox") == nil }

        let removed = Box<Int?>(nil), answered = Box(false)
        _ = onMain { secrets.set("access", key) }
        onMain {
            m.petWebhooks.remove(pet: URL(string: self.device), keepAccessKey: false) { status in
                removed.value = status
                answered.value = true
            }
        }
        check(until(10) { answered.value } && removed.value == 204 && until(5) { onMain { m.status?.relay?.set == false } }
                  && onMain { !m.petWebhooks.hasAccessKey && m.petWebhooks.url == nil },
              "Remove Webhooks: the relay deleted the mailbox (\(removed.value.map(String.init) ?? "-")), the pet "
                  + "forgot its keys, and nothing is kept here")
        check(post(url, secret: secret, #"{"from":"relaytest","state":"over"}"#) == 404, "and its URL is gone (404)")
        for account in ["access", "mailbox", "secret", "pet-mailbox", "pet-secret"] {
            _ = onMain { secrets.set(account, nil) }
        }
    }

    private func post(_ url: URL, secret: String, _ body: String) -> Int {
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue(secret, forHTTPHeaderField: "X-NestBox-Secret")
        request.httpBody = Data(body.utf8)
        return fetch(request).0
    }

    private func get(_ url: URL) -> String { String(decoding: fetch(URLRequest(url: url)).1, as: UTF8.self) }

    /// A request on this background thread, waiting for its answer: the status (0 without one) and the body.
    private func fetch(_ request: URLRequest) -> (Int, Data) {
        let done = DispatchSemaphore(value: 0)
        let result = Box<(Int, Data)>((0, Data()))
        URLSession.shared.dataTask(with: request) { data, response, _ in
            result.value = ((response as? HTTPURLResponse)?.statusCode ?? 0, data ?? Data())
            done.signal()
        }.resume()
        _ = done.wait(timeout: .now() + 10)
        return result.value
    }

    // MARK: - Slack

    /// Slack's parts without a connection: the manifest and its link, pasted tokens, which messages call the pet,
    /// the bubbles' words and lengths, Do Not Disturb.
    /// Secrets live in a file only this user reads; one an earlier version left in the keychain moves there.
    private func secretChecks() {
        let store = SecretStore(service: "pet.nestbox.selftest.secrets")
        let file = SecretStore.folder.appendingPathComponent("pet.nestbox.selftest.secrets.json")
        store.set("a", "one")
        let modes = [SecretStore.folder.path, file.path].map {
            ((try? FileManager.default.attributesOfItem(atPath: $0))?[.posixPermissions] as? Int) ?? 0
        }
        check(store.get("a") == "one" && modes == [0o700, 0o600],
              "a secret is kept in a file only this user can read (\(modes.map { String($0, radix: 8) }))")
        store.set("a", nil)
        check(store.get("a") == nil && !FileManager.default.fileExists(atPath: file.path),
              "a forgotten secret is gone from the file")
        let item: [CFString: Any] = [kSecClass: kSecClassGenericPassword, kSecAttrService: store.service,
                                     kSecAttrAccount: "old"]
        SecItemDelete(item as CFDictionary)
        var add = item
        add[kSecValueData] = Data("from the keychain".utf8)
        let added = SecItemAdd(add as CFDictionary, nil) == errSecSuccess
        let moved = store.get("old") == "from the keychain"
        let left = SecItemCopyMatching(item as CFDictionary, nil) == errSecSuccess
        check(added && moved && !left && store.get("old") == "from the keychain",
              "a secret left in the keychain moves to the file the first time it is read")
        store.set("old", nil)
        SecItemDelete(item as CFDictionary)
        var stray = item
        stray[kSecAttrAccount] = "stray"
        var addStray = stray
        addStray[kSecValueData] = Data("still in the keychain".utf8)
        let kept = SecItemAdd(addStray as CFDictionary, nil) == errSecSuccess
        store.set("stray", nil)
        check(kept && SecItemCopyMatching(stray as CFDictionary, nil) == errSecItemNotFound && store.get("stray") == nil,
              "forgetting a secret also deletes a copy left in the keychain")
        SecItemDelete(stray as CFDictionary)
    }

    private func slackChecks() {
        let manifest = try? JSONSerialization.jsonObject(with: Data(Slack.manifest.utf8)) as? [String: Any]
        let settings = manifest?["settings"] as? [String: Any]
        let scopes = ((manifest?["oauth_config"] as? [String: Any])?["scopes"] as? [String: Any])?["user"] as? [String]
        let events = (settings?["event_subscriptions"] as? [String: Any])?["user_events"] as? [String]
        check(settings?["socket_mode_enabled"] as? Bool == true && scopes == Slack.userScopes && events == Slack.userEvents
                  && manifest?["features"] == nil,
              "the manifest is JSON: Socket Mode, user scopes and user events only, no bot")
        let display = manifest?["display_information"] as? [String: Any]
        let nameLength = (display?["name"] as? String)?.count ?? 99
        let descriptionLength = (display?["description"] as? String)?.count ?? 999
        check(nameLength <= 35 && descriptionLength <= 140 && display?["long_description"] == nil,
              "the manifest's name and description fit Slack's limits, with no long description")
        let link = Slack.createAppURL.absoluteString
        let prefix = "https://api.slack.com/apps?new_app=1&manifest_json="
        check(link.hasPrefix(prefix) && String(link.dropFirst(prefix.count)).removingPercentEncoding == Slack.manifest
                  && !link.dropFirst(prefix.count).contains("&") && !link.contains("#"),
              "Create App in Slack: the manifest, percent-encoded, in Slack's link")
        check(Slack.problem(appToken: "xapp-1-A-1-x") == nil && Slack.problem(userToken: "xoxp-1") == nil
                  && Slack.problem(appToken: "xoxb-1")?.contains("bot token") == true
                  && Slack.problem(appToken: "xoxp-1")?.contains("user token") == true
                  && Slack.problem(userToken: "xapp-1")?.contains("app-level token") == true
                  && Slack.problem(userToken: "") != nil,
              "a token pasted in the wrong field says which one it is")

        let me = "UADA", all = SlackWants(threads: true)
        func call(_ e: SlackEvent, threads: Set<String> = [], wants: SlackWants = all) -> SlackCall? {
            SlackCall.of(e, me: me, myThreads: threads, wants: wants)
        }
        let dm = SlackEvent(type: "message", channel: "D1", channelType: "im", user: "U1", text: "hi", ts: "1.1")
        check(call(dm) == .directMessage && call(SlackEvent(type: "message", channel: "G1", channelType: "mpim",
                                                           user: "U1", ts: "1.1")) == .groupMessage,
              "a DM and a group DM call the pet")
        var mine = dm
        mine.user = me
        var edited = dm
        edited.subtype = "message_changed"
        var joined = dm
        joined.subtype = "channel_join"
        check(call(mine) == nil && call(edited) == nil && call(joined) == nil,
              "the user's own messages, edits and joins don't")
        let mention = SlackEvent(type: "message", channel: "C1", channelType: "channel", user: "U1",
                                 text: "look <@UADA|ada>", ts: "2.1")
        let plain = SlackEvent(type: "message", channel: "C1", channelType: "channel", user: "U1",
                               text: "look <@UADAM>", ts: "2.2")
        check(call(mention) == .mention && call(plain) == nil, "a mention calls; another person's mention doesn't")
        let reply = SlackEvent(type: "message", channel: "C1", channelType: "channel", user: "U1", text: "ok",
                               ts: "3.2", threadTS: "3.1")
        var toMine = reply
        toMine.parentUserID = me
        check(call(reply) == nil && call(reply, threads: ["C1/3.1"]) == .threadReply && call(toMine) == .threadReply,
              "a reply calls in a thread the user started or wrote in, not in others")
        var groupMention = mention
        groupMention.channelType = "mpim"
        var replyMention = toMine
        replyMention.text = "<@UADA> done"
        check(call(groupMention, wants: SlackWants(directMessages: false)) == .mention
                  && call(replyMention, wants: SlackWants(mentions: false, threads: true)) == .threadReply,
              "a message with two reasons calls for the one that is ticked")
        check(call(dm, wants: SlackWants(directMessages: false)) == nil
                  && call(mention, wants: SlackWants(mentions: false)) == nil
                  && call(toMine, wants: SlackWants(threads: false)) == nil,
              "each kind can be switched off")

        let longName = String(repeating: "Bartholomew ", count: 6)
        let kinds: [SlackCall] = [.directMessage, .groupMessage, .mention, .threadReply]
        let tells = kinds.flatMap { kind in
            [1, 3].map { SlackTell.call(kind, id: "C1", who: longName, place: "#" + longName, count: $0) }
        }
        let fits = tells.allSatisfy { $0.say.utf8.count <= SlackTell.sayBytes && $0.seconds == 20 }
        check(fits, "long names are cut to fit the bubble (72 bytes), shown for 20 s")
        let body = (try? JSONEncoder().encode(SlackTell.call(.mention, id: "C1", who: "Dana", place: "#deploys",
                                                             count: 1))).map { String(decoding: $0, as: UTF8.self) }
        check(body?.contains(#""say":"Dana mentioned you in #deploys""#) == true && body?.contains(#""for":20"#) == true
                  && body?.contains(#""from":"slack""#) == true && body?.contains("state") == false,
              "a mention's tell: a bubble with who and where, for 20 s, no state  [\(body ?? "")]")
        let sorted = JSONEncoder()
        sorted.outputFormatting = .sortedKeys
        let first = (try? sorted.encode(SlackTell.call(.directMessage, id: "D1", who: "Dana", place: nil, count: 1)))
            .map { String(decoding: $0, as: UTF8.self) }
        let more = (try? sorted.encode(SlackTell.call(.directMessage, id: "D1", who: "Dana", place: nil, count: 2)))
            .map { String(decoding: $0, as: UTF8.self) }
        check(first?.contains(#""effect":"letter""#) == true
                  && first?.contains(#""do":[{"do":"surprised","face":"front"}]"#) == true
                  && more?.contains(#""effect":"letter""#) == true && more?.contains(#""do""#) == false,
              "the first message: a letter, and the pet turns to you surprised; more only update the bubble")
        let now = Date()
        let t = now.timeIntervalSince1970
        check(SlackDND(snoozeEnabled: true, snoozeEnd: t + 60).quietUntil(now) != nil
                  && SlackDND(snoozeEnabled: true, snoozeEnd: t - 60).quietUntil(now) == nil
                  && SlackDND(dndEnabled: true, nextStart: t - 60, nextEnd: t + 60).quietUntil(now) != nil
                  && SlackDND(dndEnabled: true, nextStart: t + 60, nextEnd: t + 120).quietUntil(now) == nil,
              "Do Not Disturb: a snooze, or inside the scheduled hours")
        check(SlackDND(dndEnabled: true, nextStart: t - 120, nextEnd: t - 60).isStale(now)
                  && !SlackDND(dndEnabled: true, nextStart: t + 60, nextEnd: t + 120).isStale(now)
                  && !SlackDND(dndEnabled: false, nextStart: t - 120, nextEnd: t - 60).isStale(now),
              "scheduled Do Not Disturb hours that have passed are asked for again")
    }

    /// Slack end to end, against FakeSlack: tokens checked before they are saved, the socket, each kind of message
    /// as a bubble on the pet, Do Not Disturb, a reconnect Slack asks for, and disconnecting.
    private func slackEndToEnd() {
        guard let fake = fakeSlack else { return check(false, "the stand-in Slack started") }
        let m = model
        let secrets = SecretStore(service: Self.slackSecrets)
        let petPage = URL(string: device)!.appendingPathComponent("v1")
        let notes = { () -> [[String: Any]] in
            let page = self.fetch(URLRequest(url: petPage)).1
            let all = (try? JSONSerialization.jsonObject(with: page) as? [String: Any])?["notes"] as? [[String: Any]]
            return (all ?? []).filter { $0["from"] as? String == "slack" }
        }
        let note = { (id: String) -> [String: Any]? in notes().first { $0["id"] as? String == id } }
        let told = { (words: String) -> Bool in
            self.onMain { m.log.lines.contains { $0.contains(words) && $0.contains("-> 200") } }
        }
        func connect(_ app: String, _ user: String) -> String? {
            let answer = Box<String??>(nil)
            onMain { m.slack.connect(appToken: app, userToken: user) { answer.value = .some($0) } }
            _ = until(10) { answer.value != nil }
            return answer.value ?? "no answer"
        }

        check(connect("xoxb-1", FakeSlack.userToken)?.contains("bot token") == true && secrets.get("app") == nil,
              "a bot token in the app-token field is refused before asking Slack")
        check(connect("xapp-1-wrong", FakeSlack.userToken)?.contains("app-level token") == true
                  && secrets.get("app") == nil && onMain { m.slack.state == .off },
              "an app token Slack refuses: said so, nothing saved")
        check(connect(FakeSlack.appToken, FakeSlack.userToken) == nil && wait(5) { m.slack.state == .connected }
                  && secrets.get("app") == FakeSlack.appToken && secrets.get("user") == FakeSlack.userToken
                  && fake.hellos == 1,
              "the right tokens: checked, saved, connected")
        check(onMain { MenuBar.words(for: m.slack) } == "Connected as ada in Test Team" && until(5) { told("Slack is connected") },
              "the menu says who is connected, and the pet says Slack is connected")

        fake.event("Ev1", #"{"type":"message","channel":"D0DANA","channel_type":"im","user":"UDANA","#
                   + #""text":"the secret plan","ts":"1700000001.000100"}"#)
        check(until(5) { fake.acks.contains("env-Ev1") }, "each envelope is acknowledged")
        check(until(5) { note("D0DANA") != nil } && told("Dana wrote to you")
                  && !onMain { m.log.lines.contains { $0.contains("secret plan") } },
              "a DM: a bubble with who wrote, never what")
        fake.event("Ev1", #"{"type":"message","channel":"D0DANA","channel_type":"im","user":"UDANA","#
                   + #""text":"again","ts":"1700000001.000100"}"#)
        fake.event("Ev2", #"{"type":"message","channel":"D0DANA","channel_type":"im","user":"UDANA","#
                   + #""text":"more","ts":"1700000002.000100"}"#)
        check(until(5) { told("2 messages from Dana") } && !told("3 messages"),
              "a second DM counts; Slack sending one again does not")

        fake.event("Ev3", #"{"type":"message","channel":"C0DEPLOYS","channel_type":"channel","user":"UDANA","#
                   + #""text":"can you look, <@UADA>?","ts":"1700000010.000100"}"#)
        check(until(5) { note("C0DEPLOYS") != nil } && told("Dana mentioned you in #deploys"),
              "a mention: who and in which channel")
        let mark = onMain { m.log.lines.count }
        fake.event("Ev4", #"{"type":"message","channel":"C0DEPLOYS","channel_type":"channel","user":"UDANA","#
                   + #""text":"lunch?","ts":"1700000011.000100"}"#)
        _ = until(1.5) { false }
        check(fake.acks.contains("env-Ev4") && !onMain { m.log.lines.dropFirst(mark).contains { $0.contains("slack") } },
              "a channel message that doesn't call the user: acknowledged, not told")
        let mark1 = onMain { m.log.lines.count }
        fake.event("Ev5", #"{"type":"message","channel":"D0DANA","channel_type":"im","user":"UADA","#
                   + #""text":"on it <@UADA>","ts":"1700000012.000100"}"#)
        _ = until(1.5) { false }
        check(!onMain { m.log.lines.dropFirst(mark1).contains { $0.contains("\"from\":\"slack\"") } },
              "the user's own message shows nothing")

        onMain { m.setSlackWants(SlackWants(threads: true)) }
        fake.event("Ev6", #"{"type":"message","channel":"C0DEPLOYS","channel_type":"channel","user":"UADA","#
                   + #""text":"me too","ts":"1700000021.000100","thread_ts":"1700000020.000100"}"#)
        fake.event("Ev7", #"{"type":"message","channel":"C0DEPLOYS","channel_type":"channel","user":"UDANA","#
                   + #""text":"done","ts":"1700000022.000100","thread_ts":"1700000020.000100"}"#)
        check(until(5) { note("C0DEPLOYS-1700000020.000100") != nil } && told("Dana replied in a thread in #deploys"),
              "Replies in Your Threads ticked: a reply in a thread the user wrote in")
        onMain { m.setSlackWants(SlackWants(mentions: false, threads: true)) }
        let mark2 = onMain { m.log.lines.count }
        fake.event("Ev8", #"{"type":"message","channel":"C0DEPLOYS","channel_type":"channel","user":"UDANA","#
                   + #""text":"<@UADA> again","ts":"1700000030.000100"}"#)
        _ = until(1.5) { false }
        check(!onMain { m.log.lines.dropFirst(mark2).contains { $0.contains("mentioned you") } }
                  && onMain { m.prefs.slackWants.mentions == false },
              "Mentions unticked: a mention is not told")
        onMain { m.setSlackWants(SlackWants()) }

        let end = Int(Date().timeIntervalSince1970) + 600
        fake.event("Ev9", #"{"type":"dnd_updated","user":"UADA","dnd_status":{"dnd_enabled":false,"#
                   + #""snooze_enabled":true,"snooze_endtime":\#(end)}}"#)
        check(wait(5) { m.slack.quietUntil != nil && MenuBar.words(for: m.slack).hasPrefix("Do Not Disturb until") },
              "Slack's Do Not Disturb: the menu says so")
        let mark3 = onMain { m.log.lines.count }
        fake.event("Ev10", #"{"type":"message","channel":"D0DANA","channel_type":"im","user":"UDANA","#
                   + #""text":"psst","ts":"1700000040.000100"}"#)
        _ = until(1.5) { false }
        check(!onMain { m.log.lines.dropFirst(mark3).contains { $0.contains("Dana wrote") } },
              "...and a DM during it is not told")
        fake.event("Ev11", #"{"type":"dnd_updated","user":"UADA","dnd_status":{"dnd_enabled":false,"#
                   + #""snooze_enabled":false}}"#)
        check(wait(5) { m.slack.quietUntil == nil }, "Do Not Disturb ends")

        fake.disconnect("refresh_requested")
        check(until(10) { fake.hellos == 2 } && wait(5) { m.slack.state == .connected },
              "Slack asks for a new connection: connected again")

        let text = onMain { menuText(MenuBar(model: m).menu) }
        check(text.contains("Slack") && text.contains("✓ Direct Messages") && text.contains("✓ Mentions")
                  && text.contains("  Replies in Your Threads") && text.contains("Slack Setup…")
                  && text.contains("Disconnect Slack…"),
              "the Slack menu: DMs and mentions ticked, thread replies not, Slack Setup and Disconnect")
        if let dir = shots {
            let ok = onMain { () -> Bool in
                let window = SlackWindow(model: m)
                window.refresh()
                return savePNG(drawn(window.window.contentView), to: dir, as: "slack.png")
                    && savePNG(Icon.slackIcon(), to: dir, as: "slack-icon.png")
            }
            check(ok, "drew the Slack window and its icon into \(dir)")
        }

        onMain { m.slack.disconnect() }
        check(until(5) { fake.connected == 0 } && secrets.get("app") == nil && secrets.get("user") == nil
                  && onMain { m.slack.state == .off },
              "Disconnect Slack: the socket closes and the tokens are gone")
        fake.stop()
    }

    private func quickTest() {
        let status = Box(0), reply = Box<MessageReply?>(nil)
        onMain {
            model.send(QuickTest.all[0]) {
                reply.value = $1
                status.value = $0
            }
        }
        check(wait(5) { status.value != 0 } && status.value == 200 && reply.value?.ok == true,
              "quick test 'Done' -> \(status.value) \(reply.value?.doing ?? "-")")
    }

    // The pet's page for coding agents, and its link.
    private func teaching() {
        let done = Box(false)
        let window = onMain { () -> TeachWindow in
            let window = TeachWindow(model: model)
            window.load { done.value = true }
            return window
        }
        check(wait(5) { done.value }, "fetched the page for coding agents")
        let page = onMain { window.page }, link = onMain { window.link } ?? ""
        check(page.contains("POST ") && page.contains("/v1/tell") && link.hasPrefix(device)
                  && link.hasSuffix("/agents"),
              "it is the pet's own page, with its link: \(link) (\(page.count) characters)")
        if let dir = shots { _ = onMain { savePNG(drawn(window.window.contentView), to: dir, as: "teach.png") } }
    }

    // Finding the pet by its Bonjour name, as on a Mac with no ~/.nestbox/device yet.
    private func bonjour() {
        guard let name = bonjourName else {
            print("SKIP  Bonjour: give --name NAME (the test pet's name) to test it")
            return
        }
        let home = Self.makeHome(tmp.appendingPathComponent("bonjour-home"))
        let m = onMain { () -> Model in
            let m = Model(prefs: model.prefs, paths: model.paths, device: nil, name: name, simulatorArguments: [],
                          childrenFile: nil, childHome: home)
            m.begin()
            return m
        }
        check(wait(15) { m.companion.petURL != nil && m.companion.isConnected },
              "found \(name) by Bonjour at \(onMain { m.companion.petURL?.absoluteString ?? "-" }) and connected")
        check(wait(5) { m.status?.device == name },
              "status reads that pet: \(onMain { StatusText.lines(for: m).prefix(3).joined(separator: " | ") })")
        let remembered = (try? String(contentsOfFile: home + "/.nestbox/device", encoding: .utf8)) ?? ""
        check(remembered.hasPrefix("http://"),
              "remembered for hooks: \(remembered.trimmingCharacters(in: .whitespacesAndNewlines))")
        onMain { m.stopAll() }
    }

    // Pairing by id: the only device pairs itself, choosing another pairs that one, and a renamed pet is still found.
    private func pairing() {
        let home = Self.makeHome(tmp.appendingPathComponent("pair-home"))
        let environment = helperEnvironment(home: home)
        UserDefaults().removePersistentDomain(forName: Self.pairingSuite)
        defer { UserDefaults().removePersistentDomain(forName: Self.pairingSuite) }
        let parsed = NetworkPet.parse(
            "@nestbox {\"event\": \"pet\", \"name\": \"My NestBox\", \"url\": \"http://nestbox-c3c3.local:80\", "
            + "\"id\": \"c3c3c3\", \"host\": \"nestbox-c3c3.local\", \"pet\": \"Big Goose\", \"simulator\": false, "
            + "\"paired\": false}\nunrelated line\n"
            + "@nestbox {\"event\": \"pet\", \"name\": \"nestbox-simulator\", \"url\": \"http://mac.local:8080\", "
            + "\"id\": \"sim-mac\", \"host\": \"\", \"pet\": \"\", \"simulator\": true, \"paired\": false}\n")
        check(parsed.count == 2 && parsed[0].name == "My NestBox" && parsed[0].pet == "Big Goose"
              && parsed[0].id == "c3c3c3" && !parsed[0].isSimulator
              && parsed[1].isSimulator && parsed[1].url.absoluteString == "http://mac.local:8080",
              "reads `find --machine` lines: names and values with spaces, simulators")
        let helper = model.paths.helper
        let first = announce("mb-fake-pet-1", ["id=a1a1a1", "host=nestbox-a1a1.local", "pet=Goose"])
        defer { first.terminate() }
        let others = NetworkPet.scan(helper: helper, environment: environment)
            .filter { !$0.isSimulator && $0.id != "a1a1a1" }
        let listed = NetworkPet.scan(helper: helper, environment: environment).first { $0.id == "a1a1a1" }
        let listedText = listed.map { "\($0.name) \($0.url) id=\($0.id) host=\($0.host) pet=\($0.pet)" } ?? "nothing"
        check(listed?.name == "mb-fake-pet-1" && listed?.host == "nestbox-a1a1.local" && listed?.pet == "Goose"
              && listed?.isSimulator == false, "find reads the TXT: \(listedText)")
        let m = onMain { () -> Model in
            let prefs = Preferences(defaults: Self.suiteDefaults(Self.pairingSuite))
            let m = Model(prefs: prefs, paths: model.paths, device: nil, name: nil, simulatorArguments: [],
                          childrenFile: nil, childHome: home)
            m.begin()
            return m
        }
        defer { onMain { m.stopAll() } }
        @MainActor func found(after count: Int, _ id: String) -> Bool {
            m.log.lines.dropFirst(count).contains { $0.hasPrefix("found the pet at ") && $0.hasSuffix("id=" + id) }
        }
        if others.isEmpty {
            check(wait(20) { m.prefs.pairedID == "a1a1a1" },
                  "the only real pet on the network pairs itself (id \(onMain { m.prefs.pairedID ?? "-" }))")
            let file = (try? String(contentsOfFile: home + "/.nestbox/device", encoding: .utf8)) ?? ""
            check(file.contains("id=a1a1a1") && file.hasPrefix("http://"),
                  "~/.nestbox/device keeps its URL and id: \(file.replacingOccurrences(of: "\n", with: " "))")
        } else {
            print("SKIP  self-pairing: other real pets are on the network (\(others.map(\.name)))")
        }
        let second = announce("mb-fake-pet-2", ["id=b2b2b2", "host=nestbox-b2b2.local", "pet=Robot"])
        let scanned = Box(false)
        onMain { m.scanNetwork { scanned.value = true } }
        _ = wait(10) { scanned.value }
        check(onMain { m.networkPets.contains { $0.id == "a1a1a1" } && m.networkPets.contains { $0.id == "b2b2b2" } },
              "Pets on Your Network lists both: \(onMain { m.networkPets.map { "\($0.pet) at \($0.location)" } })")
        var mark = onMain { m.log.lines.count }
        onMain { if let pet = m.networkPets.first(where: { $0.id == "b2b2b2" }) { m.pair(pet) } }
        check(onMain { m.companion.process?.arguments.contains("b2b2b2") == true },
              "choosing one restarts the companion with --id b2b2b2")
        check(wait(20) { found(after: mark, "b2b2b2") }, "the companion finds the chosen pet by its id")
        second.terminate()
        let renamed = announce("mb-renamed-pet", ["id=b2b2b2", "host=nestbox-new.local", "pet=Robot"])
        defer { renamed.terminate() }
        mark = onMain { m.log.lines.count }
        onMain { m.companion.restart() }
        check(wait(20) { found(after: mark, "b2b2b2") }, "renamed, it is still found by its id")
        check(onMain { m.prefs.pairedID } == "b2b2b2", "still paired with b2b2b2")
    }

    private func wifiOverUSB() {
        let environment = helperEnvironment(home: Self.makeHome(tmp.appendingPathComponent("wifi-home")))
        let helper = model.paths.helper
        let cases = [("Home", "secret"), ("My Network", "pass word with spaces"), ("Café 2.4", "p\"q'r\\s"),
                     ("ab\"cd", "x"), ("Open", ""), (" Lead", "x")]
        for (network, password) in cases {
            let back = (try? WiFiSetup.command(ssid: network, password: password).get()).flatMap(firmwareWiFiCommand)
            check(back?.ssid == network && back?.pass == password,
                  "wifi line for “\(network)” reads back exactly on the pet")
        }
        check(PosixTimeZone.rule(for: "Europe/Berlin") == "CET-1CEST,M3.5.0,M10.5.0/3"
              && PosixTimeZone.rule(for: "America/Nuuk") == "<-02>2<-01>,M3.5.0/-1,M10.5.0/0"
              && PosixTimeZone.rule(for: "UTC") == "UTC0" && PosixTimeZone.rule(for: "Nowhere/Atlantis") == nil,
              "time zone footers: Europe/Berlin -> CET-1CEST,M3.5.0,M10.5.0/3, and an extended one")
        check(PosixTimeZone.rule() != nil,
              "this Mac's zone \(TimeZone.current.identifier) -> \(PosixTimeZone.rule() ?? "none")")
        let refused = [("My \"Net\"", "x"), ("", "x"), ("N", " pad ")].allSatisfy {
            (try? WiFiSetup.command(ssid: $0.0, password: $0.1).get()) == nil
        }
        check(refused, "names and passwords the pet can't read are refused with a reason")
        let ports = USBPet.ports()
        print("      USB pets plugged in now: \(ports.isEmpty ? "none" : ports.joined(separator: ", ")); "
              + "the Mac's Wi-Fi: \(MacWiFi.currentSSID ?? "hidden by macOS")")

        // A stand-in for a new pet; once it is "on Wi-Fi", dns-sd announces it with its own id and host.
        let secret = "s3cret \"pass\" \(getpid())"
        let fakeConsole = FakeConsole(device: "nestbox", id: "f00d01", host: "nestbox-f00d.local", password: secret)
        guard let fake = fakeConsole else {
            check(false, "fake pet console (pty)")
            return
        }
        let w = onMain { WiFiSetup(helper: helper, serialOverride: fake.path, helperEnvironment: environment) }
        onMain { w.scan() }
        check(onMain { w.port } == fake.path, "a console at \(fake.path) stands in for the pet's USB port")
        let phases = Box<[String]>([])
        onMain {
            w.onChange = { phases.value.append("\(w.phase)") }
            w.join(ssid: "My Network", password: secret)
        }
        _ = wait(4) { phases.value.contains { $0.hasPrefix("joining") } }
        let announced = announce("mb-fake-pet", ["id=f00d01", "host=nestbox-f00d.local", "pet=Goose"])
        _ = wait(40) {
            switch w.phase {
            case .online, .failed: return true
            default: return false
            }
        }
        check(onMain { w.found?.id } == "f00d01",
              "joins, then finds that very pet by its id: \(onMain { w.phase }) \(onMain { describe(w.found) })")
        check(fake.got?.ssid == "My Network" && fake.got?.pass == secret,
              "the pet got the network and the password exactly")
        check(fake.tz != nil && fake.tz == PosixTimeZone.rule(),
              "the pet got the Mac's time zone: tz \(fake.tz ?? "-")")
        check(phases.value.contains { $0.hasPrefix("joining") }, "says it is joining while it waits")
        let logged = onMain { model.log.lines.joined(separator: "\n") }
        let prefs = "\(UserDefaults.standard.dictionaryRepresentation())" + "\(defaults.dictionaryRepresentation())"
        check(!logged.contains(secret) && !prefs.contains(secret) && !phases.value.joined().contains(secret),
              "the password is in no log, preference, or message")
        announced.terminate()
        fake.stop()

        // A wrong password: the pet keeps trying and does not get on.
        let wrongConsole = FakeConsole(device: "nestbox", id: "f00d02", host: "nestbox-f00d.local", password: "right")
        guard let wrong = wrongConsole else { return }
        let w2 = onMain {
            WiFiSetup(helper: helper, serialOverride: wrong.path, joinTimeout: 5, helperEnvironment: environment)
        }
        onMain {
            w2.scan()
            w2.join(ssid: "Cafe", password: "wrong")
        }
        check(wait(20) { failed(w2, with: "couldn't join") },
              "wrong password: \(onMain { w2.phase })")
        wrong.stop()

        // Something that is not the pet's console.
        guard let mute = FakeConsole(device: "nestbox", id: "", host: "", password: "x", mute: true) else { return }
        let w3 = onMain { WiFiSetup(helper: helper, serialOverride: mute.path, helperEnvironment: environment) }
        onMain {
            w3.scan()
            w3.join(ssid: "Cafe", password: "x")
        }
        check(wait(10) { failed(w3, with: "didn't answer") },
              "a port that doesn't answer: \(onMain { w3.phase })")
        check(mute.got == nil, "nothing sent beyond `now` to a port that never answered it")
        mute.stop()

        guard let dir = shots, let plugged = FakeConsole(device: "nestbox", id: "", host: "", password: "x") else {
            return
        }
        let ok = onMain { () -> Bool in
            let fresh = Model(prefs: model.prefs, paths: model.paths, device: nil, name: nil, simulatorArguments: [],
                              childrenFile: nil)
            let withPet = WiFiSetup(helper: helper, serialOverride: plugged.path)
            let withNone = WiFiSetup(helper: helper, serialOverride: USBPet.ports().isEmpty ? nil : "/nonexistent")
            let pluggedIn = snapshot(SetupWindow(model: fresh, wifi: withPet), wifi: withPet)
            guard savePNG(pluggedIn, to: dir, as: "setup-wifi.png") else { return false }
            guard USBPet.ports().isEmpty else { return true }
            let nothingFound = snapshot(SetupWindow(model: fresh, wifi: withNone), wifi: withNone)
            return savePNG(nothingFound, to: dir, as: "setup-plug.png")
        }
        check(ok, "drew the first-run window with a pet plugged in, and with none")
        plugged.stop()
    }

    private func petWindow() {
        let m = model
        guard !simulatorArguments.isEmpty else {
            print("SKIP  pet window: give --sim-args \"--headless --port N --name X\" to test it")
            return
        }
        onMain { m.togglePetWindow() }
        check(wait(8) { m.petWindow.isRunning && m.petWindow.url != nil },
              "pet window starts (\(simulatorArguments.joined(separator: " "))) "
              + "at \(onMain { m.petWindow.url?.absoluteString ?? "-" })")
        let pid = onMain { m.petWindow.process?.pid } ?? 0
        let answer = Box<PetStatus?>(nil)
        onMain {
            if let url = m.petWindow.url { PetClient.get(PetStatus.self, "/v1", from: url) { answer.value = $0 } }
        }
        check(wait(5) { answer.value != nil }, "the pet window answers GET /v1")
        petScreens()
        onMain { m.togglePetWindow() }
        check(wait(3) { !m.petWindow.isRunning && kill(pid, 0) != 0 }, "hide pet window stops it (pid \(pid))")
    }

    /// Developer > Screen: the simulator's screens are listed, and picking one starts the pet window again on it.
    private func petScreens() {
        let m = model
        onMain { m.loadPetScreens() }
        check(wait(5) { m.petScreens.map(\.id).contains("lcd-2") },
              "lists the pet window's screens (\(onMain { m.petScreens.map(\.id).joined(separator: ", ") }))")
        guard let first = onMain({ m.petScreens.first }) else { return }
        for screen in [onMain { m.petScreens.first { $0.id == "lcd-2" } }, first].compactMap({ $0 }) {
            let before = onMain { m.petWindow.process?.pid } ?? 0
            onMain { m.setPetScreen(screen.id) }
            check(wait(8) { m.petWindow.isRunning && m.petWindow.url != nil && m.petWindow.process?.pid != before },
                  "picking \(screen.id) starts the pet window again")
            check(onMain { m.petWindow.process?.arguments.contains(screen.id) == true && m.petScreen == screen.id },
                  "it runs with --display \(screen.id)")
            let size = Box<(Int, Int)?>(nil)
            if let url = onMain({ m.petWindow.url?.appendingPathComponent("screen.bmp") }) {
                URLSession.shared.dataTask(with: url) { data, _, _ in
                    guard let data, data.count > 26 else { return }
                    let int = { (at: Int) in data[at..<at + 4].enumerated().reduce(0) { $0 | Int($1.1) << (8 * $1.0) } }
                    size.value = (int(18), abs(Int(Int32(truncatingIfNeeded: int(22)))))
                }.resume()
            }
            check(wait(5) { size.value != nil } && size.value! == (screen.w, screen.h),
                  "its screen is \(screen.w)x\(screen.h) (\(size.value.map { "\($0.0)x\($0.1)" } ?? "-"))")
        }
        check(onMain { m.prefs.petScreen } == first.id, "the pick is remembered")
    }

    private func quit() {
        let pid = onMain { model.companion.process?.pid } ?? 0
        onMain { model.stopAll() }
        check(wait(3) { kill(pid, 0) != 0 }, "quit stops the companion (pid \(pid))")
        check(onMain { model.companion.process == nil && model.petWindow.process == nil }, "no children left")
    }
}
