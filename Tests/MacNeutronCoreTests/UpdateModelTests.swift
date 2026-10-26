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

/// A model whose defaults are a plist in a temp folder (CFPreferences takes an absolute path as a domain): a named
/// suite would leave a plist in ~/Library/Preferences for every run.
@MainActor private func makeUpdates(team: String? = "ABCDE12345") throws
    -> (UpdateModel, FakeGitHub, RecordingNotifier, Clock) {
    let github = FakeGitHub(), notifier = RecordingNotifier(), clock = Clock()
    let suite = try makeTempDir().appending(path: "defaults.plist").path(percentEncoded: false)
    let model = UpdateModel(current: "0.8.0", app: URL(filePath: "/Applications/MacNeutron.app"), team: team,
                            defaults: try #require(UserDefaults(suiteName: suite)), notifier: notifier,
                            get: github.get, now: { clock.date }, hourly: false)
    return (model, github, notifier, clock)
}

@MainActor @Test func checkFindsAReleaseAndNotifiesOnce() async throws {
    let (model, _, notifier, _) = try makeUpdates()
    await model.check()
    #expect(model.available?.version == "0.9.0")
    #expect(notifier.posted == ["MacNeutron 0.9.0 is available."])
    await model.check()
    #expect(notifier.posted.count == 1)  // the same version isn't announced again
}

@MainActor @Test func checkIfDueWaitsADayAndHonoursTheSwitch() async throws {
    let (model, github, _, clock) = try makeUpdates()
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
    let (model, github, _, _) = try makeUpdates(team: nil)
    #expect(!model.enabled)
    await model.check()
    await model.checkIfDue()
    #expect(github.calls == 0)
}

@MainActor @Test func launchChecksEvenWithinADay() async throws {
    let (model, github, _, _) = try makeUpdates()
    await model.checkIfDue()
    await model.checkAtLaunch()  // relaunched within the day: the update found is offered again
    #expect(github.calls == 2)
    model.automatic = false
    await model.checkAtLaunch()
    #expect(github.calls == 2)
}

@MainActor @Test func theInstallerOpensAsANewCopyFromTheApp() {
    // A running Install MacNeutron (one the player opened) mustn't be brought forward in its place, without --from-app.
    let configuration = UpdateModel.installerConfiguration()
    #expect(configuration.arguments == ["--from-app"])
    #expect(!configuration.allowsRunningApplicationSubstitution)
}
