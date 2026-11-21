import AppKit
import ServiceManagement

/// Start at Login, through SMAppService.
@MainActor
enum LoginItem {
    enum Status { case enabled, requiresApproval, disabled }

    static var status: Status {
        switch SMAppService.mainApp.status {
        case .enabled: return .enabled
        case .requiresApproval: return .requiresApproval
        default: return .disabled
        }
    }

    static var isEnabled: Bool { status == .enabled }

    /// Registers or unregisters the app; opens System Settings when macOS wants the user's approval.
    static func setEnabled(_ enabled: Bool) {
        let service = SMAppService.mainApp
        do {
            if service.status == .requiresApproval { SMAppService.openSystemSettingsLoginItems(); return }
            if enabled { try service.register() } else { try service.unregister() }
            if service.status == .requiresApproval { SMAppService.openSystemSettingsLoginItems() }
        } catch {
            showError(error)
        }
    }

    private static func showError(_ error: Error) {
        let alert = NSAlert()
        alert.messageText = "Could not change Start at Login"
        alert.informativeText = "\(error.localizedDescription)\n\nmacOS may refuse login items for an app that is "
            + "not in Applications. Move NestBox there and try again, or add it in System Settings › General › "
            + "Login Items."
        NSApp.comeToFront()
        alert.runModal()
    }
}
