import Foundation
import Testing
@testable import MacNeutronApp
@testable import MacNeutronCore

/// GitHub's release list, answering every request with the same reply and counting them.
private final class FakeGitHub: @unchecked Sendable {
    private let lock = NSLock()
    private var count = 0
    let reply = releasesJSON([("v0.9.0", false, false, ["MacNeutron-0.9.0.zip", "SHA256SUMS"])])
    var calls: Int { lock.withLock { count } }
    var get: @Sendable (URL) async throws -> Data {
        { _ in self.lock.withLock { self.count += 1 }; return self.reply }
    }
}

@MainActor private final class Clock { var date = Date(timeIntervalSince1970: 1_800_000_000) }

@MainActor private func makeUpdates(team: String? = "ABCDE12345")
    -> (UpdateModel, FakeGitHub, RecordingNotifier, Clock) {
    let github = FakeGitHub(), notifier = RecordingNotifier(), clock = Clock()
    let model = UpdateModel(current: "0.8.0", app: URL(filePath: "/Applications/MacNeutron.app"), team: team,
                            defaults: UserDefaults(suiteName: UUID().uuidString)!, notifier: notifier,
                            get: github.get, now: { clock.date }, hourly: false)
    return (model, github, notifier, clock)
}

@MainActor @Test func checkFindsAReleaseAndNotifiesOnce() async throws {
    let (model, _, notifier, _) = makeUpdates()
    await model.check()
    #expect(model.available?.version == "0.9.0")
    #expect(notifier.posted == ["MacNeutron 0.9.0 is available."])
    await model.check()
    #expect(notifier.posted.count == 1)  // the same version isn't announced again
}

@MainActor @Test func checkIfDueWaitsADayAndHonoursTheSwitch() async throws {
    let (model, github, _, clock) = makeUpdates()
    await model.checkIfDue()
    #expect(github.calls == 1)
    clock.date += 23 * 3600
    await model.checkIfDue()
    #expect(github.calls == 1)
    clock.date += 2 * 3600
    await model.checkIfDue()
    #expect(github.calls == 2)
    model.automatic = false
    clock.date += 50 * 3600
    await model.checkIfDue()
    #expect(github.calls == 2)
}

@MainActor @Test func devBuildsNeverCheck() async throws {
    let (model, github, _, _) = makeUpdates(team: nil)
    #expect(!model.enabled)
    await model.check()
    await model.checkIfDue()
    #expect(github.calls == 0)
}
