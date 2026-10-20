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
import stat
from urllib.parse import urlsplit, unquote
from urllib.request import urlopen


def download_materials(repo: Path, directory: Path, *, external_only=False):
    """Called only by explicit publication/verification, never by packaging."""
    items,problems=material_plan(repo)
    if problems:raise ValueError('; '.join(problems))
    directory.mkdir(parents=True,exist_ok=True)
    for item in items:
        delivery=item['delivery']
        if external_only and delivery['kind']=='release-asset':continue
        name=delivery['name'] if delivery['kind']=='release-asset' else 'source-'+item['id']+'.download'
        target=directory/name
        if target.is_symlink() or not target.resolve().is_relative_to(directory.resolve()):raise ValueError('Unsafe source download target')
        with urlopen(delivery['url'],timeout=20) as response:
            final=urlsplit(response.geturl())
            if final.scheme!='https' or final.hostname not in {'github.com','raw.githubusercontent.com','release-assets.githubusercontent.com','objects.githubusercontent.com'}:
                raise ValueError('Source download redirected outside trusted hosts')
            body=response.read(item['bytes']+1)
        if len(body)!=item['bytes'] or sha(body)!=item['sha256']:
            raise ValueError('Downloaded corresponding-source bytes mismatch: '+item['id'])
        target.write_bytes(body)


def material_binding(items):
    return [{key: item[key] for key in ('id', 'bytes', 'sha256', 'forComponents', 'delivery')} for item in items]


def material_plan(repo: Path) -> tuple[list[dict], list[str]]:
    """Bind preferred-source bytes to runtime bytes and an actual delivery."""
    config=json.loads((repo/'releases/readiness.json').read_text('utf-8'))
    items=config.get('sourceMaterials',[])
    problems=[]
    runtime=config.get('runtimeHashes',{})
    required=set(config.get('requiredSourceComponents',[]))
    native='cli/src/inktag_cli/_assets/nativePreviewWasm.js'
    if (repo/native).is_file():required.add(native)
    if not items: return [], ['Compiled renderer corresponding-source materials are absent']
    covered=set();seen=set();tag=json.loads((repo/'firmware/manifest.json').read_text('utf-8'))['releaseTag']
    record=json.loads((repo/'releases/record.json').read_text('utf-8'))
    release=next((r for r in record['releases'] if r['releaseTag']==tag),{})
    registered=release.get('sourceMaterials',[])
    for item in items:
        try:
            identifier=item['id'];delivery=item['delivery']
            if not isinstance(identifier,str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,80}',identifier) or identifier in seen:raise ValueError('invalid/duplicate ID')
            seen.add(identifier)
            if type(item.get('bytes')) is not int or not 0<item['bytes']<=512*1024*1024 or not re.fullmatch('[a-f0-9]{64}',str(item.get('sha256',''))):raise ValueError('size/SHA256 required')
            bindings=item['forComponents']
            if not isinstance(bindings,list) or not bindings:raise ValueError('runtime component binding required')
            for component in bindings:
                path=component['path']
                if runtime.get(path)!=component.get('sha256'):raise ValueError('runtime hash binding mismatch')
                actual=(repo/path).resolve()
                if not actual.is_relative_to(repo.resolve()) or not actual.is_file() or sha(actual.read_bytes())!=component['sha256']:raise ValueError('runtime bytes mismatch')
                covered.add(path)
            url=delivery['url'];parts=urlsplit(url)
            if parts.scheme!='https' or parts.username or parts.password or parts.query or parts.fragment or parts.port not in {None,443}:raise ValueError('invalid source delivery URL')
            if delivery['kind']=='release-asset':
                name=delivery['name']
                if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,180}',name):raise ValueError('unsafe material filename')
                if url!=f'https://github.com/solar2ain/ink-tag/releases/download/{tag}/{name}':raise ValueError('source attachment URL mismatch')
                expected={'type':'source-material','name':name,'bytes':item['bytes'],'sha256':item['sha256'],'downloadUrl':url}
                if expected not in registered:raise ValueError('source attachment absent from release record')
            elif delivery['kind']=='source-download':
                if parts.hostname!='raw.githubusercontent.com' or not re.fullmatch(r'/solar2ain/ink-tag/[a-f0-9]{40}/[A-Za-z0-9_./-]+',parts.path) or '..' in unquote(parts.path).split('/'):raise ValueError('immutable project source download required')
            else:raise ValueError('explicit delivery kind required')
        except (KeyError,ValueError,TypeError) as error:
            problems.append('Invalid source material delivery: '+str(error))
    if not required<=covered:problems.append('Required compiled runtime has no source material binding')
    return items,problems


def verify_material_delivery(repo: Path, downloaded: Path) -> list[str]:
    """Require actual attachment or external-download bytes, not local existence."""
    items,problems=material_plan(repo)
    for item in items:
        try:
            delivery=item['delivery']
            name=delivery['name'] if delivery['kind']=='release-asset' else 'source-'+item['id']+'.download'
            path=downloaded/name
            if not path.resolve().is_relative_to(downloaded.resolve()) or path.is_symlink() or not path.is_file() or path.stat().st_size!=item['bytes'] or sha(path.read_bytes())!=item['sha256']:
                problems.append('Delivered source bytes missing/mismatched: '+item['id'])
        except (KeyError,TypeError):problems.append('Invalid delivered source description')
    return problems

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
            if any(stat.S_ISLNK(item.external_attr>>16) for item in z.infolist()):raise ValueError('Archive links are forbidden')
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
    _,material_errors=material_plan(repo)
    problems.extend(material_errors)
    # A reviewed materials receipt must bind every runtime byte, not only metadata.
    expected=value.get('runtimeHashes',{})
    actual={p.relative_to(repo).as_posix():sha(p.read_bytes()) for root in ('cli/src','webui/assets') for p in (repo/root).rglob('*') if p.is_file() and '__pycache__' not in p.parts}
    if expected!=actual: problems.append('Reviewed runtime hashes are missing or stale')
    return problems
