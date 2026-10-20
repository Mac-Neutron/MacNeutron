import AppKit
import MacNeutronCore
import Observation

/// The app's own updates, as the menu and Settings show them: a check at launch and then daily while "Check
/// automatically" is on, and the install the player asks for (spec §5). Only a Developer ID build has a team, so a
/// `make app` build never checks.
@Observable @MainActor
final class UpdateModel {
    let current: String
    let app: URL
    let team: String?
    private let defaults: UserDefaults
    private let notifier: any Notifier
    private let get: @Sendable (URL) async throws -> Data
    private let now: () -> Date
    /// The newer release of this build's channel; nil when none, or not checked yet.
    private(set) var available: Release?
    /// What the last check or install said; nil when there's nothing to say.
    private(set) var notice: String?
    private(set) var working = false
    var automatic: Bool { didSet { defaults.set(automatic, forKey: "checkForUpdates") } }
    @ObservationIgnored private var timer: Task<Void, Never>?

    var enabled: Bool { team != nil }

    init(current: String, app: URL, team: String?, defaults: UserDefaults = .standard,
         notifier: any Notifier = AppleScriptNotifier(), get: @escaping @Sendable (URL) async throws -> Data = UpdateModel.fetch,
         now: @escaping () -> Date = Date.init, hourly: Bool = true) {
        self.current = current
        self.app = app
        self.team = team
        self.defaults = defaults
        self.notifier = notifier
        self.get = get
        self.now = now
        automatic = defaults.object(forKey: "checkForUpdates") as? Bool ?? true
        guard hourly, team != nil else { return }
        timer = Task { [weak self] in
            await self?.checkAtLaunch()
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(3600))
                await self?.checkIfDue()
            }
        }
    }

    /// GitHub's release list, through an ephemeral session.
    nonisolated static let fetch: @Sendable (URL) async throws -> Data = { url in
        var request = URLRequest(url: url)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        let session = URLSession(configuration: .ephemeral)
        defer { session.finishTasksAndInvalidate() }
        let (data, response) = try await session.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200 else { throw URLError(.badServerResponse) }
        return data
    }

    /// Asks GitHub now. A version found for the first time posts one notification. A failed check is retried at the
    /// next hourly look, not a day later.
    func check() async {
        guard enabled else { return }
        do {
            let data = try await get(Updater.releases)
            defaults.set(now(), forKey: "lastUpdateCheck")
            available = Updater.pick(data, current: current)
            notice = available == nil ? "MacNeutron \(current) is up to date." : nil
            if let release = available, defaults.string(forKey: "notifiedUpdate") != release.version {
                defaults.set(release.version, forKey: "notifiedUpdate")
                notifier.post(title: "MacNeutron", message: "MacNeutron \(release.version) is available.")
            }
        } catch {
            notice = "Couldn't check for updates: \(error.localizedDescription)"
        }
    }

    /// At launch, `check` whenever automatic checks are on: what a check found isn't kept across launches, so an update
    /// found earlier the same day is offered again.
    func checkAtLaunch() async {
        guard automatic else { return }
        await check()
    }

    /// `check`, when automatic checks are on and a day has passed since the last one.
    func checkIfDue() async {
        guard enabled, automatic else { return }
        if let last = defaults.object(forKey: "lastUpdateCheck") as? Date, now().timeIntervalSince(last) < 24 * 3600 {
            return
        }
        await check()
    }

    /// Downloads, verifies and installs the available release over this app, then relaunches it.
    func install() async {
        guard let release = available, let team, !working else { return }
        working = true
        notice = "Downloading and checking MacNeutron \(release.version)…"
        do {
            let work = try await Updater.install(release, over: app, team: team)
            try Updater.relaunch(app, removing: work)
            NSApplication.shared.terminate(nil)
        } catch {
            notice = (error as? UpdateError)?.description ?? error.localizedDescription
            working = false
        }
    }
}
