"""Verify the explicit public programmer-file inventory; no hardware or network."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import re

# Reviewed RAM programmer dependency, not an InkTag App/Recovery image.
NRF11_DEPENDENCIES = {
    'tools/assets/full-writer.bin': (1176, 'd3e34c2be735e84c484198b95c9b10c92798a20c1abacfbb8af3e3a26b854acf'),
}


def firmware_files(repo: Path, manifest: dict, *, require_assets=True) -> tuple[dict[str, str], list[str]]:
    """Admit only native firmware bound to the same Release attachment bytes."""
    allowed = {}; failures = []
    enabled = manifest.get('firmwareRepositoryIncluded', False)
    for board in manifest.get('boards', []):
        for item in board.get('artifacts', []):
            relative = item.get('repositoryPath')
            if not enabled and relative is None:
                continue  # Immutable historical manifests predate repository copies.
            try:
                target, kind, name = board['buildTarget'], item['type'], item['name']
                if enabled is not True or not re.fullmatch(r'[a-z0-9]+_(?:zkel|hsel|aesl|priceetag)(?:_internal)?', target):
                    raise ValueError('invalid firmware target or repository selection')
                if kind not in {'ota', 'full'} or PurePosixPath(name).name != name or '\\' in name or ':' in name or PurePosixPath(name).suffix not in {'.zip', '.bin', '.img'}:
                    raise ValueError('invalid native firmware filename or kind')
                if relative != 'firmware/' + target + '/' + kind + '/' + name or relative in allowed:
                    raise ValueError('duplicate or unexpected native firmware path')
                if type(item['bytes']) is not int or not 0 < item['bytes'] <= 16 * 1024 * 1024 or not re.fullmatch(r'[0-9a-f]{64}', item['sha256']):
                    raise ValueError('invalid firmware byte binding')
                paths = [repo / relative]
                if require_assets: paths.append(repo / '.release-assets' / manifest['releaseTag'] / name)
                for path in paths:
                    if not path.resolve().is_relative_to(repo.resolve()):
                        raise ValueError('escaping firmware path')
                    for entry in [path, *path.parents]:
                        if entry == repo: break
                        if entry.is_symlink() or getattr(entry, 'is_junction', lambda: False)():
                            raise ValueError('linked firmware path')
                    if not path.is_file() or path.stat().st_size != item['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
                        raise ValueError('missing firmware or attachment byte mismatch')
                allowed[relative] = 'firmware'
            except (KeyError, TypeError, ValueError, OSError) as exc:
                failures.append('Firmware inventory: ' + str(exc))
    return allowed, failures


def programmer_files(repo: Path, manifest: dict, *, require_assets=True) -> tuple[dict[str, str], list[str]]:
    firmware, failures = firmware_files(repo, manifest, require_assets=require_assets)
    allowed = {}
    for tool in manifest.get('programmerTools', []):
        try:
            target = tool['buildTarget']
            if not re.fullmatch(r'[a-z0-9]+_(?:zkel|hsel|aesl|priceetag)(?:_internal)?', target):
                raise ValueError('invalid build target')
            if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', tool['toolsVersion']):
                raise ValueError('invalid tool version')
            root = 'firmware/' + target
            name = tool['inventory']
            if tool['root'] != root or name not in {'firmware/tool-manifests/' + target + '.json', 'releases/tool-manifests/' + target + '.json'} or name in allowed:
                raise ValueError('duplicate or unexpected tool inventory')
            def bound(relative):
                p = PurePosixPath(relative)
                if not relative or '\\' in relative or ':' in relative or p.is_absolute() or '..' in p.parts:
                    raise ValueError('unsafe tool path')
                path = repo.joinpath(*p.parts)
                if not path.resolve().is_relative_to(repo.resolve()): raise ValueError('escaping tool path')
                for entry in [path, *path.parents]:
                    if entry == repo: break
                    if entry.is_symlink() or getattr(entry, 'is_junction', lambda: False)(): raise ValueError('linked tool path')
                return path
            def check(path, ref):
                if not path.is_file() or type(ref['bytes']) is not int or not re.fullmatch(r'[0-9a-f]{64}', ref['sha256']):
                    raise ValueError('missing tool file or invalid binding')
                if path.stat().st_size != ref['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest() != ref['sha256']:
                    raise ValueError('tool file byte mismatch')
            index = bound(name); check(index, tool)
            if index.stat().st_size > 4 * 1024 * 1024: raise ValueError('oversized tool inventory')
            inventory = json.loads(index.read_text('utf-8'))
            if inventory.get('schemaVersion') != 1 or not 0 < len(inventory['files']) <= 10000:
                raise ValueError('invalid tool inventory')
            allowed[name] = 'inventory'
            seen = set()
            for ref in inventory['files']:
                relative = ref['path']; p = PurePosixPath(relative)
                if not relative.startswith(root + '/') or relative.casefold() in seen:
                    raise ValueError('duplicate or outside tool member')
                seen.add(relative.casefold())
                forbidden = {'.git', '.sdk', '.secrets', '.local', 'node_modules', 'backups', 'factory_backups', '__pycache__'}
                if {v.lower() for v in p.parts} & forbidden or p.name.lower() in {'agents.md','agent.md','claude.md','gemini.md'}:
                    raise ValueError('private tool content')
                dependency_name = relative[len(root)+1:].removeprefix('tools/internal/native/')
                approved = (target == 'nrf52811_aesl' and ref.get('category') == 'dependency' and
                    NRF11_DEPENDENCIES.get(dependency_name) == (ref.get('bytes'), ref.get('sha256')))
                if (p.suffix.lower() in {'.c','.h','.cpp','.hpp','.pem','.key','.pfx','.p12','.log','.db','.sqlite','.pcap','.elf','.map','.hex','.img','.ota'} and not approved) or (
                        p.suffix.lower() == '.bin' and p.name not in {'public.bin','public-key.bin'} and not approved):
                    raise ValueError('source/private tool content')
                if '.tools' in p.parts and not (target == 'tlsr8359_hsel' and
                        relative in {root + prefix + '/.tools/telink/TlsrComSwireWriter/' + leaf
                                     for prefix in ('', '/tools/internal/native')
                                     for leaf in ('LICENSE','README.md','TLSR825xComFlasher.py')}):
                    raise ValueError('unreviewed vendor tools directory')
                category = ref.get('category', 'runtime')
                if category not in {'runtime','dependency'}: raise ValueError('invalid tool file category')
                if p.suffix.lower() in {'.dll','.exe','.pyd','.whl','.zip','.gz','._pth'} and category != 'dependency':
                    raise ValueError('runtime binary needs explicit dependency classification')
                check(bound(relative), ref)
                allowed[relative] = category
            members = list(bound(root).rglob('*'))
            if any(p.is_symlink() or getattr(p, 'is_junction', lambda: False)() for p in members):
                raise ValueError('linked tool content')
            actual = {p.relative_to(repo).as_posix().casefold() for p in members if p.is_file() and p.relative_to(repo).as_posix() not in firmware}
            if actual != seen: raise ValueError('extra or missing tool runtime content')
        except (KeyError, TypeError, ValueError, OSError) as exc:
            failures.append('Programmer inventory: ' + str(exc))
    allowed.update(firmware)
    return allowed, failures
