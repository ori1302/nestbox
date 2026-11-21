import AppKit

/// The view drawn on its window's background, in the current light or dark appearance.
@MainActor
func drawn(_ view: NSView?) -> NSBitmapImageRep? {
    guard let view else { return nil }
    view.layoutSubtreeIfNeeded()
    guard let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return nil }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    view.effectiveAppearance.performAsCurrentDrawingAppearance {
        NSColor.windowBackgroundColor.setFill()
        view.bounds.fill()
    }
    NSGraphicsContext.restoreGraphicsState()
    view.cacheDisplay(in: view.bounds, to: rep)
    return rep
}

func savePNG(_ rep: NSBitmapImageRep?, to directory: String, as name: String) -> Bool {
    guard let data = rep?.representation(using: .png, properties: [:]) else { return false }
    return (try? data.write(to: URL(fileURLWithPath: directory).appendingPathComponent(name))) != nil
}

/// The setup window as it would show, without showing it.
@MainActor
func snapshot(_ window: SetupWindow, wifi: WiFiSetup, showingWiFi: Bool = false) -> NSBitmapImageRep? {
    window.showsWiFiSetup = showingWiFi
    wifi.scan()
    window.refresh()
    return drawn(window.window.contentView)
}

@MainActor
func snapshot(_ window: SendsWindow) -> NSBitmapImageRep? {
    window.refreshHeader()
    window.reloadLog()
    return drawn(window.window.contentView)
}

/// The menu as indented text: ✓ for checked items, - for mixed, "(info)" for disabled ones.
@MainActor
func menuText(_ menu: NSMenu, indent: String = "") -> String {
    menu.items.filter { !$0.isHidden }.map { item -> String in
        if item.isSeparatorItem { return indent + "----" }
        let mark = item.state == .on ? "✓ " : item.state == .mixed ? "- " : "  "
        let line = indent + mark + item.title + (item.isEnabled ? "" : "   (info)")
        guard let submenu = item.submenu, !submenu.items.isEmpty else { return line }
        return line + "\n" + menuText(submenu, indent: indent + "    ")
    }.joined(separator: "\n")
}

/// A value the test thread waits on while the main actor sets it.
final class Box<T>: @unchecked Sendable {
    private let lock = NSLock()
    private var stored: T

    init(_ value: T) { stored = value }

    var value: T {
        get { lock.withLock { stored } }
        set { lock.withLock { stored = newValue } }
    }
}
