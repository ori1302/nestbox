import Foundation

/// An activity signal the companion sends; each can be switched off in the Signals menu.
enum ActivitySignal: String, CaseIterable {
    case keys, idle, lock

    var title: String {
        switch self {
        case .keys: return "Typing rate"
        case .idle: return "Idle time"
        case .lock: return "Screen lock"
        }
    }

    var detail: String {
        switch self {
        case .keys: return "key presses since the last report, a count only"
        case .idle: return "seconds since the last key or mouse input"
        case .lock: return "whether the screen is locked"
        }
    }

    /// The helper's flag that switches the signal off.
    var disableFlag: String { "--no-" + rawValue }
}

/// The app's settings, kept in user defaults (thread-safe, as UserDefaults is).
final class Preferences: @unchecked Sendable {
    private enum Key {
        static let pairedID = "pairedId"
        static let setupShown = "setupShown"
        static let device = "device"
        static let petName = "name"
        static let simulatorArguments = "simulatorArgs"
        static let home = "home"
        static let petScreen = "petScreen"

        static func signal(_ signal: ActivitySignal) -> String { "signal." + signal.rawValue }
    }

    private let defaults: UserDefaults

    init(defaults: UserDefaults) {
        self.defaults = defaults
        let allOn = ActivitySignal.allCases.map { (Key.signal($0), true) }
        defaults.register(defaults: Dictionary(uniqueKeysWithValues: allOn))
    }

    func isEnabled(_ signal: ActivitySignal) -> Bool { defaults.bool(forKey: Key.signal(signal)) }
    func setEnabled(_ signal: ActivitySignal, _ enabled: Bool) { defaults.set(enabled, forKey: Key.signal(signal)) }

    /// The paired pet's Bonjour TXT id.
    var pairedID: String? {
        get { defaults.string(forKey: Key.pairedID) }
        set { defaults.set(newValue, forKey: Key.pairedID) }
    }

    var hasPairing: Bool { pairedID != nil }

    /// The screen the pet window shows the pet on (a simulator display id); nil: the simulator's default.
    var petScreen: String? {
        get { defaults.string(forKey: Key.petScreen) }
        set { defaults.set(newValue, forKey: Key.petScreen) }
    }

    var setupShown: Bool {
        get { defaults.bool(forKey: Key.setupShown) }
        set { defaults.set(newValue, forKey: Key.setupShown) }
    }

    /// Launch settings, also accepted as command-line options.
    var device: URL? { defaults.string(forKey: Key.device).flatMap(URL.init(string:)) }
    var petName: String? { defaults.string(forKey: Key.petName) }
    var simulatorArguments: String? { defaults.string(forKey: Key.simulatorArguments) }
    var home: String? { defaults.string(forKey: Key.home) }
}
