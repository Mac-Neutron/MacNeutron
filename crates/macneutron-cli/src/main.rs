use std::collections::BTreeMap;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let environment: BTreeMap<String, String> = std::env::vars().collect();
    let executable = std::env::current_exe().unwrap_or_else(|_| std::path::PathBuf::from("/x"));
    match args.first().map(String::as_str) {
        // the launcher's own, AppKit's: not one of run's
        Some("toast") => {
            if let Some((title, message, until)) = macneutron_cli::toast::arguments(&args[1..]) {
                macneutron_cli::toast::run(title, message, until)
            }
        }
        // Steam's Stop reaches an app (stop_requests.rs)
        Some("launch") => {
            macneutron_cli::stop_requests::run(move || macneutron_cli::run(&args, &environment, &executable))
        }
        _ => {}
    }
    std::process::exit(macneutron_cli::run(&args, &environment, &executable));
}
