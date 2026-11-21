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
    private let signalsMenu = NSMenu()
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
        addSubmenu("Pets on Your Network", networkMenu)
        addSubmenu("Pets", petsMenu)
        addSubmenu("Signals", signalsMenu)
        for signal in ActivitySignal.allCases {
            let item = addItem(signal.title, to: signalsMenu, action: #selector(toggleSignal(_:)))
            item.representedObject = signal
            item.toolTip = signal.detail
        }
        addItem("What It Sends…", to: menu, action: #selector(showSends))
        menu.addItem(.separator())
        addItem("Teach a Coding Agent…", to: menu, action: #selector(showTeach))
        loginItem.target = self
        menu.addItem(loginItem)
        addItem("Set Up Pet's Wi-Fi…", to: menu, action: #selector(showWiFiSetup))
        forgetItem = addItem("Forget Pet's Wi-Fi…", to: menu, action: #selector(forgetWiFi))
        addItem("Setup…", to: menu, action: #selector(showSetup))
        // For working on NestBox itself: the pet window (a pet on screen, no device needed) and test signals.
        let developer = NSMenu()
        addSubmenu("Developer", developer)
        petWindowItem.target = self
        petWindowItem.isHidden = model.paths.simulator == nil
        developer.addItem(petWindowItem)
        screenMenu.autoenablesItems = false
        screenItem = addItem("Screen", to: developer)
        screenItem?.submenu = screenMenu
        let quickTests = NSMenu()
        addItem("Quick Test", to: developer).submenu = quickTests
        for (index, test) in QuickTest.all.enumerated() {
            addItem(test.title, to: quickTests, action: #selector(runQuickTest(_:))).tag = index
        }
        addItem("Quit NestBox", to: menu, action: #selector(quit), key: "q")
        refresh()
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
        forgetItem?.isEnabled = model.petURL != nil
        petWindowItem.title = model.petWindow.isRunning ? "Hide Pet Window" : "Show Pet Window"
        petWindowItem.state = model.petWindow.isRunning ? .on : .off
        refreshScreens()
        for item in signalsMenu.items {
            guard let signal = item.representedObject as? ActivitySignal else { continue }
            item.state = model.prefs.isEnabled(signal) ? .on : .off
        }
        refreshPets()
        refreshNetwork()
        refreshLogin()
        if let existingSetupWindow, existingSetupWindow.isOpen { existingSetupWindow.refresh() }
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
