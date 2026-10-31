#!/usr/bin/env python3
"""Validate the public InkTag release repository and optional release assets."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path
from typing import Iterable
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from release_checks import public_readiness, verify_material_delivery, download_materials


FORBIDDEN_PARTS = {".sdk", ".tools", ".vendor", "_build", "backups", "factory-backups", "factory_backups"}
LOCAL_ONLY_ROOTS = {".git", ".release-assets", ".release-local", ".audit-local"}
AGENT_INSTRUCTIONS = {"agents.md", "agent.md", "claude.md", "gemini.md", ".cursorrules", "copilot-instructions.md"}
DEVELOPMENT_MODULES = {"_diagnostics.py", "panel_diagnostic.py", "clock_diagnostic.py", "input_simulation.py"}
DEVELOPMENT_FLAGS = (b"--test-bad-signature", b"--interrupt-after-percent")
FIRMWARE_SUFFIXES = {".bin", ".hex", ".img", ".uf2", ".zip"}
FORBIDDEN_SUFFIXES = {
    ".a",
    ".axf",
    ".c",
    ".cpp",
    ".elf",
    ".h",
    ".hpp",
    ".key",
    ".lib",
    ".map",
    ".o",
    ".pem",
    ".sqlite", ".sqlite3", ".db", ".p12", ".pfx", ".log", ".dump", ".bak",
}
SECRET_PATTERNS = (
    re.compile(rb"ghp_[A-Za-z0-9]{20,}"),
    re.compile(rb"github_pat_[A-Za-z0-9_]{20,}"),
    re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def public_cli_failures(name: str, data: bytes) -> list[str]:
    relative = Path(name)
    failures = []
    if relative.name in DEVELOPMENT_MODULES:
        failures.append(f"development-only CLI module: {name}")
    if 'inktag_cli' in relative.parts and relative.suffix == '.py':
        if any(flag in data for flag in DEVELOPMENT_FLAGS):
            failures.append(f"development-only CLI option: {name}")
        if relative.name == 'app.py' and re.search(rb'add_typer\([^\n]*name=[\"\x27]test[\"\x27]', data):
            failures.append(f"development-only CLI command group: {name}")
    return failures


def repository_files(repo: Path, include_untracked: bool = False) -> Iterable[Path]:
    try:
        # Do not let Git walk up into a parent private-source repository when
        # a disposable release staging directory lives below it.
        if not (repo / ".git").exists():
            raise subprocess.CalledProcessError(1, "git ls-files")
        command = ["git", "ls-files"]
        if include_untracked:
            command.extend(["--cached", "--others", "--exclude-standard"])
        command.append("-z")
        output = subprocess.run(
            command,
            cwd=repo,
            check=True,
            capture_output=True,
        ).stdout
        entries = [entry for entry in output.split(b"\0") if entry]
        if not entries:
            raise subprocess.CalledProcessError(1, "git ls-files")
        for entry in entries:
            yield repo / entry.decode("utf-8")
    except (OSError, subprocess.CalledProcessError, UnicodeDecodeError):
        yield from (path for path in repo.rglob("*") if path.is_file() and ".git" not in path.parts)


def public_programmer_files(repo):
    path = repo / 'firmware/manifest.json'
    if not path.is_file(): return {}, []
    try:
        manifest = json.loads(path.read_text('utf8'))
        if not manifest.get('programmerTools'): return {}, []
        spec = importlib.util.spec_from_file_location('public_programmer_inventory', Path(__file__).with_name('firmware_inventory.py'))
        module = importlib.util.module_from_spec(spec)
        prior = sys.dont_write_bytecode
        try:
            sys.dont_write_bytecode = True; spec.loader.exec_module(module)
        finally: sys.dont_write_bytecode = prior
        # A Git clone has the native mirrors, not the ignored attachment cache.
        # validate_assets independently checks actual upload/download bytes.
        return module.programmer_files(repo, manifest, require_assets=False)
    except (ValueError, OSError, ImportError) as error:
        return {}, ['Programmer inventory unavailable: ' + str(error)]


def validate_public_tree(repo: Path, include_untracked: bool = False) -> list[str]:
    tool_files, failures = public_programmer_files(repo)
    paths = set(repository_files(repo, include_untracked=include_untracked))
    # Instruction files may themselves be Git-ignored. Do not let that bypass
    # publication review; local recovery/evidence and installed runtimes stay local.
    for directory, children, names in os.walk(repo, followlinks=False):
        children[:] = [name for name in children if name not in LOCAL_ONLY_ROOTS | {'.venv', '__pycache__', 'node_modules'}]
        paths.update(Path(directory) / name for name in names if name.lower() in AGENT_INSTRUCTIONS)
    for path in sorted(paths):
        if not path.is_file():
            continue
        relative = path.relative_to(repo)
        is_tool = relative.as_posix() in tool_files
        if relative.parts[0] in LOCAL_ONLY_ROOTS:
            continue
        if path.name.lower() in AGENT_INSTRUCTIONS:
            failures.append(f"agent instruction file is not public release content: {relative}")
        # Firmware/downloads live only in the separately verified manifest-bound
        # asset directory, never as an unlisted payload in the public source tree.
        if path.suffix.lower() in FIRMWARE_SUFFIXES and not is_tool:
            failures.append(f"unlisted firmware/archive in public tree: {relative}")
        lowered_parts = {part.lower() for part in relative.parts}
        if lowered_parts & FORBIDDEN_PARTS and not is_tool:
            failures.append(f"forbidden path: {relative}")
        if path.suffix.lower() in FORBIDDEN_SUFFIXES and not (is_tool and tool_files[relative.as_posix()] == 'dependency'):
            failures.append(f"forbidden source/private file: {relative}")
        data = path.read_bytes()
        failures.extend(public_cli_failures(relative.as_posix(), data))
        for pattern in SECRET_PATTERNS:
            if pattern.search(data):
                failures.append(f"possible credential/private key: {relative}")
        lowered = data.lower()
        private_windows_path = b"d:" + b"\\projects\\ink-tag"
        private_posix_path = b"d:" + b"/projects/ink-tag"
        if private_windows_path in lowered or private_posix_path in lowered:
            failures.append(f"private source path leaked: {relative}")
    return failures


def validate_manifest(repo: Path) -> tuple[dict, list[str]]:
    failures: list[str] = []
    path = repo / "firmware" / "manifest.json"
    if not path.is_file():
        return {}, ["firmware/manifest.json is missing"]
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return {}, [f"firmware/manifest.json is invalid: {exc}"]
    version = str(manifest.get("version", ""))
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?", version):
        failures.append(f"invalid semantic version: {version!r}")
    if manifest.get("schemaVersion") != 2:
        failures.append("unsupported manifest schemaVersion")
    release_tag = str(manifest.get("releaseTag", ""))
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", release_tag):
        failures.append(f"invalid release tag: {release_tag!r}")
    components = manifest.get("components", [])
    if not isinstance(components, list) or not components:
        failures.append("manifest components must be a non-empty list")
        components = []
    elif not set(components) <= {"cli", "webui", "firmware"}:
        failures.append(f"unsupported release components: {components!r}")
    seen_boards: set[str] = set()
    seen_assets: set[str] = set()
    if manifest.get('candidateOnly') is True and manifest.get('releaseReady') is not False:
        failures.append('Local-review firmware cannot be presented as a ready or stable release')
    for board in manifest.get("boards", []):
        board_id = board.get("boardId")
        if not board_id or board_id in seen_boards:
            failures.append(f"missing or duplicate boardId: {board_id!r}")
        seen_boards.add(board_id)
        if board.get('buildTarget') == board_id and board.get('artifacts'):
            otas = [a for a in board['artifacts'] if a.get('type') == 'ota']
            fulls = [a for a in board['artifacts'] if a.get('type') == 'full']
            variants = [a.get('variant') for a in fulls]
            variants_ok = len(fulls) == 1 or (board_id == 'da14585_aesl' and all(isinstance(v, str) for v in variants) and
                len(set(variants)) == len(variants) and
                set(variants) <= {'aesl29','aesl42','aesl42-ssd1683'})
            if (len(otas) != 1 or not fulls or len(otas) + len(fulls) != len(board['artifacts']) or
                    not variants_ok or any(a.get('embeddedOtaSha256') != otas[0].get('sha256') for a in fulls)):
                failures.append('OTA/complete firmware pair is missing or inconsistent: ' + str(board_id))
        if board.get("releaseScope") not in {"candidate", "deferred"}:
            failures.append(f"invalid releaseScope for {board_id}: {board.get('releaseScope')!r}")
        local_review = manifest.get('candidateOnly') is True and manifest.get('releaseReady') is False
        if board.get("releaseScope") == "deferred" and board.get("artifacts") and not local_review:
            failures.append(f"deferred board unexpectedly has artifacts: {board_id}")
        for artifact in board.get("artifacts", []):
            if local_review and (artifact.get('candidateOnly') is not True or artifact.get('qualification') != 'local-review-only'):
                failures.append('Preview artifact does not carry the local-review-only boundary')
            name = artifact.get("name", "")
            expected_fragment = f"InkTag-{board_id}-v{version}-"
            variant = artifact.get('variant')
            qualifier = '-' + str(variant) if variant else ''
            canonical_name = (board.get('buildTarget') == board_id and re.fullmatch(r'\d+\.\d+\.\d+\.\d+', str(board.get('firmwareVersion', ''))) and
                artifact.get('type') in {'ota','full'} and
                (not variant or (isinstance(variant, str) and artifact.get('type') == 'full' and board_id == 'da14585_aesl' and variant in {'aesl29','aesl42','aesl42-ssd1683'})) and
                re.fullmatch(r'InkTag-' + re.escape(board_id) + '-' + artifact['type'] + re.escape(qualifier) + '-v' + re.escape(board['firmwareVersion']) + r'-[0-9a-f]{12}\.(?:zip|bin|img)', name))
            if not name.startswith(expected_fragment) and not canonical_name:
                failures.append(f"artifact name does not match board/version: {name}")
            if name in seen_assets:
                failures.append(f"duplicate artifact name: {name}")
            seen_assets.add(name)
            if not re.fullmatch(r"[0-9a-f]{64}", str(artifact.get("sha256", ""))):
                failures.append(f"invalid SHA-256 in manifest: {name}")
            if int(artifact.get("bytes", 0)) <= 0:
                failures.append(f"invalid artifact size in manifest: {name}")
        if "firmware" not in components and board.get("artifacts"):
            failures.append(f"firmware artifact present when firmware component is not selected: {board_id}")
    firmware_selection = manifest.get("firmwareSelection", {})
    actual_firmware = any(board.get("artifacts") for board in manifest.get("boards", []))
    prepared_snapshot = False
    if (actual_firmware and firmware_selection.get('published') is False and
        firmware_selection.get('publicationRecord') == 'releases/record.json' and
        firmware_selection.get('state') == 'prepared-release-snapshot'):
        # Native mirrors may precede publication. The transaction's ledger and
        # downloaded-byte gates, not mere Git presence, control public feeds.
        try:
            ledger = json.loads((repo/'releases/record.json').read_text('utf-8'))
            entries = [r for r in ledger['releases'] if r['releaseTag'] == release_tag]
            selected = {f['buildTarget']: {(a['type'],a['name'],a['bytes'],a['sha256']) for a in f['artifacts']} for f in entries[0]['firmware']}
            native = {b['buildTarget']: {(a['type'],a['name'],a['bytes'],a['sha256']) for a in b['artifacts']} for b in manifest['boards'] if b.get('artifacts')}
            prepared_snapshot = len(entries) == 1 and ledger.get('firmwareEnabled') is True and selected == native
        except (OSError,ValueError,KeyError,IndexError,TypeError):
            pass
    if not prepared_snapshot and bool(firmware_selection.get("published")) != (actual_firmware and not manifest.get('candidateOnly')):
        failures.append("firmwareSelection.published does not match board artifacts")
    cli = manifest.get("cli")
    if cli:
        expected_names = {f"InkTag-cli-v{version}.zip"}
        dated_tag = re.fullmatch(rf"v{re.escape(version)}-(\d{{8}}\.[1-9]\d*)", release_tag)
        if dated_tag:
            expected_names = {f"InkTag-cli-v{version}-{dated_tag[1]}.zip"}
        if '+' in release_tag:
            expected_names.add(f"InkTag-cli-v{version}-{release_tag.split('+',1)[1]}.zip")
        if cli.get("name") not in expected_names:
            failures.append(f"CLI asset name does not match version: {cli.get('name')}")
        if not re.fullmatch(r"[0-9a-f]{64}", str(cli.get("sha256", ""))):
            failures.append("invalid CLI SHA-256 in manifest")
        if int(cli.get("bytes", 0)) <= 0:
            failures.append("invalid CLI asset size in manifest")
        if cli.get("requiresPython") != ">=3.11":
            failures.append("unsupported CLI Python requirement")
    flash_tools = manifest.get("flashTools")
    tool_files, tool_failures = public_programmer_files(repo)
    failures.extend(tool_failures)
    if "firmware" in components and actual_firmware:
        if not flash_tools and not manifest.get('programmerTools'):
            failures.append("firmware release is missing the flashTools asset")
        elif flash_tools and flash_tools.get("name") != f"InkTag-flash-tools-v{version}.zip":
            failures.append(f"flash tools asset name does not match version: {flash_tools.get('name')}")
        if manifest.get('programmerTools'):
            provided = {t.get('buildTarget') for t in manifest['programmerTools']}
            for board in manifest.get('boards', []):
                if board.get('artifacts') and board.get('buildTarget') not in provided:
                    failures.append('Missing target programmer directory: ' + str(board.get('boardId')))
    elif flash_tools:
        failures.append("flashTools is present without published firmware")
    return manifest, failures


def validate_assets(manifest: dict, assets: Path, repo: Path | None = None) -> list[str]:
    failures: list[str] = []
    expected: dict[str, str] = {}
    firmware_names: set[str] = set()
    for board in manifest.get("boards", []):
        for artifact in board.get("artifacts", []):
            expected[artifact["name"]] = artifact["sha256"]
            firmware_names.add(artifact['name'])
    for key in ("web", "webStandalone", "webResources", "localServer", "flashTools"):
        web_asset = manifest.get(key)
        if web_asset:
            expected[web_asset["name"]] = web_asset["sha256"]
    cli = manifest.get("cli")
    if cli:
        expected[cli["name"]] = cli["sha256"]
    for item in manifest.get('cliDistributions', []):
        cli_version = manifest.get('cliVersion', manifest.get('version'))
        allowed = {f"inktag_cli-{cli_version}-py3-none-any.whl", f"inktag_cli-{cli_version}.tar.gz"}
        if item.get('type') != 'cli' or item.get('name') not in allowed:
            failures.append('Invalid CLI distribution identity')
            continue
        expected[item['name']] = item['sha256']
    if (assets/'RELEASE_NOTES.md').is_file():
        expected['RELEASE_NOTES.md'] = digest(assets/'RELEASE_NOTES.md')
    manifest_name = f"InkTag-v{manifest.get('version')}-manifest.json"
    if (assets / manifest_name).is_file():
        expected[manifest_name] = digest(assets / manifest_name)
    allowed_names = set(expected) | {"SHA256SUMS.txt", "RELEASE_NOTES.md"}
    for path in assets.rglob('*'):
        if path.is_file() and path.relative_to(assets).as_posix() not in allowed_names:
            failures.append(f"unlisted release asset: {path.relative_to(assets)}")
    for name, expected_digest in expected.items():
        if Path(name).name != name or '\\' in name or ':' in name or name in {'.', '..'}:
            failures.append(f"unsafe release asset name: {name}")
            continue
        path = assets / name
        if not path.is_file():
            failures.append(f"release asset is missing: {name}")
        elif digest(path) != expected_digest:
            failures.append(f"release asset SHA-256 mismatch: {name}")
        elif path.suffix.lower() == '.zip':
            try:
                with zipfile.ZipFile(path) as archive:
                    for member in archive.infolist():
                        relative = Path(member.filename)
                        if (relative.name.lower() in AGENT_INSTRUCTIONS or relative.is_absolute() or
                                '..' in relative.parts or '\\' in member.filename or ':' in member.filename or
                                {part.lower() for part in relative.parts} & FORBIDDEN_PARTS or
                                relative.suffix.lower() in FORBIDDEN_SUFFIXES or
                                (relative.suffix.lower() in FIRMWARE_SUFFIXES and name not in firmware_names)):
                            failures.append(f"forbidden release archive member: {name}: {member.filename}")
                        if not member.is_dir():
                            data = archive.read(member)
                            failures.extend(public_cli_failures(member.filename, data))
                            if any(pattern.search(data) for pattern in SECRET_PATTERNS):
                                failures.append(f"possible credential/private key in archive: {name}: {member.filename}")
            except zipfile.BadZipFile:
                failures.append(f"invalid release ZIP: {name}")
        descriptions = ([a for b in manifest.get('boards', []) for a in b.get('artifacts', [])] +
            [manifest.get(k) for k in ('cli','web','webStandalone','webResources','localServer','flashTools')] + manifest.get('cliDistributions', []))
        description = next((a for a in descriptions if a and a.get('name') == name), None)
        if description and path.is_file() and path.stat().st_size != description.get('bytes'):
            failures.append('release asset size mismatch: ' + name)
        if path.is_file() and (name == (cli or {}).get('name') or any(name == a['name'] for a in manifest.get('cliDistributions', []))):
            try:
                spec = importlib.util.spec_from_file_location('artifact_license_checks', Path(__file__).with_name('release_checks.py'))
                checks = importlib.util.module_from_spec(spec)
                previous = sys.dont_write_bytecode
                try:
                    sys.dont_write_bytecode = True
                    spec.loader.exec_module(checks)
                finally:
                    sys.dont_write_bytecode = previous
                failures.extend(checks.check_cli_archive(repo or Path(__file__).resolve().parents[1], path))
            except (ValueError, OSError, KeyError, zipfile.BadZipFile, tarfile.TarError) as error:
                failures.append('Invalid CLI licensing artifact: ' + name + ': ' + str(error))
    checksum_path = assets / "SHA256SUMS.txt"
    if not checksum_path.is_file():
        failures.append("SHA256SUMS.txt is missing")
    else:
        declared: dict[str, str] = {}
        for line in checksum_path.read_text(encoding="ascii").splitlines():
            if not line.strip():
                continue
            match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            if not match:
                failures.append(f"invalid SHA256SUMS line: {line!r}")
                continue
            declared[match.group(2)] = match.group(1)
        for name in declared.keys() - expected.keys():
            failures.append(f"unlisted SHA256SUMS entry: {name}")
        for name, expected_digest in expected.items():
            if declared.get(name) != expected_digest:
                failures.append(f"SHA256SUMS does not match: {name}")
    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Release repository root.")
    parser.add_argument("--assets", type=Path, help="Optional release-assets directory to verify.")
    parser.add_argument("--public", action="store_true", help="Require independent public license/source approval.")
    parser.add_argument("--source-materials-dir", type=Path)
    parser.add_argument("--download-source-materials", action="store_true")
    parser.add_argument(
        "--candidate",
        action="store_true",
        help="Include untracked, non-ignored files from an uncommitted release candidate.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    manifest, failures = validate_manifest(repo)
    failures.extend(validate_public_tree(repo, include_untracked=args.candidate))
    if "webui" in manifest.get("components", []) and not (repo / "webui" / "index.html").is_file():
        failures.append("webui/index.html is missing")
    if args.assets:
        failures.extend(validate_assets(manifest, args.assets.resolve(), repo))
    if args.public:
        failures.extend(public_readiness(repo))
        if not failures:
            if args.download_source_materials:
                with tempfile.TemporaryDirectory(prefix='verify-materials-') as temp:
                    try:
                        download_materials(repo,Path(temp))
                        failures.extend(verify_material_delivery(repo,Path(temp)))
                    except (OSError,ValueError) as error:failures.append('Source download verification failed: '+str(error))
            elif args.source_materials_dir:
                failures.extend(verify_material_delivery(repo,args.source_materials_dir))
            else:failures.append('Actual corresponding-source deliveries must be verified, not only their descriptions')
    if failures:
        print("release verification failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(f"release verification OK: InkTag v{manifest['version']} ({len(manifest['boards'])} boards)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
