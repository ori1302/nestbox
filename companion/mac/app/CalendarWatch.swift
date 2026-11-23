import EventKit
import Foundation

/// An event on the Mac's calendars, reduced to what decides whether it is a meeting. Nothing else is read.
struct CalendarEvent {
    var start: Date
    var end: Date
    var allDay = false
    var free = false        // marked as free time
    var declined = false    // the user said no
    var canceled = false

    var isMeeting: Bool { !allDay && !free && !declined && !canceled && end > start }
}

/// A POST /v1/tell from "calendar": generic words only, never an event's title or people.
struct CalendarTell: Encodable, Equatable {
    let from = "calendar"
    var state: String
    var doing: String?
    var line: String?
    var gauge: String?
    var color: String?
    var seconds: Int?
    /// When the state ends (a meeting's end, or the next one's start); not sent, only compared.
    var until: Date?
    /// In a meeting: when the latest of the current meetings started; not sent.
    var since: Date?

    private enum CodingKeys: String, CodingKey {
        case from, state, line, gauge, color
        case doing = "do"
        case seconds = "for"
    }

    /// The words said once as a meeting starts, as a moment of their own (`calendar/start`).
    static let startWords = "Meeting time"
    static let startSeconds = 20    // how long they stay up
    /// A meeting that started longer ago than this is not announced (the companion started late, or came back).
    static let startsWithin: TimeInterval = 60

    static let over = CalendarTell(state: "over")

    /// What the pet shows at `now`: quiet with a countdown while in a meeting (until the last overlapping one ends),
    /// a quiet line with a countdown in the HEADS_UP before the next one starts, otherwise nothing.
    static func at(_ now: Date, events: [CalendarEvent]) -> CalendarTell {
        let meetings = events.filter(\.isMeeting)
        let current = meetings.filter { $0.start <= now && now < $0.end }
        if let end = current.map(\.end).max() {
            return CalendarTell(state: "quiet", doing: "watch", line: "in a meeting", gauge: "timer", color: "blue",
                                seconds: seconds(from: now, to: end), until: end, since: current.map(\.start).max())
        }
        let next = meetings.filter { $0.start > now && $0.start.timeIntervalSince(now) <= headsUp }.map(\.start).min()
        if let start = next {
            return CalendarTell(state: "on", line: "meeting soon", gauge: "timer", color: "blue",
                                seconds: seconds(from: now, to: start), until: start)
        }
        return over
    }

    static let headsUp: TimeInterval = 5 * 60

    /// The next moment after `now` at which what the pet shows changes: a heads-up starting, a meeting starting or
    /// ending. Nil when nothing in `events` changes it.
    static func nextChange(after now: Date, events: [CalendarEvent]) -> Date? {
        events.filter(\.isMeeting)
            .flatMap { [$0.start.addingTimeInterval(-headsUp), $0.start, $0.end] }
            .filter { $0 > now }
            .min()
    }

    private static func seconds(from now: Date, to end: Date) -> Int { max(1, Int(end.timeIntervalSince(now).rounded(.up))) }

    /// The same state with the same end: telling it again would only restart the countdown ring.
    func sameAs(_ other: CalendarTell) -> Bool { state == other.state && until == other.until }

    /// Whether this tell begins a meeting that has just started, so the pet says the start words.
    func starts(after previous: CalendarTell?, at now: Date) -> Bool {
        guard state == "quiet", previous?.state != "quiet", let since else { return false }
        return now.timeIntervalSince(since) < Self.startsWithin
    }
}

/// Watches the Mac's calendars (the Calendar app's accounts) and tells the pet when a meeting is near or on. It
/// looks again at the next change it can see, every LOOK_EVERY, and whenever the calendars change, and tells only
/// when what it would say changes.
@MainActor
final class CalendarWatch {
    private static let lookEvery: TimeInterval = 20
    private static let lookBack: TimeInterval = 12 * 60 * 60    // a meeting that started this long ago still counts

    var onTell: ((CalendarTell) -> Void)?
    var onSay: ((String) -> Void)?
    var onNote: ((String) -> Void)?

    private let store = EKEventStore()
    private var timer: Timer?
    private var nextLook: Timer?
    private var changes: NSObjectProtocol?
    private var told: CalendarTell?
    private(set) var isRunning = false

    func start() {
        guard !isRunning else { return }
        isRunning = true
        told = nil
        askForAccess { [weak self] granted in
            guard let self, self.isRunning else { return }
            guard granted else {
                self.onNote?("calendar: no access; allow NestBox in System Settings > Privacy & Security > Calendars")
                return
            }
            self.watch()
        }
    }

    /// Stops watching and ends the calendar's note at once.
    func stop() {
        guard isRunning else { return }
        isRunning = false
        timer?.invalidate()
        timer = nil
        nextLook?.invalidate()
        nextLook = nil
        if let changes { NotificationCenter.default.removeObserver(changes) }
        changes = nil
        if told != nil && told != .over { onTell?(.over) }
        told = nil
    }

    /// Tells the current state again, for a pet that may not have it (a new address, a restart).
    func tellAgain() {
        guard isRunning else { return }
        told = nil
        look()
    }

    private func watch() {
        changes = NotificationCenter.default.addObserver(forName: .EKEventStoreChanged, object: store, queue: .main) {
            [weak self] _ in MainActor.assumeIsolated { self?.look() }
        }
        let timer = Timer(timeInterval: Self.lookEvery, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.look() }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
        look()
    }

    private func look() {
        let now = Date()
        let predicate = store.predicateForEvents(withStart: now.addingTimeInterval(-Self.lookBack),
                                                 end: now.addingTimeInterval(CalendarTell.headsUp + Self.lookEvery),
                                                 calendars: nil)
        let events = store.events(matching: predicate).map { event in
            CalendarEvent(start: event.startDate, end: event.endDate, allDay: event.isAllDay,
                          free: event.availability == .free,
                          declined: event.attendees?.first(where: \.isCurrentUser)?.participantStatus == .declined,
                          canceled: event.status == .canceled)
        }
        scheduleLook(at: CalendarTell.nextChange(after: now, events: events))
        let tell = CalendarTell.at(now, events: events)
        if let told, told.sameAs(tell) { return }
        if told == nil && tell == .over { told = tell; return }    // nothing on, and nothing told yet
        let starting = tell.starts(after: told, at: now)
        told = tell
        onTell?(tell)
        if starting { onSay?(CalendarTell.startWords) }
    }

    /// Looks again just after `moment`, when it comes before the next regular look.
    private func scheduleLook(at moment: Date?) {
        nextLook?.invalidate()
        nextLook = nil
        guard let moment, moment.timeIntervalSinceNow < Self.lookEvery else { return }
        let look = Timer(fire: moment.addingTimeInterval(0.2), interval: 0, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.look() }
        }
        RunLoop.main.add(look, forMode: .common)
        nextLook = look
    }

    private func askForAccess(_ done: @escaping @MainActor (Bool) -> Void) {
        let reply: @Sendable (Bool, Error?) -> Void = { granted, _ in Task { @MainActor in done(granted) } }
        if #available(macOS 14.0, *) {
            store.requestFullAccessToEvents(completion: reply)
        } else {
            store.requestAccess(to: .event, completion: reply)
        }
    }
}
