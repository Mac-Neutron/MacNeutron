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
                VStack(alignment: .leading, spacing: 6) {
                    optionsToggles(row)
                    optionsPickers(row)
                    LaunchOptionsField(row: row).id(row.id)  // a fresh draft for each game
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

    /// The per-game toggles. In two rows of their own: one HStack with every option is past what the type checker
    /// chews through in reasonable time.
    @ViewBuilder private func optionsToggles(_ row: GameRow) -> some View {
        HStack(spacing: 16) {
            Text(row.name).bold()
            Toggle("Log", isOn: binding(row, \.log, default: false))
            Toggle("Metal HUD", isOn: binding(row, \.metalHUD, default: false))
                .help("Apple's Metal performance HUD over the game (FPS, frame times), with DXMT's own pass, queue and MetalFX lines on it: the same as MTL_HUD_ENABLED=1")
            Toggle("msync", isOn: binding(row, \.msync, default: true))
            Toggle("MetalFX upscaling", isOn: binding(row, \.metalFX, default: true))
                .help("Upscales with Apple's MetalFX when the game renders below its window or the display's pixel density.")
            Toggle("DLSS", isOn: binding(row, \.dlss, default: false))
                .help("Experimental: the game sees an NVIDIA GPU, and its DLSS option upscales with MetalFX's temporal upscaler (MACNEUTRON_DLSS=1). Direct3D 12 games on DXMT.")
            Toggle("Frame generation", isOn: binding(row, \.frameGen, default: false))
                .disabled(row.settings.dlss != true)
                .help("Experimental: MetalFX interpolates a frame between each two the game renders, while the game runs DLSS (MACNEUTRON_FRAMEGEN=1). More frames shown, a little more latency; the HUD may wobble in motion.")
            Spacer()
        }
    }

    @ViewBuilder private func optionsPickers(_ row: GameRow) -> some View {
        HStack(spacing: 16) {
            Picker("Upscaling", selection: upscaleBinding(row)) {
                Text("Off").tag("off")
                Text("Ultra Quality (1.3x)").tag("ultra-quality")
                Text("Quality (1.5x)").tag("quality")
                Text("Balanced (1.7x)").tag("balanced")
                Text("Performance (2.0x)").tag("performance")
                Text("Ultra Performance (3.0x)").tag("ultra-performance")
            }
            .fixedSize()
            .disabled(row.settings.metalFX == false)  // the render scale needs the MetalFX upscaler
            .help("reports a smaller desktop to the game and upscales the window with MetalFX: much faster, a little softer. Needs \"MetalFX upscaling\" on, and the game's own upscaler off")
            Picker("Anti-aliasing (post)", selection: postAABinding(row)) {
                Text("Off").tag("off")
                Text("CMAA2").tag("cmaa2")
            }
            .fixedSize()
            .help("for games without their own anti-aliasing")
            Picker("Anisotropic filtering", selection: anisotropyBinding(row)) {
                Text("Game's choice").tag("game")
                Text("4x").tag("4")
                Text("8x").tag("8")
                Text("16x (default)").tag("16")
            }
            .fixedSize()
            .help("Sharper textures on surfaces seen at an angle")
            Spacer()
        }
    }

    /// Unknown values run at the default, so they show as it.
    private func upscaleBinding(_ row: GameRow) -> Binding<String> {
        Binding(get: { row.settings.upscale.flatMap { GameSettings.upscalePresets.contains($0) ? $0 : nil } ?? "off" },
                set: { value in Task { await model.update(row.id) { $0.upscale = value == "off" ? nil : value } } })
    }

    private func postAABinding(_ row: GameRow) -> Binding<String> {
        Binding(get: { row.settings.postAA == "cmaa2" ? "cmaa2" : "off" },  // any other value is off
                set: { value in Task { await model.update(row.id) { $0.postAA = value == "off" ? nil : value } } })
    }

    private func anisotropyBinding(_ row: GameRow) -> Binding<String> {
        Binding(get: { row.settings.anisotropy.flatMap { ["game", "4", "8"].contains($0) ? $0 : nil } ?? "16" },
                set: { value in Task { await model.update(row.id) { $0.anisotropy = value == "16" ? nil : value } } })
    }
}

/// The selected game's launch options (`GameSettings.launchOptions`), saved on Return and when the field is left; with
/// the game's own Steam launch line, which wins over it, and what's wrong with that line.
private struct LaunchOptionsField: View {
    @Environment(AppModel.self) private var model
    let row: GameRow
    @State private var draft: String
    @FocusState private var focused: Bool

    init(row: GameRow) {
        self.row = row
        _draft = State(initialValue: row.settings.launchOptions ?? "")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("Launch options")
                TextField("Launch options", text: $draft, prompt: Text("DXMT_D3D12_SM6=0 -dx12"))
                    .labelsHidden()  // the Text beside it shows the name; VoiceOver reads the field's own
                    .focused($focused)
                    .onSubmit(save)
                    .onChange(of: focused) { _, now in if !now { save() } }
                    .onDisappear(perform: save)
                    .help("Variables and game arguments, e.g. DXMT_D3D12_SM6=0 -dx12. In the game VM only the variables MacNeutron passes to the VM reach the game (its logging, DLSS and a few DXMT ones); DXMT's reach its renderer on the Mac.")
            }
            if let line = row.steamLaunchOptions {
                Text("Steam's launch options: \(line)").foregroundStyle(.secondary).textSelection(.enabled)
                ForEach(LaunchOptions.problems(steamLine: line, appEnvironment: row.settings.environment), id: \.self) {
                    Text($0).foregroundStyle(.orange)
                }
            }
        }
    }

    private func save() {
        let value = draft.trimmingCharacters(in: .whitespaces)
        guard value != (row.settings.launchOptions ?? "") else { return }
        let id = row.id
        Task { await model.update(id) { $0.launchOptions = value.isEmpty ? nil : value } }
    }
}
