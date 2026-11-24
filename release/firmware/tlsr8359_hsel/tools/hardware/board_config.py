"""Bounded, format-neutral host loader for PCB Full configuration data.

This module never opens a programmer, imports a firmware image or infers a
board. Callers must validate the returned mapping against the *same* board
schema and admitted base artifact before preparing a write plan. TOML is the
default preset format; extension selection is explicit, never content sniffed.
"""
from __future__ import annotations

import json
import re
import tomllib
from pathlib import Path

DEFAULT_FORMAT = "toml"
MAX_BYTES = 65536
MAX_DEPTH = 16
MAX_NODES = 4096
SUFFIXES = {".toml", ".yaml", ".yml", ".json"}


class ConfigurationError(ValueError):
    """Input could not be decoded unambiguously as board configuration data."""


def _unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if not isinstance(key, str):
            raise ConfigurationError("configuration keys must be strings")
        if key in result:
            raise ConfigurationError(f"duplicate configuration key: {key!r}")
        result[key] = value
    return result


def _yaml(text: str):
    try:
        import yaml
    except ImportError as exc:
        raise ConfigurationError(
            "YAML requires PyYAML; install tools/hardware/requirements.txt"
        ) from exc

    # Board files are plain data: neither references nor custom constructors.
    # Scan before construction so even unused aliases/tags cannot be hidden.
    for token in yaml.scan(text):
        if isinstance(token, (yaml.tokens.AliasToken, yaml.tokens.AnchorToken,
                              yaml.tokens.TagToken)):
            raise ConfigurationError("YAML aliases, anchors and tags are not supported")

    class Loader(yaml.SafeLoader):
        pass

    # Use YAML 1.2 booleans. PyYAML's YAML 1.1 resolver treats on/off/yes/no as
    # booleans, unlike TOML/JSON; do not modify the process-wide SafeLoader.
    Loader.yaml_implicit_resolvers = {
        key: [(tag, regex) for tag, regex in entries
              if tag != "tag:yaml.org,2002:bool"]
        for key, entries in yaml.SafeLoader.yaml_implicit_resolvers.items()
    }
    Loader.add_implicit_resolver("tag:yaml.org,2002:bool",
                                 re.compile(r"^(?:true|false)$"), list("tf"))

    def mapping(loader, node):
        pairs = []
        for key_node, value_node in node.value:
            # Do not flatten mapping merges, even when no alias is involved.
            if key_node.tag == "tag:yaml.org,2002:merge":
                raise ConfigurationError("YAML mapping merges are not supported")
            key = loader.construct_object(key_node, deep=True)
            value = loader.construct_object(value_node, deep=True)
            pairs.append((key, value))
        return _unique_pairs(pairs)

    Loader.add_constructor("tag:yaml.org,2002:map", mapping)
    return yaml.load(text, Loader=Loader)


def _validate_tree(root):
    if not isinstance(root, dict):
        raise ConfigurationError("configuration root must be a mapping")
    count = 0

    def walk(value, depth):
        nonlocal count
        count += 1
        if count > MAX_NODES or depth > MAX_DEPTH:
            raise ConfigurationError("configuration exceeds structural limits")
        if isinstance(value, dict):
            for key, child in value.items():
                if not isinstance(key, str) or not key or any(ord(c) < 32 for c in key):
                    raise ConfigurationError("configuration keys must be nonempty plain strings")
                walk(child, depth + 1)
        elif isinstance(value, list):
            for child in value:
                walk(child, depth + 1)
        elif type(value) is int:
            if not -(1 << 63) <= value < (1 << 63):
                raise ConfigurationError("configuration integer exceeds signed 64-bit range")
        elif type(value) is str:
            if any(ord(c) < 32 or ord(c) == 127 for c in value):
                raise ConfigurationError("configuration strings must not contain controls")
        elif value is not None and type(value) is not bool:
            raise ConfigurationError("configuration permits strings, integers, booleans and null only")

    walk(root, 0)
    return root


def load_board_config(path: str | Path) -> dict:
    """Decode plain configuration data; caller still must run board validation."""
    path = Path(path)
    suffix = path.suffix.lower()
    if suffix not in SUFFIXES:
        raise ConfigurationError("configuration extension must be .toml, .yaml, .yml or .json")
    try:
        # Bound the actual read as well as parsing; a growing file cannot evade it.
        with path.open("rb") as stream:
            raw = stream.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ConfigurationError("configuration exceeds 64 KiB")
        text = raw.decode("utf-8-sig")
        if suffix == ".toml":
            value = tomllib.loads(text)
        elif suffix == ".json":
            def reject_constant(value):
                raise ConfigurationError(f"invalid JSON constant: {value}")
            value = json.loads(text, object_pairs_hook=_unique_pairs,
                               parse_constant=reject_constant)
        else:
            value = _yaml(text)
        return _validate_tree(value)
    except ConfigurationError:
        raise
    except (OSError, UnicodeError, ValueError, RecursionError) as exc:
        raise ConfigurationError(f"invalid {suffix[1:]} configuration: {exc}") from exc
    except Exception as exc:
        # YAML's parser errors are not ValueError subclasses. Do not expose
        # format-specific exceptions to a programmer front door.
        if exc.__class__.__module__.startswith("yaml"):
            raise ConfigurationError(f"invalid YAML configuration: {exc}") from exc
        raise
