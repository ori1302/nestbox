import AppKit

struct LaunchOptions {
    private static let valued: Set = [
        "--home", "--device", "--name", "--sim-args", "--render-icon", "--app-icon", "--serial",
    ]

    private var values: [String: String] = [:]

    /// Options without a value, and unknown arguments (such as Finder's -psn_...), are ignored.
    init(arguments: [String]) {
        var i = 0
        while i < arguments.count {
            if Self.valued.contains(arguments[i]), i + 1 < arguments.count {
                values[arguments[i]] = arguments[i + 1]
                i += 2
            } else {
                i += 1
            }
        }
    }

    var home: String? { values["--home"] }
    var device: String? { values["--device"] }
    var name: String? { values["--name"] }
    var simulatorArguments: String? { values["--sim-args"] }
    var iconOutput: String? { values["--render-icon"] }
    var appIconFolder: String? { values["--app-icon"] }
    var serial: String? { values["--serial"] }
}

@MainActor
enum AppLauncher {
    static func run(arguments: [String]) -> Never {
        let options = LaunchOptions(arguments: arguments)
        if let output = options.iconOutput { exit(Icon.writePNG(output) ? 0 : 1) }
        if let folder = options.appIconFolder { exit(Icon.writeAppIconSet(folder) ? 0 : 1) }

        let prefs = Preferences(defaults: .standard)
        let home = options.home ?? ProcessInfo.processInfo.environment["NESTBOX_HOME"] ?? prefs.home
        let paths = AppPaths.find(home: home)
        let simulatorArguments = (options.simulatorArguments ?? prefs.simulatorArguments ?? "")
            .split(separator: " ").map(String.init)

        // A second copy would stop the first one's children as leftovers of a crash.
        if isAnotherInstanceRunning() {
            print("NestBox is already running (see the menu bar)")
            exit(0)
        }

        let childrenFile = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".nestbox/menubar-children")
        let device = options.device.flatMap(URL.init(string:)) ?? prefs.device
        let model = Model(prefs: prefs, paths: paths, device: device, name: options.name ?? prefs.petName,
                          simulatorArguments: simulatorArguments, childrenFile: childrenFile)
        let signalSources = stopChildrenOnTermination(model)

        let app = NSApplication.shared
        let menuBar = MenuBar(model: model, serialOverride: options.serial)
        app.delegate = menuBar
        app.setActivationPolicy(.accessory)
        withExtendedLifetime((menuBar, signalSources)) { app.run() }    // the delegate property is weak
        exit(0)
    }

    private static func isAnotherInstanceRunning() -> Bool {
        guard let id = Bundle.main.bundleIdentifier else { return false }
        return NSRunningApplication.runningApplications(withBundleIdentifier: id)
            .contains { $0.processIdentifier != getpid() }
    }

    // SIGTERM (logout, kill), SIGINT and SIGHUP stop the children before the app exits.
    private static func stopChildrenOnTermination(_ model: Model) -> [DispatchSourceSignal] {
        [SIGTERM, SIGINT, SIGHUP].map { signalNumber in
            signal(signalNumber, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: signalNumber, queue: .main)
            source.setEventHandler {
                MainActor.assumeIsolated { model.stopAll() }    // the source delivers on the main queue
                exit(0)
            }
            source.resume()
            return source
        }
    }
}
