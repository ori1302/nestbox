import Foundation

/// A pet on the network, as `nestbox find --machine` lists it.
struct NetworkPet: Decodable, Equatable {
    let name: String
    let url: URL
    let id: String
    let host: String
    let pet: String
    let isSimulator: Bool

    private enum CodingKeys: String, CodingKey {
        case name, url, id, host, pet
        case isSimulator = "simulator"
    }

    /// Where the pet is, for display: its own host name, else the URL's host.
    var location: String { !host.isEmpty ? host : url.host ?? url.absoluteString }

    /// The pets in `find --machine` output.
    static func parse(_ output: String) -> [NetworkPet] {
        output.split(separator: "\n").compactMap { MachineLine.decode(NetworkPet.self, from: String($0)) }
    }

    /// Runs `nestbox find --machine`, blocking for the 2-4 s of a Bonjour browse.
    static func scan(helper: URL, environment: [String: String]? = nil) -> [NetworkPet] {
        guard let run = runProcess(helper, ["find", "--machine"], environment: environment) else { return [] }
        return parse(run.output)
    }
}
