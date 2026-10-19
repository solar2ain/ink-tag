#!/usr/bin/env python3
"""Validate the compiled directory uploaded to GitHub Pages, using stdlib only."""
from __future__ import annotations

import argparse
from html.parser import HTMLParser
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

ALLOWED_SUFFIXES = {
    ".html", ".js", ".css", ".json", ".txt", ".wasm", ".png", ".jpg",
    ".jpeg", ".gif", ".webp", ".avif", ".svg", ".ico", ".woff", ".woff2", ".ttf",
}
INSTRUCTIONS = {"agent.md", "agents.md", "claude.md", "gemini.md", "copilot-instructions.md"}
SECRET_MARKERS = (
    re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----"),
    re.compile(rb"ghp_[A-Za-z0-9]{20,}"),
    re.compile(rb"github_pat_[A-Za-z0-9_]{20,}"),
)


class ResourceReferences(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.references: list[tuple[str, str]] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        if tag == "script" and values.get("src"):
            self.references.append(("script", str(values["src"])))
        if tag == "link" and values.get("rel") in {"stylesheet", "modulepreload"} and values.get("href"):
            self.references.append(("resource", str(values["href"])))


def validate(site: Path) -> list[str]:
    site = site.resolve()
    errors: list[str] = []
    if not (site / "index.html").is_file():
        return ["Compiled site index.html is missing"]
    for path in sorted(site.rglob("*")):
        relative = path.relative_to(site)
        if path.is_symlink():
            errors.append(f"Links cannot be deployed: {relative}")
            continue
        if any(part.startswith(".") for part in relative.parts) and relative.as_posix() != ".nojekyll":
            errors.append(f"Hidden content cannot be deployed: {relative}")
            continue
        if not path.is_file():
            continue
        if path.name.lower() in INSTRUCTIONS or (path.suffix.lower() not in ALLOWED_SUFFIXES and path.name != ".nojekyll"):
            errors.append(f"Not a compiled static asset: {relative}")
            continue
        data = path.read_bytes()
        if any(pattern.search(data) for pattern in SECRET_MARKERS):
            errors.append(f"Possible private credential: {relative}")
        if path.suffix.lower() in {".js", ".css"} and b"sourceMappingURL=" in data:
            errors.append(f"Source mapping reference is not allowed: {relative}")
        if path.suffix.lower() == ".html":
            parser = ResourceReferences()
            parser.feed(data.decode("utf-8"))
            for kind, reference in parser.references:
                parsed = urlsplit(reference)
                if parsed.scheme or parsed.netloc:
                    errors.append(f"External executable resource: {relative}")
                    continue
                candidate = (path.parent / unquote(parsed.path)).resolve()
                if not candidate.is_relative_to(site) or not candidate.is_file():
                    errors.append(f"Missing or out-of-site {kind}: {relative}: {reference}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", type=Path, default=Path("webui"))
    args = parser.parse_args()
    errors = validate(args.site)
    if errors:
        for error in errors:
            print(error)
        return 1
    count = sum(path.is_file() for path in args.site.rglob("*"))
    print(f"Pages verification OK: {count} compiled static files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
