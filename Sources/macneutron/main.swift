import Foundation
import MacNeutronCore

let arguments = CommandLine.arguments
if arguments.count >= 4, arguments[1] == "toast" {  // the launcher's own, AppKit's: not one of CommandLineTool's
    Toast.run(title: arguments[2], message: arguments[3], until: arguments.dropFirst(4).first)
}
let executable = Bundle.main.executableURL ?? URL(filePath: CommandLine.arguments[0])
exit(await CommandLineTool.run(Array(CommandLine.arguments.dropFirst()),
                               environment: ProcessInfo.processInfo.environment,
                               executable: executable))
