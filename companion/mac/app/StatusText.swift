/// The informational lines at the top of the menu.
@MainActor
enum StatusText {
    static func lines(for model: Model) -> [String] {
        var lines = petLines(model)
        lines.append(companionLine(model))
        if model.needsPick { lines.append("Several pets found: pick yours in Pets on Your Network") }
        if !model.otherCompanions.isEmpty {
            let pids = model.otherCompanions.map(String.init).joined(separator: ", ")
            lines.append("Another nestbox is also running (pid \(pids))")
        }
        if let note = model.petWindow.note { lines.append("Pet window: \(note)") }
        return lines
    }

    private static func petLines(_ model: Model) -> [String] {
        guard let status = model.status else {
            if let base = model.petURL { return ["No answer from \(base.absoluteString)"] }
            return ["Looking for a pet on the network…"]
        }
        let title = status.pet.map(model.title(of:)) ?? "?"
        var lines = ["\(title) · \(status.doing ?? "?")"]
        if let why = status.why, !why.isEmpty { lines.append("Why: \(why)") }
        let kind = status.isSimulator ? "Pet window" : "Device"
        lines.append("\(kind) “\(status.device ?? "pet")” at \(model.statusSource?.absoluteString ?? "")")
        return lines
    }

    private static func companionLine(_ model: Model) -> String {
        let companion = model.companion
        var line = "Companion: " + (companion.isRunning && companion.isConnected ? "connected" : companion.note)
        let off = ActivitySignal.allCases.filter { !model.prefs.isEnabled($0) }
        if !off.isEmpty { line += " · off: " + off.map { $0.title.lowercased() }.joined(separator: ", ") }
        return line
    }
}
