import Foundation

/// Runs a program to the end and returns its exit status and output, or nil if it didn't start. Blocks: call it off
/// the main thread. `mergeErrors`: its standard error goes into the output too, else it is dropped.
func runProcess(_ executable: URL, _ arguments: [String], environment: [String: String]? = nil,
                mergeErrors: Bool = false) -> (status: Int32, output: String)? {
    let process = Process()
    process.executableURL = executable
    process.arguments = arguments
    if let environment { process.environment = environment }
    let output = Pipe()
    process.standardOutput = output
    process.standardError = mergeErrors ? output : FileHandle.nullDevice
    guard (try? process.run()) != nil else { return nil }
    let data = output.fileHandleForReading.readDataToEndOfFile()
    process.waitUntilExit()
    return (process.terminationStatus, String(decoding: data, as: UTF8.self))
}
