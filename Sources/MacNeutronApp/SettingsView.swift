import MacNeutronCore
import ServiceManagement
import SwiftUI

struct SettingsView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.openWindow) private var openWindow
    @State private var confirmingTurnOff = false

    var body: some View {
        Form {
            Toggle("Open MacNeutron at login", isOn: Binding(get: { model.launchesAtLogin }, set: { model.setLaunchAtLogin($0) }))
            if model.loginItemStatus == .requiresApproval {
                LabeledContent("Allow MacNeutron under Login Items in System Settings") {
                    Button("Open Login Items") { SMAppService.openSystemSettingsLoginItems() }
                }
                .foregroundStyle(.orange)
            }
            LabeledContent("Runtime") {
                Button("Repair runtime") { Task { await model.installRuntime(force: true) } }
                    .disabled(model.installing)
            }
            if let notice = model.runtimeNotice { Text(notice).foregroundStyle(.secondary).textSelection(.enabled) }
            LabeledContent("Setup") {
                Button("Run setup again") { show("setup", with: openWindow) }
            }
            LabeledContent("Steam Play mode") {
                Button("Turn off Steam Play mode", role: .destructive) { confirmingTurnOff = true }
                    .disabled(!model.mode.isWanted)
            }
            if let busy = model.busy { ProgressView(busy).controlSize(.small) }
            if let error = model.errorMessage { Text(error).foregroundStyle(.red).textSelection(.enabled) }
        }
        .formStyle(.grouped)
        .frame(width: 460)
        .disabled(model.busy != nil)
        .confirmationDialog("Turn off Steam Play mode?", isPresented: $confirmingTurnOff) {
            Button("Turn off and restart Steam", role: .destructive) { Task { await model.disableSteamPlay() } }
        } message: {
            Text("Windows games will be removed from disk and need downloading again if you turn this back on. Saves inside their prefixes are kept.")
        }
    }
}

struct CleanupView: View {
    @Environment(AppModel.self) private var model
    @State private var selected = Set<String>()
    @State private var confirming = false

    private var chosen: [OrphanPrefix] { model.orphans.filter { selected.contains($0.appID) } }
    private func name(_ orphan: OrphanPrefix) -> String {
        model.games.first { String($0.id) == orphan.appID }?.name ?? "App \(orphan.appID)"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Leftover data from games you've uninstalled").font(.headline)
            Text("These folders hold games' Windows settings and saves. Saves that aren't in Steam Cloud exist nowhere else.")
                .foregroundStyle(.secondary)
            if model.orphans.isEmpty {
                Text("Nothing to clean up.").foregroundStyle(.secondary)
            }
            ForEach(model.orphans, id: \.appID) { orphan in
                Toggle(isOn: Binding(get: { selected.contains(orphan.appID) },
                                     set: { if $0 { selected.insert(orphan.appID) } else { selected.remove(orphan.appID) } })) {
                    HStack {
                        Text(name(orphan))
                        Spacer()
                        Text(ByteCountFormatter.string(fromByteCount: orphan.bytes, countStyle: .file)).foregroundStyle(.secondary)
                    }
                }
            }
            HStack {
                Spacer()
                Button("Delete selected", role: .destructive) { confirming = true }
                    .disabled(selected.isEmpty)
            }
        }
        .padding(20)
        .frame(width: 420)
        .confirmationDialog("Delete \(chosen.count) \(chosen.count == 1 ? "game's" : "games'") data?", isPresented: $confirming) {
            Button("Delete", role: .destructive) {
                let picked = chosen
                Task { await model.cleanUp(picked) }
                selected.removeAll()
            }
        } message: {
            Text(chosen.map(name).joined(separator: ", ") + "\n\nThis can't be undone.")
        }
    }
}
