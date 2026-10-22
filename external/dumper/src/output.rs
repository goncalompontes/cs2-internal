//! JSON output for dumps and generated SDK data.

use crate::memory::ModuleInfo;
use serde::Serialize;

#[derive(Debug, Serialize)]
pub struct ModuleDump {
    pub name: String,
    pub path: String,
    pub base: String,
    pub size: usize,
}

#[derive(Debug, Serialize)]
pub struct DumpManifest {
    pub process: String,
    pub pid: u32,
    pub modules: Vec<ModuleDump>,
}

impl DumpManifest {
    pub fn new(process: &str, pid: u32, modules: &[ModuleInfo]) -> Self {
        Self {
            process: process.to_string(),
            pid,
            modules: modules
                .iter()
                .map(|m| ModuleDump {
                    name: m.name.clone(),
                    path: m.path.to_string_lossy().to_string(),
                    base: format!("0x{:x}", m.base),
                    size: m.size,
                })
                .collect(),
        }
    }
}

pub fn to_json<T: Serialize>(value: &T) -> String {
    serde_json::to_string_pretty(value).unwrap_or_else(|_| "{}".to_string())
}
