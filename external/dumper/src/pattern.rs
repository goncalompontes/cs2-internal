//! IDA-style byte-pattern parsing, scanning and RIP-relative resolution.
//!
//! This is deliberately pure (operates on `&[u8]`) so it is unit-testable with
//! committed fixtures and needs no game process.

use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PatternError {
    Empty,
    BadToken(String),
}

impl fmt::Display for PatternError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PatternError::Empty => write!(f, "pattern is empty"),
            PatternError::BadToken(t) => write!(f, "invalid pattern token: {t:?}"),
        }
    }
}

impl std::error::Error for PatternError {}

/// A compiled byte pattern. `None` is a wildcard (`?` / `??`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Pattern {
    tokens: Vec<Option<u8>>,
}

impl Pattern {
    /// Parse a whitespace-separated pattern such as `48 8B 05 ?? ?? ?? ??`.
    pub fn parse(text: &str) -> Result<Self, PatternError> {
        let mut tokens = Vec::new();
        for raw in text.split_whitespace() {
            match raw {
                "?" | "??" => tokens.push(None),
                hex => {
                    if hex.len() != 2 {
                        return Err(PatternError::BadToken(hex.to_string()));
                    }
                    let byte = u8::from_str_radix(hex, 16)
                        .map_err(|_| PatternError::BadToken(hex.to_string()))?;
                    tokens.push(Some(byte));
                }
            }
        }
        if tokens.is_empty() {
            return Err(PatternError::Empty);
        }
        Ok(Self { tokens })
    }

    pub fn len(&self) -> usize {
        self.tokens.len()
    }

    pub fn is_empty(&self) -> bool {
        self.tokens.is_empty()
    }

    pub fn matches_at(&self, haystack: &[u8], at: usize) -> bool {
        if at + self.tokens.len() > haystack.len() {
            return false;
        }
        self.tokens
            .iter()
            .enumerate()
            .all(|(i, tok)| match tok {
                None => true,
                Some(b) => haystack[at + i] == *b,
            })
    }

    pub fn find_first(&self, haystack: &[u8]) -> Option<usize> {
        if self.is_empty() || self.len() > haystack.len() {
            return None;
        }
        (0..=haystack.len() - self.len()).find(|&i| self.matches_at(haystack, i))
    }

    pub fn find_all(&self, haystack: &[u8]) -> Vec<usize> {
        if self.is_empty() || self.len() > haystack.len() {
            return Vec::new();
        }
        (0..=haystack.len() - self.len())
            .filter(|&i| self.matches_at(haystack, i))
            .collect()
    }
}

/// Resolve a RIP-relative operand: `target = instr_addr + instr_len + disp32`.
pub fn resolve_rip_relative(instr_addr: usize, instr_len: usize, disp: i32) -> usize {
    (instr_addr as i64 + instr_len as i64 + disp as i64) as usize
}

/// Read a little-endian `i32` at `at`, if present.
pub fn read_i32(haystack: &[u8], at: usize) -> Option<i32> {
    let bytes: [u8; 4] = haystack.get(at..at + 4)?.try_into().ok()?;
    Some(i32::from_le_bytes(bytes))
}

/// A named signature to resolve a global/function within a module.
#[derive(Debug, Clone)]
pub struct Signature {
    pub name: String,
    pub module: String,
    pub pattern: Pattern,
    /// Offset (within the match) of the rel32 displacement, when RIP-relative.
    pub rip_offset: Option<usize>,
    /// Instruction length used for RIP-relative resolution.
    pub instr_len: Option<usize>,
}

impl Signature {
    /// Resolve a match to a module RVA. If the signature is RIP-relative, the
    /// displacement at `rip_offset` is decoded and resolved; otherwise the match
    /// start is used.
    pub fn resolve_rva(&self, module_base: usize, match_addr: usize, module_bytes: &[u8]) -> Option<usize> {
        let target = match self.rip_offset {
            None => match_addr,
            Some(off) => {
                let disp = read_i32(module_bytes, match_addr - module_base + off)?;
                let instr_len = self.instr_len?;
                resolve_rip_relative(match_addr, instr_len, disp)
            }
        };
        Some(target - module_base)
    }
}
