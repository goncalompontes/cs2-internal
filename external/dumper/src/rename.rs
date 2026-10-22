//! Symbol renaming.
//!
//! Target (Source 2) symbols carry Hungarian/Source prefixes and suffixes
//! (`C_`, `m_i`, `m_p`, `m_vec`, `_t`, …). Per ADR-0010 our identifiers follow
//! Rust conventions instead: no prefixes, `snake_case` for members, `PascalCase`
//! for types. This module derives those names, while callers keep the original
//! target string as a mapping key so lookups still work.

/// Tags that prefix a member but are not type information.
const MEMBER_TAGS: &[&str] = &["m_", "g_", "s_", "p_"];

/// Hungarian type prefixes, longest first so `fl`/`vec` beat `f`/`v`.
const HUNGARIAN: &[&str] = &[
    "pp", "psz", "pfn", "vec", "ang", "fl", "ull", "ll", "ui", "ul", "us", "un", "sz", "clr", "ch",
    "p", "b", "i", "n", "f", "u", "w", "h", "e", "q", "s", "c", "t", "d", "o", "r", "k",
];

/// Primitive/keyword tokens kept verbatim in a type display.
const PRIMITIVES: &[&str] = &[
    "void", "bool", "char", "short", "int", "long", "unsigned", "signed", "float", "double",
    "byte", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float32",
    "float64", "const", "struct", "class", "enum", "auto",
];

/// A `PascalCase` type name (e.g. `C_CSPlayerPawn` -> `CsPlayerPawn`).
pub fn class(target: &str) -> String {
    let stripped = strip_class(target);
    let words = split_words(&stripped);
    join_pascal(&words, target)
}

/// A `snake_case` member identifier (e.g. `m_iHealth` -> `health`).
pub fn member(target: &str) -> String {
    let stripped = strip_member(target);
    let words = split_words(&stripped);
    join_snake(&words, target)
}

/// A `SCREAMING_SNAKE_CASE` constant name.
pub fn constant(target: &str) -> String {
    member(target).to_ascii_uppercase()
}

/// Rewrite the identifier tokens inside a C++ type expression, preserving
/// pointers, templates and primitives (e.g. `CCSPlayer_BulletServices*` ->
/// `CsPlayerBulletServices*`).
pub fn type_display(target: &str) -> String {
    let mut out = String::with_capacity(target.len());
    let mut token = String::new();
    let flush = |token: &mut String, out: &mut String| {
        if token.is_empty() {
            return;
        }
        let lower = token.to_ascii_lowercase();
        if PRIMITIVES.contains(&lower.as_str()) {
            out.push_str(token);
        } else {
            out.push_str(&class(token));
        }
        token.clear();
    };
    for c in target.chars() {
        if c.is_ascii_alphanumeric() || c == '_' {
            token.push(c);
        } else {
            flush(&mut token, &mut out);
            out.push(c);
        }
    }
    flush(&mut token, &mut out);
    out
}

fn strip_class(target: &str) -> String {
    let s = target.trim().replace("::", "_");
    // Strip the Source class tag exactly once: `C_` or a single `C` before an
    // uppercase letter.
    let s = if let Some(rest) = s.strip_prefix("C_") {
        rest.to_string()
    } else if let Some(rest) = s.strip_prefix('C') {
        if rest.starts_with(|c: char| c.is_ascii_uppercase()) {
            rest.to_string()
        } else {
            s
        }
    } else {
        s
    };
    s.strip_suffix("_t").map(str::to_string).unwrap_or(s)
}

fn strip_member(target: &str) -> String {
    let mut s = target.trim().to_string();
    for tag in MEMBER_TAGS {
        if let Some(rest) = s.strip_prefix(tag) {
            s = rest.to_string();
            break;
        }
    }
    for h in HUNGARIAN {
        if let Some(rest) = s.strip_prefix(h) {
            if rest.starts_with(|c: char| c.is_ascii_uppercase() || c.is_ascii_digit()) {
                s = rest.to_string();
                break;
            }
        }
    }
    strip_class_tag(&s)
}

/// Strip a leading Source class tag (`C_` or a single `C` before an uppercase
/// letter) without the trailing-suffix handling of [`strip_class`].
fn strip_class_tag(s: &str) -> String {
    if let Some(rest) = s.strip_prefix("C_") {
        rest.to_string()
    } else if let Some(rest) = s.strip_prefix('C') {
        if rest.starts_with(|c: char| c.is_ascii_uppercase()) {
            rest.to_string()
        } else {
            s.to_string()
        }
    } else {
        s.to_string()
    }
}

fn split_words(s: &str) -> Vec<String> {
    let chars: Vec<char> = s.chars().collect();
    let mut words = Vec::new();
    let mut cur = String::new();
    for (i, &c) in chars.iter().enumerate() {
        if !c.is_ascii_alphanumeric() {
            if !cur.is_empty() {
                words.push(std::mem::take(&mut cur));
            }
            continue;
        }
        if !cur.is_empty() {
            let prev = cur.chars().last().unwrap();
            let next = chars.get(i + 1).copied();
            let boundary = (prev.is_ascii_lowercase() || prev.is_ascii_digit())
                && c.is_ascii_uppercase()
                || prev.is_ascii_uppercase()
                    && c.is_ascii_uppercase()
                    && next.is_some_and(|n| n.is_ascii_lowercase());
            if boundary {
                words.push(std::mem::take(&mut cur));
            }
        }
        cur.push(c);
    }
    if !cur.is_empty() {
        words.push(cur);
    }
    words
}

fn join_snake(words: &[String], fallback: &str) -> String {
    let joined: Vec<String> = words.iter().map(|w| w.to_ascii_lowercase()).collect();
    let s = joined.join("_");
    sanitize_ident(&s, fallback)
}

fn join_pascal(words: &[String], fallback: &str) -> String {
    let mut out = String::new();
    for w in words {
        let mut chars = w.chars();
        if let Some(first) = chars.next() {
            out.extend(first.to_uppercase());
            out.push_str(&chars.as_str().to_ascii_lowercase());
        }
    }
    sanitize_ident(&out, fallback)
}

/// Ensure a valid, non-empty identifier; fall back to a sanitized original.
fn sanitize_ident(s: &str, fallback: &str) -> String {
    let cleaned: String = s
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
        .collect();
    let cleaned = cleaned.trim_matches('_');
    if cleaned.is_empty() || cleaned.chars().next().unwrap().is_ascii_digit() {
        let fb: String = fallback
            .chars()
            .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
            .collect();
        format!("symbol_{}", fb.trim_matches('_'))
    } else {
        cleaned.to_string()
    }
}

/// Make `name` unique against `used`, suffixing `_2`, `_3`, … on collision.
pub fn uniquify(name: String, used: &mut std::collections::BTreeSet<String>) -> String {
    if used.insert(name.clone()) {
        return name;
    }
    for n in 2.. {
        let candidate = format!("{name}_{n}");
        if used.insert(candidate.clone()) {
            return candidate;
        }
    }
    unreachable!()
}

/// Rewrite a set of interface registrations to our naming, preserving each
/// original target name in `target`. Identifiers are unique across the list.
pub fn apply_interfaces(items: &mut [crate::interfaces::Interface]) {
    let mut used = std::collections::BTreeSet::new();
    for it in items.iter_mut() {
        let original = original_of(std::mem::take(&mut it.target), &it.name);
        it.name = uniquify(member(&original), &mut used);
        it.target = original;
    }
}

/// Rewrite schema classes/fields to our naming. Class names are unique per
/// module; field names are unique per class. The original target names are kept
/// in `target_name`, and field type displays are rewritten too.
pub fn apply_schema(classes: &mut [crate::schema::SchemaClass]) {
    use std::collections::{BTreeMap, BTreeSet};
    let mut used_by_module: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
    for c in classes.iter_mut() {
        let original = original_of(std::mem::take(&mut c.target_name), &c.name);
        let used = used_by_module.entry(c.module.clone()).or_default();
        c.name = uniquify(class(&original), used);
        c.target_name = original;

        let mut used_fields = BTreeSet::new();
        for f in c.fields.iter_mut() {
            let field_original = original_of(std::mem::take(&mut f.target_name), &f.name);
            f.name = uniquify(member(&field_original), &mut used_fields);
            f.target_name = field_original;
            f.type_name = type_display(&f.type_name);
        }
    }
}

fn original_of(target: String, fallback: &str) -> String {
    if target.is_empty() {
        fallback.to_string()
    } else {
        target
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn class_names_drop_their_tags() {
        assert_eq!(class("C_CSPlayerPawn"), "CsPlayerPawn");
        assert_eq!(class("CCSPlayerPawn"), "CsPlayerPawn");
        assert_eq!(class("CBaseEntity"), "BaseEntity");
        assert_eq!(class("C_EnvWindShared"), "EnvWindShared");
        assert_eq!(
            class("CSceneEntity::QueuedEvents_t"),
            "SceneEntityQueuedEvents"
        );
        assert_eq!(
            class("CCSGO_TeamIntroCharacterPosition"),
            "CsgoTeamIntroCharacterPosition"
        );
        assert_eq!(
            class("InfoForResourceTypeManifestTestResource_t"),
            "InfoForResourceTypeManifestTestResource"
        );
    }

    #[test]
    fn member_names_drop_tags_and_hungarian() {
        assert_eq!(member("m_iHealth"), "health");
        assert_eq!(member("m_pBulletServices"), "bullet_services");
        assert_eq!(member("m_vecOrigin"), "origin");
        assert_eq!(member("m_nRandom"), "random");
        assert_eq!(member("m_flSimulationTime"), "simulation_time");
        assert_eq!(member("m_szName"), "name");
        assert_eq!(member("m_hOwnerEntity"), "owner_entity");
        assert_eq!(member("m_angEyeAngles"), "eye_angles");
        assert_eq!(member("m_bSpotted"), "spotted");
        // Class tag inside a member name is stripped too.
        assert_eq!(member("m_CBodyComponent"), "body_component");
        // No false strip when the remainder is lower-case.
        assert_eq!(member("m_name"), "name");
        assert_eq!(member("m_integer"), "integer");
    }

    #[test]
    fn constants_and_types() {
        assert_eq!(constant("m_iTeamNum"), "TEAM_NUM");
        assert_eq!(
            type_display("CCSPlayer_BulletServices*"),
            "CsPlayerBulletServices*"
        );
        assert_eq!(type_display("int32"), "int32");
        assert_eq!(
            type_display("CHandle< C_BaseEntity >"),
            "Handle< BaseEntity >"
        );
        assert_eq!(type_display("CUtlString"), "UtlString");
    }

    #[test]
    fn uniquify_suffixes_collisions() {
        let mut used = std::collections::BTreeSet::new();
        assert_eq!(uniquify("a".into(), &mut used), "a");
        assert_eq!(uniquify("a".into(), &mut used), "a_2");
        assert_eq!(uniquify("a".into(), &mut used), "a_3");
    }
}
