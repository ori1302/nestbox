import AppKit

/// The setup window: finds the pet or puts a new one on Wi-Fi over USB, and offers Start at Login and
/// Teach a Coding Agent. Shown on the first run and from "Setup…" and "Set Up Pet's Wi-Fi…".
@MainActor
final class SetupWindow: NSObject, NSWindowDelegate {
    private static let width: CGFloat = 400
    private static let usbScanInterval: TimeInterval = 2

    let window: NSWindow
    private let model: Model
    private let wifi: WiFiSetup
    var onChange: (() -> Void)?
    var onTeach: (() -> Void)?
    /// Opened from "Set Up Pet's Wi-Fi…": the Wi-Fi fields show even when a pet is already on the network.
    var showsWiFiSetup = false

    private let found = NSTextField(wrappingLabelWithString: "")
    private let spinner = NSProgressIndicator()
    private let login = NSSwitch()
    private let ssid = NSTextField()
    private let password = NSSecureTextField()    // cleared as soon as it is sent and when the window closes
    private let joinButton = NSButton()
    private let wifiNote = NSTextField(wrappingLabelWithString: "")
    private var wifiBox = NSView()
    private var usbScanTimer: Timer?

    init(model: Model, wifi: WiFiSetup) {
        self.model = model
        self.wifi = wifi
        window = .reusable(title: "NestBox", size: NSSize(width: Self.width, height: 260), style: [.titled, .closable])
        super.init()
        window.delegate = self
        buildContent()
    }

    var isOpen: Bool { window.isVisible }

    func show(wifi showWiFi: Bool = false) {
        showsWiFiSetup = showWiFi
        startUSBScan()
        model.scanNetwork()
        refresh()
        window.bringToFront()
    }

    func refresh() {
        let (message, isLooking) = petMessage()
        found.stringValue = message
        let busy = wifi.isBusy
        spinner.isHidden = !isLooking && !busy
        if isLooking || busy { spinner.startAnimation(nil) } else { spinner.stopAnimation(nil) }
        refreshWiFiBox()
        login.state = LoginItem.isEnabled ? .on : .off
        if let content = window.contentView { window.setContentSize(content.fittingSize) }
    }

    // The headline, and whether the window is still waiting for a pet to show up.
    private func petMessage() -> (String, Bool) {
        let host = model.statusSource?.host ?? "your network"
        if case .online(let url) = wifi.phase {
            return ("Your pet is on Wi-Fi at \(url.host ?? url.absoluteString)", false)
        }
        if model.needsPick {
            return ("Several pets are on your network. Pick yours: goose menu › Pets on Your Network.", false)
        }
        if deviceOnNetwork, let pet = model.status?.pet { return ("Found \(model.title(of: pet)) at \(host)", false) }
        if wifi.port != nil { return ("Your pet is plugged in. Let's put it on Wi-Fi.", false) }
        if let pet = model.status?.pet { return ("Found \(model.title(of: pet)) in the pet window at \(host)", false) }
        return ("Plug your pet into this Mac with its USB cable.", true)
    }

    // A device (not the simulator) answers, so it is on Wi-Fi already.
    private var deviceOnNetwork: Bool {
        guard let status = model.status, status.device != nil else { return false }
        return !status.isSimulator
    }

    private func refreshWiFiBox() {
        let plugged = wifi.port != nil, busy = wifi.isBusy
        let wanted = showsWiFiSetup || busy
        wifiBox.isHidden = wifi.phase.isOnline || (!plugged && !wanted) || (deviceOnNetwork && !wanted)
        for control in [ssid, password, joinButton] as [NSControl] { control.isEnabled = plugged && !busy }
        switch wifi.phase {
        case .idle:
            if !plugged {
                wifiNote.stringValue = "Plug your pet in with its USB cable to set its Wi-Fi."
            } else if MacWiFi.isOn5GHz {
                wifiNote.stringValue = "Your pet needs a 2.4 GHz network; most routers offer one under the same name."
            } else {
                wifiNote.stringValue = ""
            }
        case .sending: wifiNote.stringValue = "Sending it to your pet…"
        case .joining(let network): wifiNote.stringValue = "Your pet is joining “\(network)”…"
        case .online: wifiNote.stringValue = ""
        case .failed(let reason): wifiNote.stringValue = reason
        }
        wifiNote.isHidden = wifiNote.stringValue.isEmpty
    }

    private func startUSBScan() {
        wifi.scan()
        guard usbScanTimer == nil else { return }
        let timer = Timer(timeInterval: Self.usbScanInterval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.wifi.scan() }    // the timer runs on the main run loop
        }
        RunLoop.main.add(timer, forMode: .common)
        usbScanTimer = timer
    }

    // MARK: - Layout

    private func buildContent() {
        wifiBox = makeWiFiBox()
        let footer = makeFooter()
        let all = NSStackView(views: [makeHeader(), wifiBox, makeOptions(), footer], orientation: .vertical)
        all.alignment = .leading
        all.spacing = 20
        all.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 18, right: 20)
        all.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(all)
        NSLayoutConstraint.activate([
            all.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            all.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            all.topAnchor.constraint(equalTo: content.topAnchor),
            all.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            content.widthAnchor.constraint(equalToConstant: Self.width),
            footer.widthAnchor.constraint(equalTo: all.widthAnchor, constant: -40),
        ])
        window.contentView = content
        window.center()
    }

    private func makeHeader() -> NSView {
        let icon = NSImageView(image: Icon.image(height: 40, template: true))
        icon.contentTintColor = .labelColor
        let title = NSTextField(labelWithString: "NestBox")
        title.font = .systemFont(ofSize: 20, weight: .semibold)
        found.font = .systemFont(ofSize: 13)
        found.preferredMaxLayoutWidth = 270
        spinner.style = .spinning
        spinner.controlSize = .small
        let status = NSStackView(views: [spinner, found])
        status.alignment = .firstBaseline
        let titles = NSStackView(views: [title, status], orientation: .vertical)
        titles.alignment = .leading
        let header = NSStackView(views: [icon, titles])
        header.spacing = 14
        return header
    }

    private func makeWiFiBox() -> NSView {
        ssid.placeholderString = "Wi-Fi name"
        ssid.stringValue = MacWiFi.currentSSID ?? ""
        password.placeholderString = "Password"
        for field in [ssid, password] { field.widthAnchor.constraint(equalToConstant: 240).isActive = true }
        joinButton.title = "Put It on Wi-Fi"
        joinButton.bezelStyle = .rounded
        joinButton.target = self
        joinButton.action = #selector(join)
        password.target = self
        password.action = #selector(join)
        wifiNote.font = .systemFont(ofSize: 11)
        wifiNote.textColor = .secondaryLabelColor
        wifiNote.preferredMaxLayoutWidth = 340
        let grid = NSStackView(views: [formRow("Wi-Fi", ssid), formRow("Password", password), formRow("", joinButton)],
                               orientation: .vertical)
        grid.alignment = .leading
        grid.spacing = 8
        let box = NSStackView(views: [grid, wifiNote], orientation: .vertical)
        box.alignment = .leading
        box.spacing = 8
        return box
    }

    private func formRow(_ title: String, _ control: NSView) -> NSView {
        let label = NSTextField(labelWithString: title)
        label.alignment = .right
        label.widthAnchor.constraint(equalToConstant: 70).isActive = true
        return NSStackView(views: [label, control])
    }

    private func makeOptions() -> NSView {
        login.target = self
        login.action = #selector(toggleLogin)
        let teach = NSButton(title: "Teach a Coding Agent…", target: self, action: #selector(teachAgent))
        teach.bezelStyle = .rounded
        let teachNote = secondaryLabel("Your pet's own instructions for Claude Code, Codex, Cursor or any coding "
                                       + "agent, so your pet shows what the agent is doing and calls you when it "
                                       + "needs you.")
        teachNote.preferredMaxLayoutWidth = 340
        let teachRow = NSStackView(views: [teach, teachNote], orientation: .vertical)
        teachRow.alignment = .leading
        teachRow.spacing = 4
        let rows = NSStackView(views: [switchRow(login, "Start at login", "NestBox starts when you log in."), teachRow],
                               orientation: .vertical)
        rows.alignment = .leading
        rows.spacing = 12
        return rows
    }

    private func makeFooter() -> NSView {
        let hint = secondaryLabel("NestBox lives in the menu bar: the goose at the top of your screen.")
        let done = NSButton(title: "Done", target: self, action: #selector(finish))
        let footer = NSStackView(views: [hint, done])
        footer.alignment = .centerY
        return footer
    }

    private func switchRow(_ control: NSSwitch, _ title: String, _ detail: String) -> NSView {
        let titleLabel = NSTextField(labelWithString: title)
        titleLabel.font = .systemFont(ofSize: 13, weight: .medium)
        let detailLabel = secondaryLabel(detail)
        detailLabel.preferredMaxLayoutWidth = 280
        let text = NSStackView(views: [titleLabel, detailLabel], orientation: .vertical)
        text.alignment = .leading
        text.spacing = 2
        let row = NSStackView(views: [control, text])
        row.alignment = .top
        row.spacing = 10
        return row
    }

    private func secondaryLabel(_ text: String) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: 11)
        label.textColor = .secondaryLabelColor
        return label
    }

    // MARK: - Actions

    @objc private func join() {
        let secret = password.stringValue
        password.stringValue = ""
        wifi.join(ssid: ssid.stringValue, password: secret)
    }

    @objc private func toggleLogin() {
        LoginItem.setEnabled(login.state == .on)
        refresh()
        onChange?()
    }

    @objc private func teachAgent() { onTeach?() }

    // Done and the close button both count as having seen setup; quitting with the window open does not.
    @objc private func finish() {
        model.prefs.setupShown = true
        window.close()
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        model.prefs.setupShown = true
        return true
    }

    func windowWillClose(_ notification: Notification) {
        usbScanTimer?.invalidate()
        usbScanTimer = nil
        password.stringValue = ""
        onChange?()
    }
}
