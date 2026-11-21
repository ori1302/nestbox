import Foundation

/// A screen the pet window can show the pet on, as the simulator lists them (nestbox-sim --displays).
struct PetScreen: Decodable, Sendable, Equatable {
    let id: String
    let name: String
    let w: Int
    let h: Int

    /// The simulator's screens, the default first; empty if it can't be asked. Blocks: call it off the main thread.
    static func list(simulator: URL) -> [PetScreen] {
        guard let result = runProcess(simulator, ["--displays"]), result.status == 0 else { return [] }
        return (try? JSONDecoder().decode([PetScreen].self, from: Data(result.output.utf8))) ?? []
    }
}
