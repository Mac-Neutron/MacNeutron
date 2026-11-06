use std::collections::BTreeMap;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let environment: BTreeMap<String, String> = std::env::vars().collect();
    let executable = std::env::current_exe().unwrap_or_else(|_| std::path::PathBuf::from("/x"));
    std::process::exit(macneutron_cli::run(&args, &environment, &executable));
}
