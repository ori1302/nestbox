import AppKit

/// Connect Slack: three steps in Slack's own pages (make the app from NestBox's manifest, make an app-level token,
/// install it), then the two tokens, checked with Slack before they are saved.
@MainActor
final class SlackWindow: NSObject, NSWindowDelegate {
    private static let width: CGFloat = 500
    private static let textWidth: CGFloat = 420

    let window: NSWindow
    private let model: Model
    private let appToken = NSSecureTextField()
    private let userToken = NSSecureTextField()
    private let spinner = NSProgressIndicator()
    private let status = NSTextField(wrappingLabelWithString: "")
    private let connectButton = NSButton()
    private var problem: String?
    private var checking = false

    init(model: Model) {
        self.model = model
        window = .reusable(title: "Connect Slack", size: NSSize(width: Self.width, height: 520),
                           style: [.titled, .closable])
        super.init()
        window.delegate = self
        buildContent()
    }

    var isOpen: Bool { window.isVisible }

    func show() {
        problem = nil
        refresh()
        window.bringToFront()
        window.makeFirstResponder(model.slack.hasTokens ? nil : appToken)
    }

    func refresh() {
        let saved = model.slack.hasTokens
        appToken.placeholderString = saved ? "Saved. Paste a new one to replace it." : "xapp-…"
        userToken.placeholderString = saved ? "Saved. Paste a new one to replace it." : "xoxp-…"
        connectButton.isEnabled = !checking
        connectButton.title = saved && model.slack.state == .connected ? "Check Again" : "Connect"
        if checking {
            spinner.isHidden = false
            spinner.startAnimation(nil)
        } else {
            spinner.stopAnimation(nil)
            spinner.isHidden = true
        }
        let (text, color) = statusLine()
        status.stringValue = text
        status.textColor = color
        if let content = window.contentView { window.setContentSize(content.fittingSize) }
    }

    private func statusLine() -> (String, NSColor) {
        if checking { return ("Asking Slack…", .secondaryLabelColor) }
        if let problem { return (problem, .systemRed) }
        let slack = model.slack
        switch slack.state {
        case .connected:
            let who = slack.account.map { "Connected as \($0.name) in \($0.team)." } ?? "Connected."
            return (who + " Ask someone to send you a DM to try it.", .systemGreen)
        case .refused(let words): return (words, .systemRed)
        case .connecting, .checking: return ("Connecting to Slack…", .secondaryLabelColor)
        case .unreachable: return ("Can't reach Slack; trying again.", .secondaryLabelColor)
        case .idle:
            return ("Saved. Nothing is ticked under Connections › Slack, so it stays off.", .secondaryLabelColor)
        case .off: return ("Paste both tokens, then Connect.", .secondaryLabelColor)
        }
    }

    // MARK: - Layout

    private func buildContent() {
        let steps = NSStackView(views: [
            step(1, "Create the app in Slack",
                 "Slack opens with NestBox's app filled in. Pick your workspace, click Next, then Create.",
                 extra: createRow()),
            step(2, "Make an app-level token",
                 "On the app's Basic Information page, find App-Level Tokens and click Generate Token and Scopes. "
                 + "Name it NestBox, add the scope connections:write, click Generate and copy the token.",
                 extra: field(appToken)),
            step(3, "Install it and copy your token",
                 "Open Install App in the sidebar, click the Install button, then Allow. Copy the User OAuth "
                 + "Token.",
                 extra: field(userToken)),
        ], orientation: .vertical)
        steps.alignment = .leading
        steps.spacing = 18

        let all = NSStackView(views: [header(), steps, separator(), footer()], orientation: .vertical)
        all.alignment = .leading
        all.spacing = 20
        all.edgeInsets = NSEdgeInsets(top: 22, left: 24, bottom: 18, right: 24)
        all.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(all)
        NSLayoutConstraint.activate([
            all.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            all.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            all.topAnchor.constraint(equalTo: content.topAnchor),
            all.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            content.widthAnchor.constraint(equalToConstant: Self.width),
        ])
        window.contentView = content
        window.center()
    }

    private func header() -> NSView {
        let icon = NSImageView(image: Self.iconImage(side: 56))
        icon.wantsLayer = true
        icon.layer?.cornerRadius = 12
        icon.layer?.masksToBounds = true
        icon.widthAnchor.constraint(equalToConstant: 56).isActive = true
        icon.heightAnchor.constraint(equalToConstant: 56).isActive = true
        let title = NSTextField(labelWithString: "Slack on your pet")
        title.font = .systemFont(ofSize: 20, weight: .semibold)
        let about = secondary("When someone writes to you or mentions you, your pet looks up, a letter pops in over "
                              + "its head, and a bubble says who and where for 20 seconds. Never the message. The "
                              + "connection runs from this Mac to Slack, through no NestBox server.", width: 360)
        let titles = NSStackView(views: [title, about], orientation: .vertical)
        titles.alignment = .leading
        titles.spacing = 4
        let row = NSStackView(views: [icon, titles])
        row.alignment = .top
        row.spacing = 16
        return row
    }

    private func createRow() -> NSView {
        let create = NSButton(title: "Create App in Slack", target: self, action: #selector(createApp))
        create.bezelStyle = .rounded
        let icon = NSButton(title: "Save Icon…", target: self, action: #selector(saveIcon))
        icon.bezelStyle = .rounded
        let buttons = NSStackView(views: [create, icon])
        buttons.spacing = 8
        let note = secondary("Save Icon puts NestBox's icon in Downloads. To use it, upload it on Basic Information, "
                             + "under Display Information.", width: Self.textWidth - 28)
        let column = NSStackView(views: [buttons, note], orientation: .vertical)
        column.alignment = .leading
        column.spacing = 6
        return column
    }

    private func field(_ field: NSSecureTextField) -> NSView {
        field.widthAnchor.constraint(equalToConstant: Self.textWidth - 28).isActive = true
        field.target = self
        field.action = #selector(connect)
        return field
    }

    /// A numbered step: its number in a circle, a title, what to do in Slack, then its control.
    private func step(_ number: Int, _ title: String, _ detail: String, extra: NSView) -> NSView {
        let badge = StepNumber(number)
        let titleLabel = NSTextField(labelWithString: title)
        titleLabel.font = .systemFont(ofSize: 13, weight: .semibold)
        let text = NSStackView(views: [titleLabel, secondary(detail, width: Self.textWidth - 28), extra],
                               orientation: .vertical)
        text.alignment = .leading
        text.spacing = 6
        text.setCustomSpacing(10, after: text.views[1])
        let row = NSStackView(views: [badge, text])
        row.alignment = .top
        row.spacing = 10
        return row
    }

    private func separator() -> NSView {
        let line = NSBox()
        line.boxType = .separator
        line.widthAnchor.constraint(equalToConstant: Self.width - 48).isActive = true
        return line
    }

    private func footer() -> NSView {
        spinner.style = .spinning
        spinner.controlSize = .small
        spinner.isHidden = true
        status.font = .systemFont(ofSize: 12)
        status.preferredMaxLayoutWidth = 270
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.bezelStyle = .rounded
        close.keyEquivalent = "\u{1b}"
        connectButton.title = "Connect"
        connectButton.bezelStyle = .rounded
        connectButton.keyEquivalent = "\r"
        connectButton.target = self
        connectButton.action = #selector(connect)
        let left = NSStackView(views: [spinner, status])
        left.alignment = .firstBaseline
        left.spacing = 6
        let row = NSStackView(views: [left, NSView(), close, connectButton])
        row.alignment = .centerY
        row.widthAnchor.constraint(equalToConstant: Self.width - 48).isActive = true
        return row
    }

    private func secondary(_ text: String, width: CGFloat) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: 12)
        label.textColor = .secondaryLabelColor
        label.preferredMaxLayoutWidth = width
        label.widthAnchor.constraint(equalToConstant: width).isActive = true
        return label
    }

    static func iconImage(side: CGFloat) -> NSImage {
        let image = NSImage(size: NSSize(width: side, height: side))
        if let rep = Icon.slackIcon(side: Int(side * 2)) { image.addRepresentation(rep) }
        return image
    }

    // MARK: - Actions

    @objc private func createApp() { NSWorkspace.shared.open(Slack.createAppURL) }

    @objc private func saveIcon() {
        let downloads = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser
        let file = downloads.appendingPathComponent("NestBox Slack icon.png")
        guard let data = Icon.slackIcon()?.representation(using: .png, properties: [:]),
              (try? data.write(to: file)) != nil else {
            NSSound.beep()
            return
        }
        NSWorkspace.shared.activateFileViewerSelecting([file])
    }

    @objc private func connect() {
        guard !checking else { return }
        problem = nil
        checking = true
        refresh()
        model.slack.connect(appToken: appToken.stringValue, userToken: userToken.stringValue) { [weak self] problem in
            guard let self else { return }
            self.checking = false
            self.problem = problem
            if problem == nil {
                self.appToken.stringValue = ""
                self.userToken.stringValue = ""
            }
            self.refresh()
        }
    }

    @objc private func closeWindow() { window.close() }

    func windowWillClose(_ notification: Notification) {
        appToken.stringValue = ""
        userToken.stringValue = ""
        problem = nil
    }
}

/// A step's number in a filled circle, in the label colour, so it follows light and dark.
private final class StepNumber: NSView {
    private let number: Int

    init(_ number: Int) {
        self.number = number
        super.init(frame: NSRect(x: 0, y: 0, width: 20, height: 20))
        widthAnchor.constraint(equalToConstant: 20).isActive = true
        heightAnchor.constraint(equalToConstant: 20).isActive = true
    }

    required init?(coder: NSCoder) { nil }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.labelColor.setFill()
        NSBezierPath(ovalIn: bounds).fill()
        let text = NSAttributedString(string: "\(number)", attributes: [
            .font: NSFont.systemFont(ofSize: 11, weight: .bold), .foregroundColor: NSColor.windowBackgroundColor])
        let size = text.size()
        text.draw(at: NSPoint(x: (bounds.width - size.width) / 2, y: (bounds.height - size.height) / 2))
    }
}
