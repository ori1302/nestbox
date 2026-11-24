import AppKit

/// "What It Sends": the companion's --print output, live. Each line is the JSON body of a POST /v1/user, then
/// "->", the pet's HTTP status and its reply.
@MainActor
final class SendsWindow: NSObject {
    private static let linesShown = 500
    private static let maxCharacters = 400_000
    private static let keptFromCharacter = 100_000    // trimming keeps the text after the first newline past this
    private static let atBottomThreshold: Float = 0.98

    let window: NSWindow
    private let model: Model
    private let textView: NSTextView
    private let header = NSTextField(wrappingLabelWithString: "")
    private var isPaused = false

    init(model: Model) {
        self.model = model
        window = .reusable(title: "What NestBox sends", size: NSSize(width: 820, height: 440),
                           style: [.titled, .closable, .resizable, .miniaturizable])
        window.minSize = NSSize(width: 480, height: 240)
        let (scroll, text) = makeReadOnlyTextView(inset: 4)
        scroll.hasVerticalScroller = true
        text.isRichText = true
        textView = text
        super.init()

        header.font = .systemFont(ofSize: 12)
        header.textColor = .secondaryLabelColor
        let pause = NSButton(checkboxWithTitle: "Pause", target: self, action: #selector(togglePause(_:)))
        let clear = NSButton(title: "Clear", target: self, action: #selector(clear))
        let copy = NSButton(title: "Copy All", target: self, action: #selector(copyAll))
        let buttons = NSStackView(views: [pause, NSView(), copy, clear])
        window.contentView = textPanel(label: header, scroll: scroll, buttons: buttons, margin: 12, spacing: 8)
        window.center()
        window.setFrameAutosaveName("SendsWindow")
    }

    var isOpen: Bool { window.isVisible }

    func show() {
        refreshHeader()
        reloadLog()
        window.bringToFront()
    }

    func append(_ line: String) {
        guard isOpen, !isPaused else { return }
        let atBottom = (textView.enclosingScrollView?.verticalScroller?.floatValue ?? 1) > Self.atBottomThreshold
        textView.textStorage?.append(styled(line))
        trim()
        if atBottom { textView.scrollToEndOfDocument(nil) }
    }

    func refreshHeader() {
        let activity = ActivitySignal.allCases.filter { $0 != .music && $0 != .calendar && model.prefs.isEnabled($0) }
        let sending = activity.map { $0.title.lowercased() }
        let base = model.petURL?.absoluteString ?? ""
        let found = model.petURL == nil ? " (no pet found yet)" : ""
        header.stringValue = "Each line is one message exactly as the companion sends it: POST \(base)/v1/user\(found), "
            + "then -> the pet's status and reply.\n"
            + "Sending: " + (sending.isEmpty ? "nothing but a regular \"still here\"" : sending.joined(separator: ", "))
            + ", and the Mac's local time."
            + (model.prefs.isEnabled(.music)
                ? "\nMusic: the song playing, as POST \(base)/v1/tell from \"music\"." : "")
            + (model.status?.relay?.state == "connected"
                ? "\nWebhooks go from the relay straight to the pet, so they don't show here." : "")
            + (model.webhooks.state == .connected
                ? "\nWebhooks through this Mac: tells that came to this Mac's own webhook URL with the right secret, "
                    + "as POST \(base)/v1/tell."
                : "")
            + (model.slack.hasTokens
                ? "\nSlack: who wrote to you and where (never the message), as POST \(base)/v1/tell from \"slack\"."
                : "")
            + (model.prefs.isEnabled(.calendar)
                ? "\nCalendar: \"in a meeting\" or \"meeting soon\" with a countdown, as POST \(base)/v1/tell from \"calendar\"."
                : "")
    }

    func reloadLog() {
        let all = NSMutableAttributedString()
        for line in model.log.lines.suffix(Self.linesShown) { all.append(styled(line)) }
        textView.textStorage?.setAttributedString(all)
        textView.scrollToEndOfDocument(nil)
    }

    // The message in full colour; the pet's reply and the app's own notes dimmer.
    private func styled(_ line: String) -> NSAttributedString {
        let mono = NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
        let out = NSMutableAttributedString()
        func add(_ text: Substring, _ color: NSColor) {
            out.append(NSAttributedString(string: String(text), attributes: [.font: mono, .foregroundColor: color]))
        }
        if let arrow = line.range(of: " -> ") {
            add(line[..<arrow.lowerBound], .labelColor)
            add(line[arrow.lowerBound...], .secondaryLabelColor)
        } else {
            add(line[...], .tertiaryLabelColor)
        }
        out.append(NSAttributedString(string: "\n", attributes: [.font: mono]))
        return out
    }

    private func trim() {
        guard let storage = textView.textStorage, storage.length > Self.maxCharacters else { return }
        let from = Self.keptFromCharacter
        let searched = NSRange(location: from, length: storage.length - from)
        let cut = (storage.string as NSString).range(of: "\n", range: searched).location
        if cut != NSNotFound { storage.deleteCharacters(in: NSRange(location: 0, length: cut + 1)) }
    }

    @objc private func togglePause(_ button: NSButton) {
        isPaused = button.state == .on
        if !isPaused { reloadLog() }
    }

    @objc private func clear() { textView.string = "" }

    @objc private func copyAll() { copyToPasteboard(textView.string) }
}
