import AppKit

/// "Teach a Coding Agent": the pet's onboarding page for coding agents (GET /agents), to paste into an agent or
/// to give it as a link. The app does not edit any agent's configuration.
@MainActor
final class TeachWindow: NSObject {
    let window: NSWindow
    private let model: Model
    private let textView: NSTextView
    private let note = NSTextField(wrappingLabelWithString: "")
    private let copyText = NSButton(title: "Copy", target: nil, action: nil)
    private let copyLink = NSButton(title: "Copy Link", target: nil, action: nil)
    /// What Copy copies.
    private(set) var page = ""
    /// What Copy Link copies.
    private(set) var link: String?

    init(model: Model) {
        self.model = model
        window = .reusable(title: "Teach a Coding Agent", size: NSSize(width: 640, height: 480),
                           style: [.titled, .closable, .resizable])
        window.minSize = NSSize(width: 420, height: 300)
        let (scroll, text) = makeReadOnlyTextView(inset: 6)
        textView = text
        super.init()

        note.font = .systemFont(ofSize: 13)
        copyText.target = self
        copyText.action = #selector(copyPage)
        copyText.keyEquivalent = "\r"
        copyLink.target = self
        copyLink.action = #selector(copyPageLink)
        let buttons = NSStackView(views: [NSView(), copyLink, copyText])
        window.contentView = textPanel(label: note, scroll: scroll, buttons: buttons, margin: 14, spacing: 10)
        window.center()
        window.setFrameAutosaveName("TeachWindow")
    }

    func show() {
        load()
        window.bringToFront()
    }

    /// Fetches the page from the pet in use.
    func load(completion: (() -> Void)? = nil) {
        guard let base = model.petURL else {
            display("", link: nil,
                    note: "No pet found yet. Once your pet is on Wi-Fi, its page for coding agents shows here.")
            completion?()
            return
        }
        display("", link: nil, note: "Asking your pet…")
        PetClient.getText("/agents", from: base) { [weak self] status, body in
            guard let self else { return }
            if status == HTTPStatus.ok, let body, !body.isEmpty {
                self.display(body, link: PetClient.url(base, "/agents").absoluteString,
                             note: "Paste this into Claude Code, Codex, Cursor or any coding agent and ask it to "
                                 + "connect itself to your pet.")
            } else {
                self.display("", link: nil,
                             note: "Your pet at \(base.absoluteString) didn't answer. Is it on and on Wi-Fi?")
            }
            completion?()
        }
    }

    private func display(_ page: String, link: String?, note text: String) {
        self.page = page
        self.link = link
        textView.string = page
        note.stringValue = text
        copyText.isEnabled = !page.isEmpty
        copyLink.isEnabled = link != nil
        copyLink.toolTip = link
    }

    @objc private func copyPage() { copyToPasteboard(page) }

    @objc private func copyPageLink() {
        guard let link else { return }
        copyToPasteboard(link)
    }
}
