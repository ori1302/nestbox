import Foundation

/// Getting the pet's key: `nestbox pair` asks the pet, the user presses its button, and the helper keeps the key in
/// ~/.nestbox/device for itself and for hooks.
enum Pairing {
    /// Runs `nestbox pair --device URL --machine`, blocking for up to a minute while the pet waits for its button.
    /// True once paired; otherwise why not.
    static func run(helper: URL, device: URL, environment: [String: String]? = nil) -> (paired: Bool, detail: String) {
        guard let run = runProcess(helper, ["pair", "--device", device.absoluteString, "--machine"],
                                   environment: environment) else { return (false, "the helper did not start") }
        var result: (Bool, String) = (false, "the pet did not answer")
        for line in run.output.split(separator: "\n") {
            switch MachineLine.decode(HelperEvent.self, from: String(line)) {
            case .pairDone?: result = (true, "")
            case .pairFailed(let detail)?: result = (false, detail)
            default: break
            }
        }
        return result
    }
}
