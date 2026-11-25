import Foundation

/// The app's state: the companion it supervises, the pet window it may run, the pets on the network, and what
/// the pet in use reports. The menu and the windows call its methods.
@MainActor
final class Model {
    private static let pollInterval: TimeInterval = 2

    let prefs: Preferences
    let paths: AppPaths
    /// --device, or the `device` preference: the companion and the status use only this pet.
    let deviceOverride: URL?
    let log = EventLog()
    let companion: CompanionSupervisor
    let petWindow: PetWindowRunner
    private let registry: ChildRegistry?
    private let childEnvironment: [String: String]?

    var onChange: (() -> Void)?

    /// Other copies of the helper, not started by this app.
    private(set) var otherCompanions: [pid_t] = []
    private(set) var status: PetStatus?
    private(set) var statusSource: URL?
    private(set) var pets: [DevicePet] = []
    private(set) var petOut: String?
    private(set) var networkPets: [NetworkPet] = []
    private(set) var isScanning = false
    private var timer: Timer?
    private let networkWatch = NetworkWatch()
    let calendar = CalendarWatch()
    /// Webhooks through the relay. NESTBOX_RELAY and NESTBOX_RELAY_SECRETS point a test at a local relay and a
    /// secrets file of its own.
    private static let relay = ProcessInfo.processInfo.environment["NESTBOX_RELAY"].flatMap(URL.init(string:))
        ?? WebhookBridge.defaultRelay
    private static let relaySecrets = SecretStore(
        service: ProcessInfo.processInfo.environment["NESTBOX_RELAY_SECRETS"] ?? "pet.nestbox.relay")
    /// The pet holds the relay's socket itself: the usual way.
    let petWebhooks = PetWebhooks(relay: Model.relay, secrets: Model.relaySecrets)
    /// Developer › Webhooks Through This Mac: a socket of this Mac's own, on a mailbox of its own.
    let webhooks = WebhookBridge(relay: Model.relay, secrets: Model.relaySecrets)
    private var calendarPet: URL?
    /// Slack through Socket Mode. NESTBOX_SLACK_API and NESTBOX_SLACK_SECRETS point a test at a stand-in Slack and
    /// a secrets file of its own.
    let slack = SlackWatch(
        api: ProcessInfo.processInfo.environment["NESTBOX_SLACK_API"].flatMap(URL.init(string:)) ?? Slack.api,
        secrets: SecretStore(service: ProcessInfo.processInfo.environment["NESTBOX_SLACK_SECRETS"]
                                     ?? "pet.nestbox.slack"))

    /// `childrenFile` records running children for cleanup after a crash; `childHome` sets HOME for the helper
    /// processes (where they keep ~/.nestbox/device).
    init(prefs: Preferences, paths: AppPaths, device: URL?, name: String?, simulatorArguments: [String],
         childrenFile: URL?, childHome: String? = nil) {
        self.prefs = prefs
        self.paths = paths
        deviceOverride = device
        registry = childrenFile.map(ChildRegistry.init)
        childEnvironment = childHome.map { ProcessInfo.processInfo.environment.merging(["HOME": $0]) { $1 } }
        companion = CompanionSupervisor(helper: paths.helper, prefs: prefs, deviceOverride: device, nameOverride: name,
                                        environment: childEnvironment, log: log)
        petWindow = PetWindowRunner(executable: paths.simulator, arguments: simulatorArguments)
        petWindow.screen = prefs.petScreen
        companion.onChange = { [weak self] in self?.onChange?() }
        companion.onChildrenChanged = { [weak self] in self?.saveChildren() }
        petWindow.onChange = { [weak self] in self?.onChange?() }
        petWindow.onChildrenChanged = { [weak self] in self?.saveChildren() }
    }

    /// The pet the status and the menus talk to: the one the companion found, else the pet window.
    var petURL: URL? { deviceOverride ?? companion.petURL ?? petWindow.url }

    func begin() {
        registry?.stopLeftovers()
        companion.start()
        loadPetScreens()
        let timer = Timer(timeInterval: Self.pollInterval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }    // the timer runs on the main run loop
        }
        RunLoop.main.add(timer, forMode: .common)    // .common keeps it firing while the menu is open
        self.timer = timer
        networkWatch.onChange = { [weak self] in
            self?.companion.reachAgain()
            self?.webhooks.reachAgain()
            self?.slack.reachAgain()
            self?.scanNetwork()
            self?.tick()
        }
        networkWatch.start()
        calendar.onTell = { [weak self] tell in self?.sendCalendar(tell) }
        calendar.onSay = { [weak self] words in
            self?.sendCalendar(PetMessage(from: "calendar", id: "start", say: words, seconds: CalendarTell.startSeconds))
        }
        calendar.onNote = { [weak self] note in self?.log.append("-- " + note) }
        if prefs.isEnabled(.calendar) { calendar.start() }
        petWebhooks.onNote = { [weak self] note in self?.log.append("-- " + note) }
        petWebhooks.onChange = { [weak self] in self?.onChange?() }
        petWebhooks.onAdoptBridgeMailbox = { [weak self] in
            self?.prefs.webhooksThroughMac = false
            self?.webhooks.pause()
        }
        petWebhooks.addressOfPet = { [weak self] id in self?.networkPets.first { $0.id == id }?.url }
        // From an app before the pet could hold the socket: the bridge keeps running its URL as it did until a pet
        // that can hold it takes it over.
        if !prefs.hasWebhooksThroughMac {
            prefs.webhooksThroughMac = petWebhooks.bridgeHasOldMailbox && prefs.receivedWebhooks
        }
        webhooks.onTell = { [weak self] tell in self?.forwardWebhook(tell) }
        webhooks.onNote = { [weak self] note in self?.log.append("-- " + note) }
        webhooks.onChange = { [weak self] in self?.onChange?() }
        if prefs.webhooksThroughMac { webhooks.start() } else { webhooks.pause() }
        slack.onTell = { [weak self] tell in self?.sendSlack(tell) }
        slack.onNote = { [weak self] note in self?.log.append("-- " + note) }
        slack.onChange = { [weak self] in self?.onChange?() }
        slack.wants = prefs.slackWants
        slack.start()
        tick()
    }

    func tick() {
        if calendar.isRunning, let url = petURL, url != calendarPet {
            calendarPet = url
            calendar.tellAgain()
        }
        let ours = companion.process?.pid
        otherCompanions = ProcessTable.pids(runningExecutableAt: paths.helper.path).filter { $0 != ours }
        pollStatus()
        onChange?()
    }

    func setSignal(_ signal: ActivitySignal, enabled: Bool) {
        prefs.setEnabled(signal, enabled)
        guard signal.isHelpers else {
            log.append("-- \(signal.title) \(enabled ? "on" : "off")")
            if enabled { calendar.start() } else { calendar.stop() }
            return
        }
        log.append("-- \(signal.title) \(enabled ? "on" : "off"): restarting the companion")
        companion.restart()
        if signal == .music && !enabled { endMusic() }
    }

    /// Connections › Slack: what may call the pet.
    func setSlackWants(_ wants: SlackWants) {
        prefs.slackWants = wants
        slack.wants = wants
    }

    /// Pauses or resumes webhooks on the pet (on again also takes the URL back from another pet that took it).
    func setReceivingWebhooks(_ on: Bool) {
        petWebhooks.setReceiving(on, pet: petURL) { [weak self] _ in self?.pollStatus() }
    }

    /// Saves the webhooks access key and gives the pet its keys.
    func setWebhooksAccessKey(_ key: String) {
        petWebhooks.setAccessKey(key, pet: mayHoldWebhooks(status) ? petURL : nil, status: status, receiving: true)
        if prefs.webhooksThroughMac { webhooks.start() }
    }

    /// Developer › Webhooks Through This Mac.
    func setWebhooksThroughMac(_ on: Bool) {
        prefs.webhooksThroughMac = on
        log.append("-- webhooks through this Mac \(on ? "on" : "off")")
        if on { webhooks.resume() } else { webhooks.pause() }
    }

    /// Only the paired pet (or the one given with --device) gets the relay's keys: a pet window that answers while
    /// the paired pet is away must not take its webhook URL.
    private func mayHoldWebhooks(_ status: PetStatus?) -> Bool {
        deviceOverride != nil || (status?.id != nil && status?.id == prefs.pairedID)
    }

    /// Webhooks through the pet, as the menu shows them.
    var webhookState: PetWebhooks.State { petWebhooks.state(status, mayHold: mayHoldWebhooks(status)) }

    /// A verified webhook: its tell goes to the pet as it came.
    private func forwardWebhook(_ tell: Data) {
        guard let base = petURL else {
            log.append("-- webhook dropped: no pet found")
            return
        }
        let body = String(decoding: tell, as: UTF8.self)
        PetClient.post(data: tell, to: base) { [weak self] status, _ in
            self?.log.append("\(body) -> \(status) (webhook)")
        }
    }

    private func sendCalendar<Tell: Encodable>(_ tell: Tell) {
        guard let base = petURL else { return }
        calendarPet = base
        let body = (try? JSONEncoder().encode(tell)).flatMap { String(data: $0, encoding: .utf8) } ?? ""
        PetClient.post(tell, to: base) { [weak self] status, _ in self?.log.append("\(body) -> \(status)") }
    }

    private func sendSlack(_ tell: SlackTell) {
        guard let base = petURL else {
            log.append("-- slack: no pet found to tell")
            return
        }
        let body = (try? JSONEncoder().encode(tell)).flatMap { String(data: $0, encoding: .utf8) } ?? ""
        PetClient.post(tell, to: base) { [weak self] status, _ in self?.log.append("\(body) -> \(status)") }
    }

    /// Music switched off: the song's note goes at once, instead of when the song would have ended.
    private func endMusic() {
        guard let base = petURL else { return }
        PetClient.post(PetMessage(from: "music", state: "over"), to: base) { [weak self] status, _ in
            self?.log.append("-- music over: \(status)")
        }
    }

    /// Quitting: stops both children and waits for them, so the record of running children is empty and true.
    func stopAll() {
        companion.stopAndWait()
        petWindow.stopAndWait()
        webhooks.stop()
        slack.stop()
        timer?.invalidate()
        networkWatch.stop()
        saveChildren()
    }

    private func saveChildren() {
        registry?.save([companion.process, petWindow.process].compactMap { $0 })
    }

    // MARK: - Pets on the network and pairing

    func scanNetwork(completion: (() -> Void)? = nil) {
        guard !isScanning, deviceOverride == nil else {
            completion?()
            return
        }
        isScanning = true
        let helper = paths.helper, environment = childEnvironment
        Task {
            let pets = await Task.detached { NetworkPet.scan(helper: helper, environment: environment) }.value
            networkPets = pets
            isScanning = false
            onChange?()
            completion?()
        }
    }

    func isPaired(_ pet: NetworkPet) -> Bool { prefs.pairedID == pet.id }

    /// `nestbox pair` is running: the pet waits for its button.
    private(set) var isGettingKey = false

    /// Gets the pet's key: the pet asks the user, who presses its button. Completes with whether it worked and why
    /// not.
    func getKey(completion: @escaping (Bool, String) -> Void) {
        guard !isGettingKey else {
            completion(false, "pairing is already waiting for the pet's button")
            return
        }
        guard let device = petURL else {
            completion(false, "no pet found yet")
            return
        }
        isGettingKey = true
        onChange?()
        let helper = paths.helper, environment = childEnvironment
        Task {
            let result = await Task.detached {
                Pairing.run(helper: helper, device: device, environment: environment)
            }.value
            isGettingKey = false
            log.append("-- pairing: \(result.paired ? "done" : result.detail)")
            if result.paired { petWebhooks.paired() }
            onChange?()
            completion(result.paired, result.detail)
        }
    }

    /// A pet's name as it calls itself (its title), from its id; the id when the list hasn't come yet.
    func title(of id: String) -> String { pets.first { $0.id == id }?.name ?? id }

    /// Several devices are on the network and none is paired yet.
    var needsPick: Bool {
        !prefs.hasPairing && networkPets.filter { !$0.isSimulator }.count > 1
    }

    /// A pet without an id can't be told apart from others later, so it isn't paired.
    func pair(_ pet: NetworkPet) {
        guard !pet.id.isEmpty else { return }
        prefs.pairedID = pet.id
        companion.forgetPet()
        log.append("-- paired with \(pet.name) (id \(pet.id)): restarting the companion")
        companion.restart()
    }

    // MARK: - The pet window

    func togglePetWindow() {
        if petWindow.isRunning {
            petWindow.stop()
            saveChildren()
            onChange?()
        } else {
            petWindow.start()
        }
    }

    /// The screens the pet window can be, once the simulator has listed them.
    private(set) var petScreens: [PetScreen] = []

    func loadPetScreens() {
        guard let simulator = paths.simulator else { return }
        Task {
            petScreens = await Task.detached { PetScreen.list(simulator: simulator) }.value
            onChange?()
        }
    }

    /// The screen in use: the chosen one, else the simulator's default (listed first).
    var petScreen: String? { petWindow.screen ?? petScreens.first?.id }

    /// Shows the pet on another screen; a running pet window starts again on it.
    func setPetScreen(_ id: String) {
        guard id != petScreen else { return }
        prefs.petScreen = id
        petWindow.screen = id
        log.append("-- pet window screen: \(id)")
        if petWindow.isRunning {
            petWindow.stopAndWait()
            petWindow.start()
        }
        onChange?()
    }

    // MARK: - The pet's own state

    /// Counts status requests: an answer that comes after a newer request's is old, and dropped.
    private var statusRequest = 0

    func pollStatus() {
        guard let base = petURL else {
            status = nil
            return
        }
        statusRequest += 1
        let mine = statusRequest
        PetClient.get(PetStatus.self, "/v1", from: base) { [weak self] status in
            guard let self, mine == self.statusRequest else { return }
            self.status = status
            self.statusSource = base
            if self.mayHoldWebhooks(status) {
                self.petWebhooks.follow(status, pet: base, receiving: self.prefs.receivedWebhooks)
            }
            if let pet = status?.pet, let match = self.pets.first(where: { $0.id == pet }) {
                self.petOut = match.id
            }
            self.onChange?()
        }
    }

    /// Fetched when the menu opens rather than polled: GET /v1/pets makes the pet rescan its eggs.
    func fetchPets(completion: (() -> Void)? = nil) {
        guard let base = petURL else {
            pets = []
            completion?()
            return
        }
        PetClient.get(PetList.self, "/v1/pets", from: base) { [weak self] list in
            guard let self else { return }
            self.pets = list?.pets ?? []
            if let list { self.petOut = list.current }
            self.onChange?()
            completion?()
        }
    }

    func bringOut(_ id: String, completion: ((Int) -> Void)? = nil) {
        guard let base = petURL else {
            completion?(0)
            return
        }
        PetClient.post(PetChoice(pet: id), "/v1", to: base) { [weak self] status, _ in
            if status == HTTPStatus.ok { self?.petOut = id }
            self?.pollStatus()
            self?.onChange?()
            completion?(status)
        }
    }

    /// Switches a sender off (the pet ignores it) or back on; completes with the HTTP status (0: no answer).
    func setListening(to sender: String, _ listening: Bool, completion: ((Int) -> Void)? = nil) {
        guard let base = petURL, let status else {
            completion?(0)
            return
        }
        let name = sender.lowercased()
        let ignore = status.ignore.filter { $0 != name } + (listening ? [] : [name])
        PetClient.post(IgnoredSenders(ignore: ignore), "/v1", to: base) { [weak self] code, _ in
            self?.pollStatus()
            completion?(code)
        }
    }

    /// Asks the pet to forget its Wi-Fi; completes with the HTTP status (0: no answer).
    func forgetWiFi(completion: @escaping (Int) -> Void) {
        guard let base = petURL else {
            completion(0)
            return
        }
        PetClient.post(ForgetWiFi(), "/v1", to: base) { status, _ in completion(status) }
    }

    func send(_ test: QuickTest, completion: ((Int, MessageReply?) -> Void)? = nil) {
        guard let base = petURL else {
            completion?(0, nil)
            return
        }
        PetClient.post(test.message, to: base) { [weak self] status, reply in
            self?.log.append("-- quick test \(test.title): \(status)")
            self?.pollStatus()
            completion?(status, reply)
        }
    }
}
