import MacNeutronCore
import SwiftUI

struct GamesView: View {
    @Environment(AppModel.self) private var model
    @State private var search = ""
    @State private var selection: UInt32?
    /// A "Runs as" change waiting for the player to confirm it (spec §4.5).
    @State private var pendingRunAs: (id: UInt32, runAs: RunAs)?

    private var rows: [GameRow] {
        search.isEmpty ? model.games : model.games.filter { $0.name.localizedCaseInsensitiveContains(search) }
    }

    var body: some View {
        VStack(spacing: 0) {
            Table(rows, selection: $selection) {
                TableColumn("Game") { row in
                    HStack {
                        Text(row.name)
                        if row.installed { Text("Installed").font(.caption).foregroundStyle(.secondary) }
                    }
                }
                TableColumn("Runs as") { row in
                    if row.isDualPlatform {
                        Picker("", selection: Binding(
                            get: { row.settings.runAs ?? .mac },
                            set: { value in pendingRunAs = (row.id, value) })) {
                            Text("Mac version").tag(RunAs.mac)
                            Text("Windows version").tag(RunAs.windows)
                        }
                        .labelsHidden()
                    } else {
                        Text(row.app.oslist.contains("macos") ? "Mac version" : "Windows").foregroundStyle(.secondary)
                    }
                }
                TableColumn("Graphics") { row in
                    if row.runsWithMacNeutron {
                        Picker("", selection: Binding(
                            // A value from before 0.1 (d3dmetal, dxvk) runs DXMT, so it shows as the default.
                            get: { row.settings.graphics.flatMap { GraphicsBackend(rawValue: $0)?.rawValue } ?? "" },
                            set: { value in Task { await model.update(row.id) { $0.graphics = value.isEmpty ? nil : value } } })) {
                            Text("Default (DXMT)").tag("")
                            Text("DXMT").tag("dxmt")
                            Text("wined3d (OpenGL)").tag("wined3d")
                        }
                        .labelsHidden()
                    } else {
                        Text("Native").foregroundStyle(.secondary)
                    }
                }
            }
            if let row = rows.first(where: { $0.id == selection }), row.runsWithMacNeutron {
                HStack(spacing: 16) {
                    Text(row.name).bold()
                    Toggle("Log", isOn: binding(row, \.log, default: false))
                    Toggle("msync", isOn: binding(row, \.msync, default: true))
                    Toggle("MetalFX upscaling", isOn: binding(row, \.metalFX, default: true))
                        .help("Upscales with Apple's MetalFX when the game renders below its window or the display's pixel density.")
                    Picker("Anti-aliasing (post)", selection: Binding(
                        get: { row.settings.postAA == "cmaa2" ? "cmaa2" : "off" },  // any other value is off
                        set: { value in Task { await model.update(row.id) { $0.postAA = value == "off" ? nil : value } } })) {
                        Text("Off").tag("off")
                        Text("CMAA2").tag("cmaa2")
                    }
                    .fixedSize()
                    .help("for games without their own anti-aliasing")
                    Picker("Anisotropic filtering", selection: Binding(
                        // Unknown values run at the default, so they show as it.
                        get: { row.settings.anisotropy.flatMap { ["game", "4", "8"].contains($0) ? $0 : nil } ?? "16" },
                        set: { value in Task { await model.update(row.id) { $0.anisotropy = value == "16" ? nil : value } } })) {
                        Text("Game's choice").tag("game")
                        Text("4x").tag("4")
                        Text("8x").tag("8")
                        Text("16x (default)").tag("16")
                    }
                    .fixedSize()
                    .help("Sharper textures on surfaces seen at an angle")
                    Spacer()
                }
                .padding(10)
            }
            if let error = model.errorMessage {
                HStack {
                    Text(error).foregroundStyle(.red).textSelection(.enabled)
                    Spacer()
                    Button("Dismiss") { model.errorMessage = nil }
                }
                .padding(10)
            }
            if case .restartNeeded = model.status {
                HStack {
                    Text(model.status.menuTitle).foregroundStyle(.orange)
                    Spacer()
                    Button("Restart Steam") { Task { await model.restartSteam() } }
                }
                .padding(10)
            }
        }
        .searchable(text: $search)
        .confirmationDialog("Switch versions?", isPresented: Binding(get: { pendingRunAs != nil }, set: { if !$0 { pendingRunAs = nil } }),
                            presenting: pendingRunAs) { pending in
            Button("Switch") { Task { await model.update(pending.id) { $0.runAs = pending.runAs == .mac ? nil : pending.runAs } } }
            Button("Cancel", role: .cancel) {}
        } message: { _ in
            Text("Steam Cloud keeps this game's saves in a different place for each version. Back up your saves before switching.")
        }
        .frame(minWidth: 640, minHeight: 360)
        .overlay {
            if let error = model.appInfoError {
                ContentUnavailableView("Steam's app list couldn't be read", systemImage: "exclamationmark.triangle",
                                       description: Text(error))
            }
        }
    }

    private func binding(_ row: GameRow, _ key: WritableKeyPath<GameSettings, Bool?>, default value: Bool) -> Binding<Bool> {
        Binding(get: { row.settings[keyPath: key] ?? value },
                set: { newValue in Task { await model.update(row.id) { $0[keyPath: key] = newValue == value ? nil : newValue } } })
    }
}
