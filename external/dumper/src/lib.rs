//! External dumper: offset/interface/schema extraction.
//!
//! Runs as a normal Linux process and reads the target PE (a Linux process at
//! the OS level) via `/proc/<pid>/mem`. Produces offsets, interface maps and
//! generated schema headers for the C++ payload ([`sdk`]).
//!
//! Design note: memory access is behind the [`memory::ProcessMemory`] trait so a
//! `memflow` backend can be added without touching the analysis code.

pub mod analysis;
pub mod interfaces;
pub mod live;
pub mod mcp;
pub mod memory;
pub mod output;
pub mod pattern;
pub mod pe;
pub mod reader;
pub mod schema;
pub mod sdk;

pub use pattern::{resolve_rip_relative, Pattern, Signature};
