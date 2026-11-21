import Foundation

/// Where the app finds the companion helper and the developer pet window.
struct AppPaths {
    static let bundledHelper = "Contents/Helpers/nestbox"

    /// The companion helper: the app's own copy; an unbundled build falls back to the project's.
    let helper: URL
    /// The developer pet window, if there is one: build/NestBox Pet.app (scripts/build_app.sh makes it, so macOS
    /// shows the goose icon and its name), else build/nestbox-sim.
    let simulator: URL?

    /// Looks in `home`/build, then next to the app (when it sits in build/), then ~/nestbox/build, where a clone
    /// in the home folder builds.
    static func find(home: String?, appBundle: URL = Bundle.main.bundleURL) -> AppPaths {
        let fm = FileManager.default
        var buildDirectories: [URL] = []
        if let home {
            let root = URL(fileURLWithPath: (home as NSString).expandingTildeInPath)
            buildDirectories.append(root.appendingPathComponent("build"))
        }
        buildDirectories.append(appBundle.deletingLastPathComponent())
        buildDirectories.append(fm.homeDirectoryForCurrentUser.appendingPathComponent("nestbox/build"))
        func executable(_ name: String) -> URL? {
            buildDirectories.map { $0.appendingPathComponent(name) }.first { fm.isExecutableFile(atPath: $0.path) }
        }
        let bundled = appBundle.appendingPathComponent(bundledHelper)
        let helper = fm.isExecutableFile(atPath: bundled.path) ? bundled : executable("nestbox") ?? bundled
        let simulator = executable("NestBox Pet.app/Contents/MacOS/nestbox-sim") ?? executable("nestbox-sim")
        return AppPaths(helper: helper, simulator: simulator)
    }
}
