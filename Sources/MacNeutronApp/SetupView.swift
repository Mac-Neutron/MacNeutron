import AppKit
import MacNeutronCore
import SwiftUI

struct SetupView: View {
    @Environment(AppModel.self) private var model

    private var nativeGames: [GameRow] { model.games.filter { !$0.runsWithMacNeutron } }
    private var windowsGames: [GameRow] { model.games.filter(\.runsWithMacNeutron) }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Set up MacNeutron", systemImage: "atom").font(.title2)

            Step(done: model.steamInstalled, title: "Steam",
                 detail: model.steamInstalled ? "Steam for Mac is installed." : "Install Steam for Mac first.") { EmptyView() }

            Step(done: model.runtime != nil, title: "Runtime",
                 detail: model.runtimeNotice ?? model.runtime.map { "Runtime \($0.label) installed" } ?? "Not installed.") {
                if model.installing { ProgressView().controlSize(.small) }
            }

            if model.vmManifest != nil {
                Step(done: !model.vmSetupRunning && !model.vmSetupNeeded, title: "Game VM",
                     detail: model.vmNotice ?? (model.vmSetupNeeded ? "Sets itself up after the runtime." : "Ready.")) {
                    if model.vmSetupRunning { ProgressView().controlSize(.small) }
                }
            }

            Step(done: model.mode.isWanted, title: "Steam Play",
                 detail: "Steam restarts. Your Mac games stay native and protected.") {
                Button("Turn on and restart Steam") { Task { await model.enableSteamPlay() } }
                    .buttonStyle(.borderedProminent)
                    .disabled(model.runtime == nil || !model.steamInstalled)
            }
            if !model.mode.isWanted {
                Grid(alignment: .leading, horizontalSpacing: 24, verticalSpacing: 4) {
                    GridRow {
                        Label("Stay native (\(nativeGames.count))", systemImage: "apple.logo")
                        Label("Run with MacNeutron (\(windowsGames.count))", systemImage: "square.grid.2x2")
                    }
                    .foregroundStyle(.secondary)
                    GridRow {
                        Text(summary(nativeGames))
                        Text(summary(windowsGames))
                    }
                }
                .font(.callout)
                .padding(.leading, 30)
            }

            if let busy = model.busy { ProgressView(busy).controlSize(.small) }
            if let error = model.errorMessage { Text(error).foregroundStyle(.red).textSelection(.enabled) }
        }
        .padding(20)
        .frame(width: 560)
        .disabled(model.busy != nil)
        .onAppear {
            Task { await model.refresh() }
            raiseWindow("setup")  // a menu-bar app isn't active on its own, so the window would open behind everything
        }
    }

    private func summary(_ rows: [GameRow]) -> String {
        let installed = rows.filter(\.installed).map(\.name)
        let names = installed.isEmpty ? rows.prefix(3).map(\.name) : installed.prefix(4).map { $0 }
        return names.isEmpty ? "None" : names.joined(separator: ", ") + (rows.count > names.count ? "…" : "")
    }
}

private struct Step<Action: View>: View {
    let done: Bool
    let title: String
    let detail: String
    @ViewBuilder let action: Action

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: done ? "checkmark.circle.fill" : "circle.dashed")
                .foregroundStyle(done ? .green : .secondary)
                .font(.title3)
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                Text(detail).font(.callout).foregroundStyle(.secondary)
            }
            Spacer()
            action
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 8).strokeBorder(.quaternary))
    }
}
