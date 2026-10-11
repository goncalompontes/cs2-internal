//! Analysis layer: signature registry and resolved offsets/interfaces.
//!
//! This is the seam between the raw scanner and the generated SDK consumed by
//! the C++ payload. It starts small and grows as analysis progresses.

use crate::pattern::Signature;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum OffsetSource {
    Signature,
    Schema,
    Manual,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Offset {
    pub name: String,
    pub module: String,
    pub rva: usize,
    pub source: OffsetSource,
}

/// Curated signatures to resolve. Kept intentionally small and data-driven:
/// add entries here or load them from `config/offsets.json` as the build is
/// reversed. Empty on purpose — no guessed patterns.
pub fn known_signatures() -> Vec<Signature> {
    Vec::new()
}
