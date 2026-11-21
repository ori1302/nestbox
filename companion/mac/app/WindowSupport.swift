import AppKit

extension NSWindow {
    /// A window the app keeps for reuse: closing only hides it.
    static func reusable(title: String, size: NSSize, style: NSWindow.StyleMask) -> NSWindow {
        let window = NSWindow(contentRect: NSRect(origin: .zero, size: size), styleMask: style, backing: .buffered,
                              defer: false)
        window.title = title
        window.isReleasedWhenClosed = false
        return window
    }

    func bringToFront() {
        NSApp.comeToFront()
        makeKeyAndOrderFront(nil)
    }
}

extension NSApplication {
    /// Activates the app, which as a menu bar app is never active by itself.
    func comeToFront() {
        if #available(macOS 14, *) {
            activate()
        } else {
            activate(ignoringOtherApps: true)
        }
    }
}

extension NSStackView {
    convenience init(views: [NSView], orientation: NSUserInterfaceLayoutOrientation) {
        self.init(views: views)
        self.orientation = orientation
    }
}

/// A read-only monospaced text view in a bordered scroll view.
@MainActor
func makeReadOnlyTextView(inset: CGFloat) -> (NSScrollView, NSTextView) {
    let scroll = NSTextView.scrollableTextView()
    scroll.borderType = .bezelBorder
    let text = scroll.documentView as! NSTextView    // documented to be an NSTextView
    text.isEditable = false
    text.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
    text.textContainerInset = NSSize(width: inset, height: inset)
    return (scroll, text)
}

/// A content view laid out top to bottom: a wrapping label, a text area, and a row of buttons, `margin` from
/// the edges.
@MainActor
func textPanel(label: NSTextField, scroll: NSScrollView, buttons: NSStackView, margin: CGFloat,
               spacing: CGFloat) -> NSView {
    let stack = NSStackView(views: [label, scroll, buttons], orientation: .vertical)
    stack.alignment = .leading
    stack.spacing = spacing
    stack.edgeInsets = NSEdgeInsets(top: margin, left: margin, bottom: margin, right: margin)
    stack.translatesAutoresizingMaskIntoConstraints = false
    let content = NSView()
    content.addSubview(stack)
    NSLayoutConstraint.activate([
        stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
        stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
        stack.topAnchor.constraint(equalTo: content.topAnchor),
        stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
        scroll.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: margin),
        scroll.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -margin),
        buttons.leadingAnchor.constraint(equalTo: scroll.leadingAnchor),
        buttons.trailingAnchor.constraint(equalTo: scroll.trailingAnchor),
        label.widthAnchor.constraint(equalTo: scroll.widthAnchor),
    ])
    return content
}

@MainActor
func copyToPasteboard(_ text: String) {
    NSPasteboard.general.clearContents()
    NSPasteboard.general.setString(text, forType: .string)
}
