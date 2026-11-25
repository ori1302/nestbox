import AppKit

/// The status item and its menu. The menu is rebuilt from the model whenever it changes, so it stays live while
/// it is open.
@MainActor
final class MenuBar: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private static let statusSlots = 4

    let menu = NSMenu()
    private let model: Model
    private let wifi: WiFiSetup
    private var statusItem: NSStatusItem?
    private var statusLines: [NSMenuItem] = []
    private let keyItem = NSMenuItem(title: "Pair with the Pet's Button…", action: #selector(getKey),
                                     keyEquivalent: "")
    private let petWindowItem = NSMenuItem(title: "Show Pet Window", action: #selector(togglePetWindow),
                                           keyEquivalent: "")
    private let loginItem = NSMenuItem(title: "Start at Login", action: #selector(toggleLogin), keyEquivalent: "")
    private let petsMenu = NSMenu()
    private let networkMenu = NSMenu()
    private let connectionsMenu = NSMenu()
    private let webhooksMenu = NSMenu()
    private var webhooksItem: NSMenuItem?
    private var webhookStatus: NSMenuItem?
    private var webhookReceive: NSMenuItem?
    private var webhookCopies: [NSMenuItem] = []
    private var webhookRemove: NSMenuItem?
    private let slackMenu = NSMenu()
    private var slackItem: NSMenuItem?
    private var slackStatus: NSMenuItem?
    private var slackWants: [NSMenuItem] = []
    private var slackSetup: NSMenuItem?
    private var slackDisconnect: NSMenuItem?
    private var slackWindow: SlackWindow?
    private let bridgeMenu = NSMenu()
    private var bridgeStatus: NSMenuItem?
    private var bridgeToggle: NSMenuItem?
    private var bridgeCopies: [NSMenuItem] = []
    private var sendersHeader: NSMenuItem?
    private var sendersSeparator: NSMenuItem?
    private var senderItems: [NSMenuItem] = []
    private let screenMenu = NSMenu()
    private var screenItem: NSMenuItem?
    private var shownPets: PetsMenuState?
    private var shownNetwork: NetworkMenuState?

    private var forgetItem: NSMenuItem?
    private var existingSetupWindow: SetupWindow?
    private var sendsWindow: SendsWindow?
    private lazy var teachWindow = TeachWindow(model: model)

    /// `serialOverride` is a console to use instead of the pet's USB port (`--serial PATH`).
    init(model: Model, serialOverride: String? = nil) {
        self.model = model
        wifi = WiFiSetup(helper: model.paths.helper, serialOverride: serialOverride)
        super.init()
        menu.delegate = self
        menu.autoenablesItems = false
        buildMenu()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        item.button?.image = Icon.image()
        item.button?.toolTip = "NestBox"
        item.menu = menu
        statusItem = item
        model.onChange = { [weak self] in self?.refresh() }
        model.log.onAppend = { [weak self] line in self?.sendsWindow?.append(line) }
        model.begin()
        wifi.onChange = { [weak self] in self?.refresh() }
        // A pet that just joined Wi-Fi over this Mac's USB cable is this Mac's pet.
        wifi.onOnline = { [weak self] pet in
            guard let model = self?.model, model.deviceOverride == nil else { return }
            model.pair(pet)
        }
        if !model.prefs.setupShown { showSetup() }
    }

    func applicationWillTerminate(_ notification: Notification) { model.stopAll() }

    func menuWillOpen(_ opened: NSMenu) {
        guard opened === menu else { return }
        model.fetchPets()
        model.scanNetwork()
        model.tick()
    }

    // MARK: - Building

    private func buildMenu() {
        for _ in 0..<Self.statusSlots { statusLines.append(addItem("", to: menu)) }
        menu.addItem(.separator())
        keyItem.target = self
        menu.addItem(keyItem)
        addSubmenu("Pets", petsMenu)
        addSubmenu("Connections", connectionsMenu)
        buildConnections()
        addItem("What It Sends…", to: menu, action: #selector(showSends))
        menu.addItem(.separator())
        addSubmenu("Pets on Your Network", networkMenu)
        addItem("Setup…", to: menu, action: #selector(showSetup))
        loginItem.target = self
        menu.addItem(loginItem)
        // For working on NestBox itself: the pet window (a pet on screen, no device needed) and test signals.
        let developer = NSMenu()
        addSubmenu("Developer", developer)
        petWindowItem.target = self
        petWindowItem.isHidden = model.paths.simulator == nil
        developer.addItem(petWindowItem)
        screenMenu.autoenablesItems = false
        screenItem = addItem("Screen", to: developer)
        screenItem?.submenu = screenMenu
        bridgeMenu.autoenablesItems = false
        addItem("Webhooks Through This Mac", to: developer).submenu = bridgeMenu
        bridgeStatus = addItem("", to: bridgeMenu)
        bridgeStatus?.isEnabled = false
        bridgeToggle = addItem("Receive Webhooks on This Mac", to: bridgeMenu, action: #selector(toggleBridge))
        bridgeMenu.addItem(.separator())
        bridgeCopies = [addItem("Copy This Mac's Webhook URL", to: bridgeMenu, action: #selector(copyBridgeURL)),
                        addItem("Copy This Mac's Secret", to: bridgeMenu, action: #selector(copyBridgeSecret)),
                        addItem("Copy Example Command", to: bridgeMenu, action: #selector(copyBridgeExample))]
        let quickTests = NSMenu()
        addItem("Quick Test", to: developer).submenu = quickTests
        for (index, test) in QuickTest.all.enumerated() {
            addItem(test.title, to: quickTests, action: #selector(runQuickTest(_:))).tag = index
        }
        addItem("Quit NestBox", to: menu, action: #selector(quit), key: "q")
        refresh()
    }

    /// Everything the pet hears from: this Mac, coding agents, Slack, webhooks. Slack and Webhooks are ticked while
    /// they reach the pet; below them, each sender the pet reports, ticked while it listens to it.
    private func buildConnections() {
        let mac = addItem("This Mac", to: connectionsMenu)
        mac.isEnabled = false
        for signal in ActivitySignal.allCases {
            let item = addItem(signal.title, to: connectionsMenu, action: #selector(toggleSignal(_:)))
            item.representedObject = signal
            item.toolTip = signal.detail
            item.indentationLevel = 1
        }
        connectionsMenu.addItem(.separator())
        addItem("Teach a Coding Agent…", to: connectionsMenu, action: #selector(showTeach))
        connectionsMenu.addItem(.separator())
        slackItem = addItem("Slack", to: connectionsMenu)
        slackItem?.submenu = slackMenu
        slackMenu.autoenablesItems = false
        slackStatus = addItem("", to: slackMenu)
        slackStatus?.isEnabled = false
        slackMenu.addItem(.separator())
        slackWants = [addItem("Direct Messages", to: slackMenu, action: #selector(toggleSlackWant(_:))),
                      addItem("Mentions", to: slackMenu, action: #selector(toggleSlackWant(_:))),
                      addItem("Replies in Your Threads", to: slackMenu, action: #selector(toggleSlackWant(_:)))]
        for (index, item) in slackWants.enumerated() { item.tag = index }
        slackWants[0].toolTip = "A letter and a bubble for 20 seconds when someone writes to you, in a DM or a "
            + "group DM"
        slackWants[1].toolTip = "A letter and a bubble for 20 seconds when someone @mentions you in a public channel"
        slackWants[2].toolTip = "A letter and a bubble for 20 seconds when someone replies in a thread you started "
            + "or wrote in"
        slackMenu.addItem(.separator())
        slackSetup = addItem("Connect Slack…", to: slackMenu, action: #selector(showSlack))
        slackDisconnect = addItem("Disconnect Slack…", to: slackMenu, action: #selector(disconnectSlack))
        webhooksItem = addItem("Webhooks", to: connectionsMenu)
        webhooksItem?.submenu = webhooksMenu
        webhooksMenu.autoenablesItems = false
        webhookStatus = addItem("", to: webhooksMenu)
        webhookStatus?.isEnabled = false
        webhookReceive = addItem("Receive Webhooks", to: webhooksMenu, action: #selector(toggleWebhooks))
        webhooksMenu.addItem(.separator())
        webhookCopies = [addItem("Copy Webhook URL", to: webhooksMenu, action: #selector(copyWebhookURL)),
                         addItem("Copy Secret", to: webhooksMenu, action: #selector(copyWebhookSecret)),
                         addItem("Copy Example Command", to: webhooksMenu, action: #selector(copyWebhookExample))]
        webhooksMenu.addItem(.separator())
        addItem("Access Key…", to: webhooksMenu, action: #selector(enterAccessKey))
        webhookRemove = addItem("Remove Webhooks…", to: webhooksMenu, action: #selector(removeWebhooks))
        let separator = NSMenuItem.separator()
        connectionsMenu.addItem(separator)
        sendersSeparator = separator
        sendersHeader = addItem("Senders", to: connectionsMenu)
        sendersHeader?.isEnabled = false
    }

    @discardableResult
    private func addItem(_ title: String, to menu: NSMenu, action: Selector? = nil, key: String = "") -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        item.target = self
        menu.addItem(item)
        return item
    }

    private func addSubmenu(_ title: String, _ submenu: NSMenu) {
        submenu.autoenablesItems = false
        addItem(title, to: menu).submenu = submenu
    }

    // MARK: - Refreshing

    private func refresh() {
        refreshStatusLines()
        keyItem.isHidden = !model.companion.needsKey && !model.isGettingKey
        keyItem.title = model.isGettingKey ? "Press the Pet's Button…" : "Pair with the Pet's Button…"
        keyItem.isEnabled = !model.isGettingKey
        petWindowItem.title = model.petWindow.isRunning ? "Hide Pet Window" : "Show Pet Window"
        petWindowItem.state = model.petWindow.isRunning ? .on : .off
        refreshScreens()
        for item in connectionsMenu.items {
            guard let signal = item.representedObject as? ActivitySignal else { continue }
            item.state = model.prefs.isEnabled(signal) ? .on : .off
        }
        refreshWebhooks()
        refreshSlack()
        refreshSenders()
        refreshPets()
        refreshNetwork()
        forgetItem?.isEnabled = model.petURL != nil
        refreshLogin()
        if let existingSetupWindow, existingSetupWindow.isOpen { existingSetupWindow.refresh() }
        if let slackWindow, slackWindow.isOpen { slackWindow.refresh() }
        statusItem?.button?.appearsDisabled = model.status == nil
        sendsWindow?.refreshHeader()
    }

    // The first line in semibold, the others secondary; unused slots hidden.
    private func refreshStatusLines() {
        let lines = StatusText.lines(for: model)
        while statusLines.count < lines.count {
            let item = NSMenuItem()
            statusLines.append(item)
            menu.insertItem(item, at: statusLines.count - 1)
        }
        let menuFontSize = NSFont.menuFont(ofSize: 0).pointSize
        for (index, item) in statusLines.enumerated() {
            item.isHidden = index >= lines.count
            item.isEnabled = false
            guard index < lines.count else { continue }
            let primary = index == 0
            let font = primary ? .systemFont(ofSize: menuFontSize, weight: .semibold) : NSFont.menuFont(ofSize: 12)
            let color = primary ? NSColor.labelColor : NSColor.secondaryLabelColor
            item.attributedTitle = NSAttributedString(string: lines[index],
                                                      attributes: [.font: font, .foregroundColor: color])
        }
    }

    // Submenus are rebuilt only when what they show changes, so an open one does not flicker.
    private struct PetsMenuState: Equatable {
        let petURL: URL?
        let current: String?
        let ids: [String]
    }

    private func refreshPets() {
        let state = PetsMenuState(petURL: model.petURL, current: model.petOut,
                                        ids: model.pets.map(\.id))
        guard state != shownPets else { return }
        shownPets = state
        petsMenu.removeAllItems()
        if model.pets.isEmpty {
            addItem(model.petURL == nil ? "No pet found" : "No pets listed", to: petsMenu).isEnabled = false
            return
        }
        for character in model.pets {
            let item = addItem(character.name, to: petsMenu, action: #selector(bringOut(_:)))
            item.representedObject = character.id
            item.state = character.id == model.petOut ? .on : .off
        }
    }

    private struct NetworkMenuState: Equatable {
        struct Entry: Equatable {
            let name: String
            let pet: String
            let location: String
            let url: URL
            let id: String
        }
        let scanning: Bool
        let pairedID: String?
        let companionURL: URL?
        let pets: [Entry]
    }

    // Every pet the companion can see, the paired one ticked. Choosing one pairs it by id, so it is found again
    // after a rename or a new address. A pet window is listed only while it runs or is the paired one.
    private func refreshNetwork() {
        let pets = model.networkPets.filter { !$0.isSimulator || model.petWindow.isRunning || model.isPaired($0) }
        let state = NetworkMenuState(scanning: model.isScanning, pairedID: model.prefs.pairedID,
                                     companionURL: model.companion.petURL,
                                     pets: pets.map {
                                         .init(name: $0.name, pet: $0.pet, location: $0.location, url: $0.url,
                                               id: $0.id)
                                     })
        guard state != shownNetwork else { return }
        shownNetwork = state
        networkMenu.removeAllItems()
        defer { addWiFiItems() }
        if let device = model.deviceOverride {
            addItem("Fixed to \(device)", to: networkMenu).isEnabled = false
            return
        }
        if pets.isEmpty {
            addItem(model.isScanning ? "Looking…" : "No pets found", to: networkMenu).isEnabled = false
            return
        }
        for pet in pets {
            let title = "\(pet.pet.isEmpty ? pet.name : pet.pet) at \(pet.location)"
                + (pet.isSimulator ? " (pet window)" : "")
            let item = addItem(title, to: networkMenu, action: #selector(pairPet(_:)))
            item.representedObject = pet.url
            item.toolTip = pet.name + (pet.id.isEmpty ? "" : ", id \(pet.id)")
            item.state = networkState(of: pet)
        }
        if model.isScanning { addItem("Looking…", to: networkMenu).isEnabled = false }
    }

    private func addWiFiItems() {
        networkMenu.addItem(.separator())
        addItem("Set Up Pet's Wi-Fi…", to: networkMenu, action: #selector(showWiFiSetup))
        forgetItem = addItem("Forget Pet's Wi-Fi…", to: networkMenu, action: #selector(forgetWiFi))
        forgetItem?.isEnabled = model.petURL != nil
    }

    // Mixed: in use by the companion but not paired yet.
    private func networkState(of pet: NetworkPet) -> NSControl.StateValue {
        if model.isPaired(pet) { return .on }
        return !model.prefs.hasPairing && model.companion.petURL == pet.url ? .mixed : .off
    }

    private func refreshLogin() {
        switch LoginItem.status {
        case .enabled:
            loginItem.title = "Start at Login"
            loginItem.state = .on
        case .requiresApproval:
            loginItem.title = "Start at Login (approve in System Settings)"
            loginItem.state = .mixed
        case .disabled:
            loginItem.title = "Start at Login"
            loginItem.state = .off
        }
    }

    // MARK: - Actions

    @objc private func togglePetWindow() { model.togglePetWindow() }

    @objc private func pickScreen(_ item: NSMenuItem) {
        guard let id = item.representedObject as? String else { return }
        model.setPetScreen(id)
    }

    // One item per screen the simulator lists, the one in use ticked.
    private func refreshScreens() {
        let screens = model.petScreens
        screenItem?.isHidden = model.paths.simulator == nil || screens.isEmpty
        if screenMenu.items.map({ $0.representedObject as? String }) != screens.map({ $0.id }) {
            screenMenu.removeAllItems()
            for screen in screens {
                let item = addItem(screen.name, to: screenMenu, action: #selector(pickScreen(_:)))
                item.representedObject = screen.id
            }
        }
        for item in screenMenu.items {
            item.state = (item.representedObject as? String) == model.petScreen ? .on : .off
        }
    }

    // Rebuilt only when the names change, so an open menu does not flicker; hidden until the pet reports one.
    private func refreshSenders() {
        let senders = model.status?.senders ?? []
        if senderItems.map(\.title) != senders.map(\.from) {
            for item in senderItems { connectionsMenu.removeItem(item) }
            senderItems = senders.map { sender in
                let item = addItem(sender.from, to: connectionsMenu, action: #selector(toggleSender(_:)))
                item.indentationLevel = 1
                item.toolTip = "Untick it and the pet ignores \(sender.from)"
                return item
            }
        }
        for (item, sender) in zip(senderItems, senders) { item.state = sender.ignored ? .off : .on }
        sendersHeader?.isHidden = senders.isEmpty
        sendersSeparator?.isHidden = senders.isEmpty
    }

    @objc private func toggleSender(_ item: NSMenuItem) {
        model.setListening(to: item.title, item.state != .on) { status in
            if status != HTTPStatus.ok { NSSound.beep() }
        }
    }

    @objc private func toggleSignal(_ item: NSMenuItem) {
        guard let signal = item.representedObject as? ActivitySignal else { return }
        model.setSignal(signal, enabled: !model.prefs.isEnabled(signal))
    }

    @objc private func bringOut(_ item: NSMenuItem) {
        guard let id = item.representedObject as? String else { return }
        model.bringOut(id) { status in if status != HTTPStatus.ok { NSSound.beep() } }
    }

    @objc private func pairPet(_ item: NSMenuItem) {
        guard let url = item.representedObject as? URL, let pet = model.networkPets.first(where: { $0.url == url })
        else { return }
        model.pair(pet)
    }

    @objc private func runQuickTest(_ item: NSMenuItem) {
        model.send(QuickTest.all[item.tag]) { status, _ in if status != HTTPStatus.ok { NSSound.beep() } }
    }

    private func refreshWebhooks() {
        let hooks = model.petWebhooks
        let host = hooks.relay.host ?? "the relay"
        let state = model.webhookState
        var relay: RelayStatus?
        if case .pet(let r) = state { relay = r }
        webhookStatus?.title = state == .unsupported && model.webhooks.state == .connected
            ? "Through this Mac for now: your pet's firmware can't hold them yet"
            : Self.words(for: state, host: host)
        webhooksItem?.state = relay?.on == true && relay?.state == "connected"
            || model.prefs.webhooksThroughMac && model.webhooks.state == .connected ? .on : .off
        webhookReceive?.isEnabled = relay?.set == true
        webhookReceive?.state = relay.map { $0.on && $0.state != "replaced" } == true ? .on : .off
        for item in webhookCopies { item.isEnabled = hooks.hasAccessKey && hooks.url != nil }
        webhookRemove?.isEnabled = hooks.hasAccessKey || hooks.url != nil

        let bridge = model.webhooks
        let bridgeWords: [WebhookBridge.State: String] = [
            .off: "Off: enter the access key under Webhooks", .paused: "Off",
            .connecting: "Connecting to \(host)…", .connected: "Connected: webhooks to this Mac's URL reach the pet",
            .refused: "The access key was not accepted", .full: "Every mailbox for this access key is in use",
            .removed: "This Mac's webhook URL was removed", .replaced: "Another Mac uses this Mac's webhook URL",
            .unreachable: "Can't reach \(host); trying again"]
        bridgeStatus?.title = bridgeWords[bridge.state] ?? ""
        bridgeToggle?.isEnabled = hooks.hasAccessKey
        bridgeToggle?.state = model.prefs.webhooksThroughMac ? .on : .off
        for item in bridgeCopies { item.isEnabled = model.prefs.webhooksThroughMac && bridge.url != nil }
    }

    private static func words(for state: PetWebhooks.State, host: String) -> String {
        switch state {
        case .noAccessKey: return "Off: enter the access key you were given"
        case .noPet: return "No pet answers"
        case .unsupported: return "Your pet's firmware can't take webhooks yet: update it"
        case .notPaired: return "Webhooks go to your paired pet: pick it under Pets on Your Network"
        case .needsKey: return "Your pet needs this Mac's key first: Pair with the Pet's Button"
        case .refused: return "The access key was not accepted"
        case .full: return "Every mailbox for this access key is in use"
        case .handing: return "Giving your pet its webhook keys…"
        case .pet(let relay):
            if !relay.set { return "Giving your pet its webhook keys…" }
            if !relay.on { return "Paused: webhooks are turned away until you tick Receive Webhooks" }
            switch relay.state {
            case "connected": return "Connected: webhooks reach your pet, even with this Mac asleep"
            case "refused": return "The relay turned your pet away; it tries again hourly. Enter the access key again"
            case "replaced": return "Another pet took this webhook URL: tick Receive Webhooks to take it back"
            default: return "Your pet is connecting to \(host)…"
            }
        }
    }

    private func refreshSlack() {
        let slack = model.slack
        slackStatus?.title = Self.words(for: slack)
        let wants = model.prefs.slackWants
        for (item, on) in zip(slackWants, [wants.directMessages, wants.mentions, wants.threads]) {
            item.state = on ? .on : .off
            item.isEnabled = slack.hasTokens
        }
        slackItem?.state = slack.state == .connected ? .on : .off
        slackSetup?.title = slack.hasTokens ? "Slack Setup…" : "Connect Slack…"
        slackDisconnect?.isHidden = !slack.hasTokens
    }

    static func words(for slack: SlackWatch) -> String {
        switch slack.state {
        case .off: return "Off: Connect Slack to start"
        case .idle: return "Off: nothing below is ticked"
        case .checking, .connecting: return "Connecting to Slack…"
        case .unreachable: return "Can't reach Slack; trying again"
        case .refused(let words): return words
        case .connected:
            if let until = slack.quietUntil {
                return "Do Not Disturb until \(SlackWatch.clock(until)): your pet stays quiet"
            }
            if slack.shared {
                return "Another Mac uses this Slack app too: each gets only some messages"
            }
            return slack.account.map { "Connected as \($0.name) in \($0.team)" } ?? "Connected"
        }
    }

    @objc private func toggleSlackWant(_ item: NSMenuItem) {
        var wants = model.prefs.slackWants
        switch item.tag {
        case 0: wants.directMessages.toggle()
        case 1: wants.mentions.toggle()
        default: wants.threads.toggle()
        }
        model.setSlackWants(wants)
        refresh()
    }

    @objc private func showSlack() {
        let window = slackWindow ?? SlackWindow(model: model)
        slackWindow = window
        window.show()
    }

    @objc private func disconnectSlack() {
        let ask = NSAlert()
        ask.messageText = "Disconnect Slack?"
        ask.informativeText = "Your pet stops hearing from Slack, and both tokens are forgotten on this Mac. The app "
            + "stays in your Slack workspace: to remove it there too, open it at api.slack.com/apps and delete it. "
            + "Connect again with new tokens any time."
        ask.addButton(withTitle: "Disconnect")
        ask.addButton(withTitle: "Cancel")
        NSApp.comeToFront()
        guard ask.runModal() == .alertFirstButtonReturn else { return }
        model.slack.disconnect()
        refresh()
    }

    @objc private func enterAccessKey() {
        let ask = NSAlert()
        ask.messageText = "Webhooks access key"
        ask.informativeText = "Webhooks reach your pet through the NestBox relay, which sits in the middle: send "
            + "nothing personal or sensitive in them. Paste the access key you were given."
        let field = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 320, height: 24))
        field.placeholderString = model.petWebhooks.hasAccessKey ? "A key is saved; paste a new one" : "nbr_…"
        ask.accessoryView = field
        ask.addButton(withTitle: "Save")
        ask.addButton(withTitle: "Cancel")
        NSApp.comeToFront()
        ask.window.initialFirstResponder = field
        guard ask.runModal() == .alertFirstButtonReturn else { return }
        let key = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !key.isEmpty else {
            let empty = NSAlert()
            empty.messageText = "Nothing was saved"
            empty.informativeText = "The field was empty. Paste the access key (⌘V) and press Save."
            NSApp.comeToFront()
            empty.runModal()
            return
        }
        model.setWebhooksAccessKey(key)
        refresh()
    }

    // Ticking it on a pet that another took the URL from takes it back.
    @objc private func toggleWebhooks() {
        guard case .pet(let relay) = model.webhookState else { return }
        model.setReceivingWebhooks(!relay.on || relay.state == "replaced")
        refresh()
    }

    @objc private func toggleBridge() {
        model.setWebhooksThroughMac(!model.prefs.webhooksThroughMac)
        refresh()
    }

    @objc private func removeWebhooks() {
        let ask = NSAlert()
        ask.messageText = "Remove webhooks?"
        ask.informativeText = "Your pet's webhook URL is deleted from the relay, so anything still sending to it gets "
            + "nothing through; the pet forgets its keys, and the URL and secret are forgotten here, the access key "
            + "too unless Developer › Webhooks Through This Mac is on. To use webhooks again, enter the access key: "
            + "you get a new URL and secret."
        ask.addButton(withTitle: "Remove")
        ask.addButton(withTitle: "Cancel")
        NSApp.comeToFront()
        guard ask.runModal() == .alertFirstButtonReturn else { return }
        model.petWebhooks.remove(pet: model.petURL, keepAccessKey: model.prefs.webhooksThroughMac) { [weak self] status in
            self?.model.log.append("-- webhooks removed: \(status.map(String.init) ?? "nothing to remove")")
            self?.refresh()
            guard let status, ![204, 404, 401].contains(status) else { return }
            let alert = NSAlert()
            alert.messageText = "Nothing was removed"
            alert.informativeText = status == 0 ? "The relay didn't answer. Check the network and try again."
                : "The relay said no (\(status)). Try again in a minute."
            NSApp.comeToFront()
            alert.runModal()
        }
    }

    @objc private func copyWebhookURL() { copy(model.petWebhooks.url?.absoluteString) }
    @objc private func copyWebhookSecret() { copy(model.petWebhooks.secret) }
    @objc private func copyWebhookExample() { copy(example(model.petWebhooks.url, model.petWebhooks.secret)) }
    @objc private func copyBridgeURL() { copy(model.webhooks.url?.absoluteString) }
    @objc private func copyBridgeSecret() { copy(model.webhooks.secret) }
    @objc private func copyBridgeExample() { copy(example(model.webhooks.url, model.webhooks.secret)) }

    private func example(_ url: URL?, _ secret: String?) -> String? {
        guard let url, let secret else { return nil }
        return "curl -H 'Content-Type: application/json' -H 'X-NestBox-Secret: \(secret)' "
            + "-d '{\"from\":\"ci\",\"state\":\"done\",\"line\":\"main is green\"}' \(url.absoluteString)"
    }

    private func copy(_ text: String?) {
        guard let text else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    @objc private func toggleLogin() {
        LoginItem.setEnabled(!LoginItem.isEnabled)
        refreshLogin()
    }

    @objc private func getKey() {
        model.getKey { paired, detail in
            let alert = NSAlert()
            alert.messageText = paired ? "Paired" : "Not paired"
            alert.informativeText = paired ? "This Mac has the pet's key now."
                : detail.isEmpty ? "The pet didn't give its key. Try again, and press its button when it asks." : detail
            NSApp.comeToFront()
            alert.runModal()
        }
    }

    @objc private func showSends() {
        let window = sendsWindow ?? SendsWindow(model: model)
        sendsWindow = window
        window.show()
    }

    @objc private func showTeach() { teachWindow.show() }
    @objc private func showSetup() { setupWindow().show() }
    @objc private func showWiFiSetup() { setupWindow().show(wifi: true) }

    @objc private func forgetWiFi() {
        let ask = NSAlert()
        ask.messageText = "Forget your pet's Wi-Fi?"
        ask.informativeText = "It leaves this network and shows a code on its screen. Scan it with your phone to "
            + "set up Wi-Fi again."
        ask.addButton(withTitle: "Forget Wi-Fi")
        ask.addButton(withTitle: "Cancel")
        NSApp.comeToFront()
        guard ask.runModal() == .alertFirstButtonReturn else { return }
        model.forgetWiFi { status in
            guard status != HTTPStatus.ok else { return }
            let alert = NSAlert()
            alert.messageText = "Your pet kept its Wi-Fi"
            alert.informativeText = status == HTTPStatus.unauthorized
                ? "This Mac needs the pet's key first: Pair with the Pet's Button."
                : status == HTTPStatus.noAnswer ? "Your pet didn't answer. Is it on and on this network?"
                : "Your pet said no (\(status))."
            NSApp.comeToFront()
            alert.runModal()
        }
    }
    @objc private func quit() { NSApp.terminate(nil) }

    private func setupWindow() -> SetupWindow {
        if let existingSetupWindow { return existingSetupWindow }
        let window = SetupWindow(model: model, wifi: wifi)
        window.onChange = { [weak self] in self?.refresh() }
        window.onTeach = { [weak self] in self?.showTeach() }
        existingSetupWindow = window
        return window
    }
}
