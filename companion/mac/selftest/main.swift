// Self-test for the menu bar app: runs the app's own code paths against a built NestBox.app and a pet started
// for the test. Exits 1 if anything failed.
//
//   nestbox-menubar-selftest --device URL [--app build/NestBox.app] [--name NAME] [--sim-args "ARGS"]
//                            [--shots DIR] [--home DIR]
import AppKit

var values: [String: String] = [:]
var remaining = CommandLine.arguments.dropFirst()
while let option = remaining.popFirst() {
    if let value = remaining.popFirst() { values[option] = value }
}
guard let device = values["--device"] else {
    print("usage: nestbox-menubar-selftest --device URL [--app PATH] [--name NAME] [--sim-args ARGS] "
          + "[--shots DIR] [--home DIR]")
    exit(2)
}
let app = URL(fileURLWithPath: values["--app"] ?? "build/NestBox.app").standardizedFileURL
let home = values["--home"] ?? ProcessInfo.processInfo.environment["NESTBOX_HOME"]
let test = SelfTest(app: app, paths: AppPaths.find(home: home, appBundle: app), device: device, name: values["--name"],
                    simulatorArguments: (values["--sim-args"] ?? "").split(separator: " ").map(String.init),
                    shots: values["--shots"])
_ = NSApplication.shared
Thread.detachNewThread { exit(test.run()) }
RunLoop.main.run()    // serves the main queue and the model's timers while the test runs on its own thread
