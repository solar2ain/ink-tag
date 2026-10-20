"""Offline license and first-release gates for the actual distributed bytes."""
from __future__ import annotations
import email
import hashlib
import json
from pathlib import Path
import tarfile
import zipfile
import re
import tomllib

def sha(data): return hashlib.sha256(data).hexdigest()

def cli_license_files(repo: Path) -> dict[str, bytes]:
    cli = repo/'cli'
    names = ['LICENSE','LICENSING.md','THIRD_PARTY_NOTICES.md']
    names += sorted(p.relative_to(cli).as_posix() for p in (cli/'licenses').glob('*.txt'))
    return {n:(cli/n).read_bytes() for n in names}

def archive_files(path: Path) -> dict[str, bytes]:
    if path.suffix in {'.zip','.whl'}:
        with zipfile.ZipFile(path) as z:
            names=z.namelist()
            if len(names)!=len(set(names)): raise ValueError('Duplicate archive entries')
            return {n:z.read(n) for n in names if not n.endswith('/')}
    if path.name.endswith('.tar.gz'):
        with tarfile.open(path, 'r:gz') as z:
            members=z.getmembers()
            if any(not (m.isfile() or m.isdir()) for m in members): raise ValueError('Archive links are forbidden')
            names=[m.name for m in members]
            if len(names)!=len(set(names)): raise ValueError('Duplicate archive entries')
            return {m.name:z.extractfile(m).read() for m in members if m.isfile()}
    raise ValueError('Unsupported CLI archive format')

def check_cli_archive(repo: Path, path: Path) -> list[str]:
    problems=[]; contents=archive_files(path)
    for name in contents:
        if name.startswith('/') or '..' in Path(name).parts or '\\' in name or ':' in name:
            problems.append('unsafe CLI archive entry: '+name)
        leaf=Path(name).name.lower()
        if leaf in {'agents.md','agent.md','claude.md','gemini.md','_diagnostics.py','panel_diagnostic.py','clock_diagnostic.py','input_simulation.py'} or Path(name).suffix.lower() in {'.pem','.key','.db','.sqlite','.sqlite3','.bin','.hex','.elf','.c','.h','.map','.zip'}:
            problems.append('Forbidden CLI artifact member: '+name)
        data=contents[name]
        if re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----|ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}',data):
            problems.append('Possible private credential in CLI artifact: '+name)
        if name.endswith('.py') and any(x in data for x in (b'--test-bad-signature',b'--interrupt-after-percent')):
            problems.append('Development-only option in CLI artifact: '+name)
    required=cli_license_files(repo)
    for name,data in required.items():
        suffix=('/licenses/'+name if path.suffix=='.whl' else '/'+name)
        matches=[p for p in contents if p.endswith(suffix)]
        if len(matches)!=1 or contents[matches[0]]!=data:
            problems.append('CLI artifact missing/mismatched license: '+name)
    for asset in ('fontRoles.js','nativePreview.js','nativePreviewWasm.js','hostScene.js'):
        matching=[data for name,data in contents.items() if name.endswith('inktag_cli/_assets/'+asset)]
        if len(matching)!=1 or matching[0]!=(repo/'cli/src/inktag_cli/_assets'/asset).read_bytes():
            problems.append('Missing/changed normal CLI resource: '+asset)
    if path.suffix=='.zip':
        pyprojects=[data for name,data in contents.items() if name.count('/')==1 and name.endswith('/pyproject.toml')]
        if len(pyprojects)!=1 or tomllib.loads(pyprojects[0].decode())['project'].get('license')!='AGPL-3.0-only':
            problems.append('Source ZIP license metadata mismatch')
    metadata=[v for n,v in contents.items() if n.endswith('/METADATA') or n.count('/')==1 and n.endswith('/PKG-INFO')]
    if path.suffix=='.whl' or path.name.endswith('.tar.gz'):
        if len(metadata)!=1:
            problems.append('CLI artifact metadata missing/ambiguous')
        else:
            value=email.message_from_bytes(metadata[0])
            if value.get('License-Expression')!='AGPL-3.0-only': problems.append('CLI License-Expression mismatch')
            if set(value.get_all('License-File',[]))!=set(required): problems.append('CLI License-File metadata incomplete')
    return problems

def public_readiness(repo: Path) -> list[str]:
    path=repo/'releases/readiness.json'
    if not path.is_file(): return ['Public licensing/readiness review is missing']
    value=json.loads(path.read_text('utf-8'))
    required=('cliCorrespondingSourceVerified','webuiBinaryTermsApproved','thirdPartyInventoryVerified')
    problems=[key+' is not independently verified' for key in required if value.get(key) is not True]
    if value.get('publicReleaseApproved') is not True: problems.append('Public release approval is absent')
    materials=value.get('sourceMaterials',[])
    if not materials:problems.append('Compiled renderer corresponding-source materials are absent')
    for item in materials:
        path=(repo/item.get('path','')).resolve()
        if not path.is_relative_to(repo.resolve()) or not path.is_file() or sha(path.read_bytes())!=item.get('sha256'):
            problems.append('Source material is missing or mismatched')
    # A reviewed materials receipt must bind every runtime byte, not only metadata.
    expected=value.get('runtimeHashes',{})
    actual={p.relative_to(repo).as_posix():sha(p.read_bytes()) for root in ('cli/src','webui/assets') for p in (repo/root).rglob('*') if p.is_file() and '__pycache__' not in p.parts}
    if expected!=actual: problems.append('Reviewed runtime hashes are missing or stale')
    return problems
