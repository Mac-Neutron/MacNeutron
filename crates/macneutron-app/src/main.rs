//! MacNeutron's menu bar app (MacNeutronApp.swift): the real models, with the login item, defaults and quitting macOS
//! gives them, run in AppKit.

use objc2_foundation::{NSBundle, NSString};

use macneutron_app::app_model::{AppModel, AppModelConfig};
use macneutron_app::ui;
use macneutron_app::update_model::{UpdateModel, UpdateServices};
use macneutron_core::{CodeIdentity, MicroVM};

fn main() {
    let mut config = AppModelConfig::standard();
    config.login_item = std::sync::Arc::new(ui::system::login_item());
    config.vm_root = AppModel::bundled_vm_tools().map(|_| MicroVM::standard_root());
    let model = AppModel::new(config);

    let bundle = NSBundle::mainBundle();
    let current = bundle
        .objectForInfoDictionaryKey(&NSString::from_str("CFBundleShortVersionString"))
        .and_then(|value| value.downcast::<NSString>().ok())
        .map_or_else(|| "dev".to_string(), |version| version.to_string());
    let app = bundle.bundleURL().to_file_path().unwrap_or_default();
    let team = CodeIdentity::team(&app);
    let mut services = UpdateServices::standard();
    services.defaults = Box::new(ui::system::UserDefaults);
    services.terminate = Box::new(ui::system::terminate);
    let updates = UpdateModel::new(&current, app, team, services);

    ui::run(model, updates);
}
