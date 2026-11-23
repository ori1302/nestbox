import Foundation

/// A signal the companion sends; each can be switched on or off in the Signals menu.
enum ActivitySignal: String, CaseIterable {
    case keys, idle, lock, music, calendar

    var title: String {
        switch self {
        case .keys: return "Typing rate"
        case .idle: return "Idle time"
        case .lock: return "Screen lock"
        case .music: return "Music"
        case .calendar: return "Calendar"
        }
    }

    var detail: String {
        switch self {
        case .keys: return "key presses since the last report, a count only"
        case .idle: return "seconds since the last key or mouse input"
        case .lock: return "whether the screen is locked"
        case .music: return "the song and artist playing in Spotify or Apple Music, and how far into it when Spotify says"
        case .calendar: return "\"in a meeting\" and \"meeting soon\" with a countdown, from the Mac's calendars; never a title or a name"
        }
    }

    /// Whether it is on before the user changes it. Music and Calendar are not: they read more than counts.
    var startsOn: Bool { self != .music && self != .calendar }

    /// Sent by the helper (a restart applies it); Calendar is the app's own.
    var isHelpers: Bool { self != .calendar }

    /// The helper's flag for the signal's setting, if that setting needs one.
    func flag(enabled: Bool) -> String? {
        switch (self, enabled) {
        case (.music, true): return "--music"
        case (.music, false): return nil
        case (.calendar, _): return nil
        case (_, true): return nil
        case (_, false): return "--no-" + rawValue
        }
    }
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
        static let receivesWebhooks = "webhooks.receive"

        static func signal(_ signal: ActivitySignal) -> String { "signal." + signal.rawValue }
    }

    private let defaults: UserDefaults

    init(defaults: UserDefaults) {
        self.defaults = defaults
        let starting = ActivitySignal.allCases.map { (Key.signal($0), $0.startsOn) }
        defaults.register(defaults: Dictionary(uniqueKeysWithValues: starting))
        defaults.register(defaults: [Key.receivesWebhooks: true])
    }

    /// Webhooks are received (not paused); the key, URL and secret are in the keychain either way.
    var receivesWebhooks: Bool {
        get { defaults.bool(forKey: Key.receivesWebhooks) }
        set { defaults.set(newValue, forKey: Key.receivesWebhooks) }
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
