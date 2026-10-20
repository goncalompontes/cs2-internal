"""The TypedDict -> JSON Schema bridge: the type safety at the **tool boundary**.

The scenario library is typed (:mod:`scenario.protocol` holds one ``TypedDict``
per wire answer, the constrained strings are ``Literal``s and the screen is an
enum).  An MCP client, however, only ever sees a **JSON Schema** — so the
typing has to be *projected* onto that vocabulary, or the agent is back to
passing `{"line": "sv_cheats 1"}` and hoping.

This module is that projection:

* a ``TypedDict`` becomes an **object** schema with ``properties``,
  ``required`` and ``additionalProperties: false``;
* a ``Literal`` becomes an ``enum``;
* an ``enum.Enum`` (the screen!) becomes an ``enum`` of its values;
* ``int`` / ``float`` / ``str`` / ``bool`` become the obvious JSON types;
* ``list[X]`` becomes an array of ``X`` and ``dict[str, X]`` an object with
  ``additionalProperties: X``;
* ``X | None`` becomes ``anyOf: [X, null]``.

Three rules make the projection trustworthy:

* **A hint that cannot be expressed falls back to ``{}`` (any) and is
  *reported*.**  Emitting a *wrong* schema is worse than emitting none: the
  client would reject a valid call.  The fallbacks are collected in the
  ``problems`` list the caller passes in, so the server can log them.
* **The projection is also a validator.**  :func:`validate_arguments` checks an
  argument object against the very schema that was advertised, so a bad
  argument is rejected **before** it reaches the game — the type safety does
  not stop at the announcement.
* **A recursive shape is expanded to a bounded depth.**  The ``wait_for``
  expression nests (``all`` / ``any`` / ``not``) and JSON Schema has no inline
  recursion without ``$ref``; the bridge expands it
  :data:`MAX_EXPANSION_DEPTH` levels and then says "an object".

The forward references the package uses (``from __future__ import annotations``
turns every annotation into a string) are resolved with
:func:`typing.get_type_hints` and its ``include_extras=True`` flag, which keeps
the ``Required`` / ``NotRequired`` markers the required-key computation needs.
"""

from __future__ import annotations

import enum
import types
import typing
from typing import Any, Literal, Mapping, get_args, get_origin, get_type_hints

__all__ = [
    "MAX_EXPANSION_DEPTH",
    "schema_for",
    "schema_for_hint",
    "validate_arguments",
]


#: How deep a *recursive* shape is expanded before the bridge gives up and
#: says "an object".  The predicate expression of ``wait_for`` is the only
#: recursive shape in the tool surface.
MAX_EXPANSION_DEPTH = 3

#: The resolved annotations of every ``TypedDict`` the bridge has walked.  The
#: resolution is slow (it evaluates every annotation) and the schemas are built
#: once at start-up, but the cache keeps a rebuild cheap.
_HINT_CACHE: dict[type, dict[str, Any]] = {}


def _hints(shape: type) -> dict[str, Any]:
    """The resolved annotations of a ``TypedDict`` (cached)."""
    cached = _HINT_CACHE.get(shape)
    if cached is None:
        cached = get_type_hints(shape, include_extras=True)
        _HINT_CACHE[shape] = cached
    return cached


def _is_typed_dict(hint: object) -> bool:
    """Whether a hint is a ``TypedDict`` (the shape of one wire object)."""
    return isinstance(hint, type) and hasattr(hint, "__required_keys__")


def _required_keys(shape: type, hints: Mapping[str, Any]) -> list[str]:
    """The keys a client must supply.

    ``__required_keys__`` is **not** enough: under
    ``from __future__ import annotations`` the ``Required`` / ``NotRequired``
    markers survive only as strings, so the class attribute cannot see them
    (a ``NotRequired`` key looks required).  The *resolved* annotations can, so
    they win.
    """
    total = bool(getattr(shape, "__total__", True))
    required: list[str] = []
    for key, hint in hints.items():
        origin = get_origin(hint)
        if origin is typing.Required:
            required.append(key)
        elif origin is typing.NotRequired:
            continue
        elif total:
            required.append(key)
    return sorted(required)


def schema_for(shape: type, *, problems: list[str] | None = None,
               depth: int = 0) -> dict[str, Any]:
    """The JSON Schema of a ``TypedDict``.

    ``problems`` collects the annotations the bridge could not express (each
    one falls back to "any"); the caller decides what to do with them.
    """
    if not _is_typed_dict(shape):
        raise TypeError(f"{shape!r} is not a TypedDict")
    if depth >= MAX_EXPANSION_DEPTH:
        # A recursive shape: say "an object" rather than expanding forever.
        _report(problems, f"{shape.__name__}: the shape is recursive — expanded to "
                          f"depth {MAX_EXPANSION_DEPTH}, the nested form is 'an object'")
        return {"type": "object"}
    hints = _hints(shape)
    properties = {
        name: schema_for_hint(hint, f"{shape.__name__}.{name}", problems, depth)
        for name, hint in hints.items()
    }
    return {
        "type": "object",
        "properties": properties,
        "required": _required_keys(shape, hints),
        "additionalProperties": False,
    }


def schema_for_hint(hint: object, path: str = "", problems: list[str] | None = None,
                    depth: int = 0) -> dict[str, Any]:
    """The JSON Schema of one annotation.

    ``path`` names the annotation in the fallback reports (e.g.
    ``"EnsureArgs.target"``) so a reader can find the offending type.
    """
    origin = get_origin(hint)

    # `Required[X]` / `NotRequired[X]` are markers, not types.
    if origin is typing.Required or origin is typing.NotRequired:
        args = get_args(hint)
        return schema_for_hint(args[0] if args else Any, path, problems, depth)

    # `Annotated[X, ...]` carries metadata, not shape.
    if origin is typing.Annotated:
        args = get_args(hint)
        return schema_for_hint(args[0] if args else Any, path, problems, depth)

    # A `Literal` is a closed set of values: an enum.
    if origin is Literal:
        return {"enum": list(get_args(hint))}

    # A union: `X | None` is "X or null", anything else is a choice.
    if origin is typing.Union or origin is types.UnionType:
        args = get_args(hint)
        if type(None) in args:
            rest = [arg for arg in args if arg is not type(None)]
            inner = (schema_for_hint(rest[0], path, problems, depth) if len(rest) == 1
                     else {"anyOf": [schema_for_hint(arg, path, problems, depth)
                                    for arg in rest]})
            return {"anyOf": [inner, {"type": "null"}]}
        return {"anyOf": [schema_for_hint(arg, path, problems, depth) for arg in args]}

    if origin in (list, set, frozenset, tuple):
        args = get_args(hint)
        item = schema_for_hint(args[0], f"{path}[]", problems, depth) if args else {}
        return {"type": "array", "items": item}

    if origin is dict:
        args = get_args(hint)
        value = schema_for_hint(args[1], f"{path}{{}}", problems, depth) if len(args) == 2 else {}
        return {"type": "object", "additionalProperties": value}

    if hint is Any:
        return {}
    if hint is None or hint is type(None):
        return {"type": "null"}
    if hint is bool:
        return {"type": "boolean"}
    if hint is int:
        return {"type": "integer"}
    if hint is float:
        return {"type": "number"}
    if hint is str:
        return {"type": "string"}

    # An enum: the screen is one, and its values are the contract.
    if isinstance(hint, type) and issubclass(hint, enum.Enum):
        values = [member.value for member in hint]
        if all(isinstance(value, str) for value in values):
            return {"type": "string", "enum": values}
        return {"enum": values}

    # A nested wire object.
    if _is_typed_dict(hint):
        return schema_for(hint, problems=problems, depth=depth + 1)

    # Anything else: fall back to "any" and *say so*.
    _report(problems, f"{path or 'annotation'}: {hint!r} is not expressible as JSON "
                      "Schema — falling back to 'any'")
    return {}


def _report(problems: list[str] | None, message: str) -> None:
    """Record a fallback, once per message."""
    if problems is not None and message not in problems:
        problems.append(message)


# --- the validator --------------------------------------------------------
def _type_name(expected: str) -> str:
    """A human name for a JSON type (for the error message)."""
    return {
        "object": "an object",
        "array": "an array",
        "string": "a string",
        "integer": "an integer",
        "number": "a number",
        "boolean": "a boolean",
        "null": "null",
    }.get(expected, expected)


def _matches_type(value: object, expected: str) -> bool:
    """Whether ``value`` is of the JSON type ``expected``."""
    if expected == "object":
        return isinstance(value, Mapping)
    if expected == "array":
        return isinstance(value, (list, tuple))
    if expected == "string":
        return isinstance(value, str)
    if expected == "integer":
        # A bool is an int in Python, but not in JSON.
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "null":
        return value is None
    return True


def validate_arguments(schema: Mapping[str, Any], value: object, path: str = "") -> list[str]:
    """Check an argument object against a generated schema.

    Returns the list of complaints (empty when the value is valid).  Only the
    keywords the bridge *generates* are understood — the validator and the
    generator are two halves of one contract, so they cannot drift.
    """
    errors: list[str] = []
    where = path or "arguments"

    if "anyOf" in schema:
        for branch in schema["anyOf"]:
            if not validate_arguments(branch, value, path):
                return []
        errors.append(f"{where}: {value!r} matches none of the allowed forms")
        return errors

    expected = schema.get("type")
    if isinstance(expected, str) and not _matches_type(value, expected):
        errors.append(f"{where}: expected {_type_name(expected)}, got {_actual(value)}")
        return errors

    if "enum" in schema:
        allowed = schema["enum"]
        if value not in allowed:
            errors.append(f"{where}: {value!r} is not one of {allowed}")

    if expected == "object":
        if not isinstance(value, Mapping):
            return errors
        properties = schema.get("properties") or {}
        for key in schema.get("required") or []:
            if key not in value:
                errors.append(f"{where}: the required argument {key!r} is missing")
        additional = schema.get("additionalProperties", True)
        for key, item in value.items():
            child = f"{path}.{key}" if path else str(key)
            if key in properties:
                errors.extend(validate_arguments(properties[key], item, child))
            elif additional is False:
                errors.append(f"{where}: unknown argument {key!r} "
                              f"(known: {sorted(properties)})")
            elif isinstance(additional, Mapping):
                errors.extend(validate_arguments(additional, item, child))
    elif expected == "array":
        if not isinstance(value, (list, tuple)):
            return errors
        items = schema.get("items") or {}
        for index, item in enumerate(value):
            errors.extend(validate_arguments(items, item,
                                             f"{path}[{index}]" if path else f"[{index}]"))

    return errors


def _actual(value: object) -> str:
    """The name of a value's type (for the error message)."""
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "a boolean"
    if isinstance(value, int):
        return "an integer"
    if isinstance(value, float):
        return "a number"
    if isinstance(value, str):
        return "a string"
    if isinstance(value, Mapping):
        return "an object"
    if isinstance(value, (list, tuple)):
        return "an array"
    return type(value).__name__
