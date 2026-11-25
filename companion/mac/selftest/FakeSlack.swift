import Foundation
import Network

/// A stand-in for Slack on 127.0.0.1: the Web API methods the app calls, over HTTP, and Socket Mode, a WebSocket
/// listener that says hello, records the app's acknowledgements and sends what the test pushes.
final class FakeSlack: @unchecked Sendable {
    static let appToken = "xapp-1-A0TEST-1-fake", userToken = "xoxp-fake-user-token", me = "UADA"

    private let queue = DispatchQueue(label: "fake-slack")
    private let lock = NSLock()
    private var http: NWListener?
    private var socket: NWListener?
    private var clients: [NWConnection] = []
    private var acknowledged: [String] = []
    private var helloCount = 0
    private var dndJSON = #"{"dnd_enabled":false,"snooze_enabled":false}"#
    private var apiPort: UInt16 = 0
    private var socketPort: UInt16 = 0

    init?() {
        let local = NWEndpoint.hostPort(host: .ipv4(.loopback), port: .any)
        let plain = NWParameters.tcp
        plain.requiredLocalEndpoint = local
        let ws = NWParameters.tcp
        ws.requiredLocalEndpoint = local
        let options = NWProtocolWebSocket.Options()
        options.autoReplyPing = true
        ws.defaultProtocolStack.applicationProtocols.insert(options, at: 0)
        guard let http = try? NWListener(using: plain), let socket = try? NWListener(using: ws) else { return nil }
        self.http = http
        self.socket = socket
        http.newConnectionHandler = { [weak self] c in self?.serve(c) }
        socket.newConnectionHandler = { [weak self] c in self?.accept(c) }
        guard let a = Self.ready(http, queue), let b = Self.ready(socket, queue) else { return nil }
        apiPort = a
        socketPort = b
    }

    var api: URL { URL(string: "http://127.0.0.1:\(apiPort)/api/")! }
    var acks: [String] { lock.withLock { acknowledged } }
    var hellos: Int { lock.withLock { helloCount } }
    var connected: Int { lock.withLock { clients.count } }

    /// An events_api envelope around `event` (JSON), with this event id.
    func event(_ id: String, _ event: String) {
        push(#"{"envelope_id":"env-\#(id)","type":"events_api","accepts_response_payload":false,"#
             + #""payload":{"type":"event_callback","event_id":"\#(id)","event":\#(event)}}"#)
    }

    func disconnect(_ reason: String) { push(#"{"type":"disconnect","reason":"\#(reason)"}"#) }

    func stop() {
        http?.cancel()
        socket?.cancel()
        for c in lock.withLock({ clients }) { c.cancel() }
    }

    private func push(_ text: String) {
        for c in lock.withLock({ clients }) { send(text, to: c) }
    }

    private static func ready(_ listener: NWListener, _ queue: DispatchQueue) -> UInt16? {
        let done = DispatchSemaphore(value: 0)
        listener.stateUpdateHandler = { state in
            switch state {
            case .ready, .failed, .cancelled: done.signal()
            default: break
            }
        }
        listener.start(queue: queue)
        _ = done.wait(timeout: .now() + 5)
        return listener.port?.rawValue
    }

    // MARK: - Socket Mode

    private func accept(_ c: NWConnection) {
        c.stateUpdateHandler = { [weak self] state in
            guard let self else { return }
            switch state {
            case .ready:
                self.lock.withLock {
                    self.clients.append(c)
                    self.helloCount += 1
                }
                let count = self.connected
                self.send(#"{"type":"hello","num_connections":\#(count),"connection_info":{"app_id":"A0TEST"},"#
                          + #""debug_info":{"host":"fake","approximate_connection_time":3600}}"#, to: c)
            case .failed, .cancelled:
                self.lock.withLock { self.clients.removeAll { $0 === c } }
            default: break
            }
        }
        c.start(queue: queue)
        receive(c)
    }

    private func receive(_ c: NWConnection) {
        c.receiveMessage { [weak self] data, context, _, error in
            guard let self else { return }
            let closing = (context?.protocolMetadata(definition: NWProtocolWebSocket.definition)
                as? NWProtocolWebSocket.Metadata)?.opcode == .close
            if let data, let ack = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
               let id = ack["envelope_id"] as? String {
                self.lock.withLock { self.acknowledged.append(id) }
            }
            if error != nil || closing {
                self.lock.withLock { self.clients.removeAll { $0 === c } }
                c.cancel()
            } else {
                self.receive(c)
            }
        }
    }

    private func send(_ text: String, to c: NWConnection) {
        let metadata = NWProtocolWebSocket.Metadata(opcode: .text)
        let context = NWConnection.ContentContext(identifier: "text", metadata: [metadata])
        c.send(content: Data(text.utf8), contentContext: context, isComplete: true, completion: .idempotent)
    }

    // MARK: - Web API

    private func serve(_ c: NWConnection) {
        c.start(queue: queue)
        read(c, Data())
    }

    private func read(_ c: NWConnection, _ got: Data) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, done, error in
            guard let self else { return }
            var all = got
            if let data { all.append(data) }
            if let end = all.range(of: Data("\r\n\r\n".utf8)) {
                let head = String(decoding: all[..<end.lowerBound], as: UTF8.self)
                let length = head.split(separator: "\r\n").compactMap { line -> Int? in
                    let parts = line.split(separator: ":", maxSplits: 1)
                    guard parts.count == 2, parts[0].lowercased() == "content-length" else { return nil }
                    return Int(parts[1].trimmingCharacters(in: .whitespaces))
                }.first ?? 0
                let body = all[end.upperBound...]
                if body.count >= length {
                    self.respond(c, head: head, body: String(decoding: body.prefix(length), as: UTF8.self))
                    return
                }
            }
            if done || error != nil { return c.cancel() }
            self.read(c, all)
        }
    }

    private func respond(_ c: NWConnection, head: String, body: String) {
        let lines = head.split(separator: "\r\n")
        let path = lines.first?.split(separator: " ").dropFirst().first.map(String.init) ?? ""
        let auth = lines.first { $0.lowercased().hasPrefix("authorization:") }
            .map { $0.dropFirst("authorization:".count).trimmingCharacters(in: .whitespaces) } ?? ""
        var arguments: [String: String] = [:]
        for pair in body.split(separator: "&") {
            let kv = pair.split(separator: "=", maxSplits: 1).map { String($0).removingPercentEncoding ?? "" }
            if kv.count == 2 { arguments[kv[0]] = kv[1] }
        }
        let json = answer(String(path.split(separator: "/").last ?? ""), auth, arguments)
        let reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: \(json.utf8.count)\r\n"
            + "Connection: close\r\n\r\n" + json
        c.send(content: Data(reply.utf8), completion: .contentProcessed { _ in c.cancel() })
    }

    private func answer(_ method: String, _ auth: String, _ arguments: [String: String]) -> String {
        let refused = #"{"ok":false,"error":"invalid_auth"}"#
        if method == "apps.connections.open" {
            return auth == "Bearer " + Self.appToken
                ? #"{"ok":true,"url":"ws://127.0.0.1:\#(socketPort)/link/?ticket=fake&app_id=A0TEST"}"# : refused
        }
        guard auth == "Bearer " + Self.userToken else { return refused }
        switch method {
        case "auth.test":
            return #"{"ok":true,"url":"https://test.slack.com/","team":"Test Team","user":"ada","team_id":"T0TEST","#
                + #""user_id":"\#(Self.me)"}"#
        case "users.info":
            guard arguments["user"] == "USHIRA" else { return #"{"ok":false,"error":"user_not_found"}"# }
            return #"{"ok":true,"user":{"id":"USHIRA","name":"shira","real_name":"Shira","#
                + #""profile":{"display_name":"Shira","real_name":"Shira"}}}"#
        case "conversations.info":
            switch arguments["channel"] {
            case "C0DEPLOYS": return #"{"ok":true,"channel":{"id":"C0DEPLOYS","name":"deploys","is_channel":true}}"#
            default: return #"{"ok":false,"error":"channel_not_found"}"#
            }
        case "dnd.info":
            return #"{"ok":true,"# + lock.withLock { dndJSON }.dropFirst()
        default:
            return #"{"ok":false,"error":"unknown_method"}"#
        }
    }
}
