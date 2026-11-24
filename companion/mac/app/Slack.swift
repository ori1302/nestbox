import Foundation

/// Slack through Socket Mode. The user makes a Slack app of their own from NestBox's manifest, in their own
/// workspace, and gives this app two of its tokens: the app-level token (`xapp-`), which opens the socket, and the
/// user token (`xoxp-`), with which Slack sends the messages the user can see and answers who wrote and where. The
/// socket goes from this Mac to Slack; no NestBox server is involved. The pet shows a short bubble with who wrote and
/// where, never the message.
enum Slack {
    static let api = URL(string: "https://slack.com/api/")!

    /// Read as the user who installs the app: messages in the public channels and DMs they are in (to see which call
    /// them), the names of channels and people, and their Do Not Disturb. Private channels are left out.
    static let userScopes = ["channels:history", "im:history", "mpim:history", "channels:read", "users:read",
                             "dnd:read"]
    static let userEvents = ["message.channels", "message.im", "message.mpim", "dnd_updated"]

    static let manifest = """
        {
          "display_information": {
            "name": "NestBox",
            "description": "A bubble on your NestBox pet when someone writes to you.",
            "background_color": "#1d2433"
          },
          "oauth_config": {
            "scopes": {
              "user": [\(userScopes.map { "\"\($0)\"" }.joined(separator: ", "))]
            }
          },
          "settings": {
            "event_subscriptions": {
              "user_events": [\(userEvents.map { "\"\($0)\"" }.joined(separator: ", "))]
            },
            "org_deploy_enabled": false,
            "socket_mode_enabled": true,
            "token_rotation_enabled": false
          }
        }
        """

    /// Opens Slack's "create an app" page with the manifest filled in.
    static var createAppURL: URL {
        let allowed = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~")
        let json = manifest.addingPercentEncoding(withAllowedCharacters: allowed) ?? ""
        return URL(string: "https://api.slack.com/apps?new_app=1&manifest_json=" + json)!
    }

    /// Why a pasted token can't be the one asked for, before asking Slack; nil when it may be.
    static func problem(appToken token: String) -> String? {
        if token.isEmpty { return "Paste the app-level token (it starts with xapp-)." }
        if token.hasPrefix("xapp-") { return nil }
        if token.hasPrefix("xoxp-") { return "That is the user token. The app-level token starts with xapp-: "
            + "Basic Information › App-Level Tokens." }
        if token.hasPrefix("xoxb-") { return "That is a bot token. The app-level token starts with xapp-." }
        return "The app-level token starts with xapp-."
    }

    static func problem(userToken token: String) -> String? {
        if token.isEmpty { return "Paste the User OAuth Token (it starts with xoxp-)." }
        if token.hasPrefix("xoxp-") { return nil }
        if token.hasPrefix("xapp-") { return "That is the app-level token. The User OAuth Token starts with xoxp-: "
            + "Install App, after installing." }
        if token.hasPrefix("xoxb-") { return "That is a bot token. Use the User OAuth Token, which starts with xoxp-." }
        return "The User OAuth Token starts with xoxp-."
    }
}

/// What may call the pet; each can be ticked under Connections › Slack.
struct SlackWants: Equatable {
    var directMessages = true
    var mentions = true
    var threads = false

    var any: Bool { directMessages || mentions || threads }
}

/// A Socket Mode frame: "hello", "disconnect", or an "events_api" envelope to acknowledge.
struct SlackEnvelope: Decodable {
    let type: String
    let envelopeID: String?
    let reason: String?
    let numConnections: Int?
    let payload: Payload?

    struct Payload: Decodable {
        let eventID: String?
        let event: SlackEvent?

        private enum CodingKeys: String, CodingKey {
            case eventID = "event_id", event
        }
    }

    private enum CodingKeys: String, CodingKey {
        case type, reason, payload
        case envelopeID = "envelope_id"
        case numConnections = "num_connections"
    }
}

/// Do Not Disturb, as dnd.info answers and dnd_updated carries it (times in Unix seconds).
struct SlackDND: Decodable, Equatable {
    var dndEnabled: Bool?
    var nextStart: Double?
    var nextEnd: Double?
    var snoozeEnabled: Bool?
    var snoozeEnd: Double?

    private enum CodingKeys: String, CodingKey {
        case dndEnabled = "dnd_enabled", nextStart = "next_dnd_start_ts", nextEnd = "next_dnd_end_ts"
        case snoozeEnabled = "snooze_enabled", snoozeEnd = "snooze_endtime"
    }

    /// Until when notifications are paused at `now` (a snooze, or the scheduled hours), or nil.
    func quietUntil(_ now: Date) -> Date? {
        let t = now.timeIntervalSince1970
        if snoozeEnabled == true, let end = snoozeEnd, t < end { return Date(timeIntervalSince1970: end) }
        if dndEnabled == true, let start = nextStart, let end = nextEnd, start <= t, t < end {
            return Date(timeIntervalSince1970: end)
        }
        return nil
    }

    /// The scheduled hours it knows of have passed: dnd.info answers the next ones, and no event says when they move.
    func isStale(_ now: Date) -> Bool {
        dndEnabled == true && (nextEnd.map { $0 <= now.timeIntervalSince1970 } ?? false)
    }
}

/// The parts of an event this app reads. `text` is read only to find a mention of the user.
struct SlackEvent: Decodable, Equatable {
    var type: String
    var subtype: String?
    var channel: String?
    var channelType: String?
    var user: String?
    var botID: String?
    var username: String?
    var botName: String?
    var text: String?
    var ts: String?
    var threadTS: String?
    var parentUserID: String?
    var dnd: SlackDND?

    private struct BotProfile: Decodable {
        let name: String?
    }

    private enum CodingKeys: String, CodingKey {
        case type, subtype, channel, user, username, text, ts
        case channelType = "channel_type", botID = "bot_id", threadTS = "thread_ts", parentUserID = "parent_user_id"
        case botProfile = "bot_profile", dnd = "dnd_status"
    }

    init(type: String, subtype: String? = nil, channel: String? = nil, channelType: String? = nil,
         user: String? = nil, text: String? = nil, ts: String? = nil, threadTS: String? = nil,
         parentUserID: String? = nil) {
        self.type = type
        self.subtype = subtype
        self.channel = channel
        self.channelType = channelType
        self.user = user
        self.text = text
        self.ts = ts
        self.threadTS = threadTS
        self.parentUserID = parentUserID
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        type = try c.decode(String.self, forKey: .type)
        subtype = try? c.decodeIfPresent(String.self, forKey: .subtype)
        channel = try? c.decodeIfPresent(String.self, forKey: .channel)
        channelType = try? c.decodeIfPresent(String.self, forKey: .channelType)
        user = try? c.decodeIfPresent(String.self, forKey: .user)
        botID = try? c.decodeIfPresent(String.self, forKey: .botID)
        username = try? c.decodeIfPresent(String.self, forKey: .username)
        botName = (try? c.decodeIfPresent(BotProfile.self, forKey: .botProfile))?.name
        text = try? c.decodeIfPresent(String.self, forKey: .text)
        ts = try? c.decodeIfPresent(String.self, forKey: .ts)
        threadTS = try? c.decodeIfPresent(String.self, forKey: .threadTS)
        parentUserID = try? c.decodeIfPresent(String.self, forKey: .parentUserID)
        dnd = try? c.decodeIfPresent(SlackDND.self, forKey: .dnd)
    }

    /// Messages someone wrote: plain ones, ones with a file, replies also sent to the channel, and apps' messages.
    /// Edits, deletions, joins and the like are not.
    var isWritten: Bool {
        type == "message" && channel != nil && ts != nil
            && [nil, "thread_broadcast", "file_share", "bot_message"].contains(subtype)
    }

    /// A reply in a thread (not the thread's first message).
    var isReply: Bool { threadTS != nil && threadTS != ts }
}

/// Why a message calls the pet.
enum SlackCall: Equatable {
    case directMessage     // in a DM
    case groupMessage      // in a group DM
    case mention           // a mention of the user, in a public channel
    case threadReply       // a reply in a thread the user started or wrote in

    /// Which message, if any, calls the pet: the first reason it has that is ticked. `myThreads` holds
    /// "channel/thread_ts" of threads the user wrote in.
    static func of(_ e: SlackEvent, me: String, myThreads: Set<String>, wants: SlackWants) -> SlackCall? {
        guard e.isWritten, let channel = e.channel, e.user != me else { return nil }
        if wants.directMessages && e.channelType == "im" { return .directMessage }
        if wants.directMessages && e.channelType == "mpim" { return .groupMessage }
        if wants.mentions && mentions(e.text, me) { return .mention }
        if wants.threads, e.isReply, let thread = e.threadTS,
           e.parentUserID == me || myThreads.contains(channel + "/" + thread) { return .threadReply }
        return nil
    }

    /// Slack writes a mention as <@U123> or <@U123|name>.
    static func mentions(_ text: String?, _ me: String) -> Bool {
        guard let text, !me.isEmpty else { return false }
        return text.contains("<@\(me)>") || text.contains("<@\(me)|")
    }
}

/// A POST /v1/tell from "slack": a bubble with who wrote and where, never what, and a letter over the pet's head,
/// for a moment. The first message also turns the pet to the user, surprised; more in the same burst only update the
/// count. One per conversation (or thread), so a burst never queues bubbles.
struct SlackTell: Encodable, Equatable {
    struct Step: Encodable, Equatable {
        var `do`: String
        var face: String
    }

    let from = "slack"
    var id: String
    var say: String
    var sign: String?
    var effect: String?
    var steps: [Step]?
    var seconds: Int

    private enum CodingKeys: String, CodingKey {
        case from, id, say, sign, effect
        case steps = "do"
        case seconds = "for"
    }

    static let surprised = [Step(do: "surprised", face: "front")]

    static let seconds = 20

    /// `who` wrote `count` messages that called, in `place` ("#deploys"), within one bubble's time.
    static func call(_ call: SlackCall, id: String, who: String, place: String?, count: Int) -> SlackTell {
        let who = fit(who, 24), place = fit(place ?? "a channel", 28)
        let say: String
        switch call {
        case .directMessage: say = count > 1 ? "\(count) messages from \(who)" : "\(who) wrote to you"
        case .groupMessage: say = count > 1 ? "\(count) messages in a group DM" : "\(who) wrote in a group DM"
        case .mention: say = "\(who) mentioned you in \(place)"
        case .threadReply:
            say = count > 1 ? "\(count) replies in a thread in \(place)" : "\(who) replied in a thread in \(place)"
        }
        return SlackTell(id: id, say: cap(say), effect: "letter", steps: count == 1 ? surprised : nil,
                         seconds: seconds)
    }

    /// The moment Slack connects after setup.
    static let hello = SlackTell(id: "hello", say: "Slack is connected", sign: "check", seconds: 8)

    /// The pet keeps at most this many bytes of "say".
    static let sayBytes = 72

    private static func cap(_ text: String) -> String {
        guard text.utf8.count > sayBytes else { return text }
        var cut = text
        while (cut + "…").utf8.count > sayBytes { cut.removeLast() }
        return cut.trimmingCharacters(in: .whitespaces) + "…"
    }

    private static func fit(_ text: String, _ most: Int) -> String {
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines)
        return clean.count <= most ? clean : String(clean.prefix(most - 1)) + "…"
    }
}
