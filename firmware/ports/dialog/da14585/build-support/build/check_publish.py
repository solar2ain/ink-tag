"""Check the actual Git index and optional reachable history before source publication."""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import runpy
import subprocess
import urllib.parse

PRIVATE_DIRS = {'.secrets', '.local', '.sdk', '.venv', '.codex', '.claude',
                'backups', 'factory_backups', 'device_backups', 'device-backups',
                'device_captures', 'research', 'history', 'handoff', 'reports', 'debug'}
PRIVATE_NAMES = {'agent.md', 'agents.md', 'agents.local.md', 'claude.md',
                 'gemini.md', 'copilot-instructions.md', 'ota-signing.json', 'makefile.private', 'makefile.signed', '.env'}
PRIVATE_KEY = re.compile(rb'-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----')
_source_scope = runpy.run_path(str(Path(__file__).with_name('source_scope.py')))
implementation_kind = _source_scope['implementation_kind']
free_implementation_error = _source_scope['free_implementation_error']

def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])

def check_member(name, mode, data):
    path = PurePosixPath(name)
    parts = {p.lower() for p in path.parts}
    if mode not in ('100644', '100755'):
        raise ValueError('Non-regular Git member refused: ' + name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError('Unsafe Git path: ' + name)
    if parts & PRIVATE_DIRS or path.name.lower() in PRIVATE_NAMES or path.name.lower().startswith('.env.'):
        raise ValueError('Private Git member refused: ' + name)
    kind = implementation_kind(name)
    if kind:
        raise ValueError(kind + ' implementation refused from DA Git tree: ' + name)
    error = free_implementation_error(name, data)
    if error:
        raise ValueError(error + ': ' + name)
    if path.suffix.lower() in {'.bin', '.img', '.hex', '.axf', '.elf', '.pfx', '.p12', '.jks', '.keystore'}:
        raise ValueError('Binary/device/key container refused: ' + name)
    if PRIVATE_KEY.search(data):
        raise ValueError('Private key bytes refused: ' + name)
    if path.suffix.lower() == '.md':
        for url in re.findall(rb'https://qun\.qq\.com/universal-share/share\?[^\s)<>]+', data):
            query = urllib.parse.parse_qs(urllib.parse.urlparse(url.decode('utf-8')).query)
            if any(key.lower() == 'authkey' for key in query):
                raise ValueError('Personal QQ share authorization refused: ' + name)
            if 'busi_data' in query:
                try:
                    payload = json.loads(base64.b64decode(query['busi_data'][0], validate=True))
                except (ValueError, TypeError) as error:
                    raise ValueError('Unreadable QQ share metadata refused: ' + name) from error
                if not isinstance(payload, dict) or set(payload) - {'groupCode', 'token'}:
                    raise ValueError('Personal QQ sharer metadata refused: ' + name)

def members(repo, ref=None):
    command = ('ls-tree', '-r', '-z', ref) if ref else ('ls-files', '--stage', '-z')
    result = {}
    for entry in git(repo, *command).split(b'\0'):
        if not entry:
            continue
        metadata, raw_name = entry.split(b'\t', 1)
        fields = metadata.decode('ascii').split()
        mode, kind, oid = fields if ref else (fields[0], fields[2], fields[1])
        name = raw_name.decode('utf-8')
        if (ref and kind != 'blob') or (not ref and kind != '0'):
            raise ValueError('Unmerged/non-blob Git member refused: ' + name)
        if mode not in ('100644', '100755'):
            raise ValueError('Non-regular Git member refused: ' + name)
        data = git(repo, 'cat-file', 'blob', oid)
        check_member(name, mode, data)
        result[name] = data
    return result

def check(repo, history=False):
    indexed = members(repo)
    if 'SOURCE_MANIFEST.json' not in indexed:
        raise ValueError('Source publication manifest is missing from the Git index')
    manifest = json.loads(indexed['SOURCE_MANIFEST.json'])
    entries = manifest['files']
    names = [e['path'] for e in entries]
    if names != sorted(set(names)) or set(indexed) != set(names) | {'SOURCE_MANIFEST.json'}:
        raise ValueError('Actual Git index differs from the source publication manifest')
    for entry in entries:
        data = indexed[entry['path']]
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Indexed bytes differ from manifest: ' + entry['path'])
    refs = git(repo, 'rev-list', '--all').decode('ascii').splitlines() if history else []
    for ref in refs:
        members(repo, ref)
    return {'ok': True, 'indexedFiles': len(indexed), 'reachableCommitsChecked': len(refs)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--history', action='store_true', help='Also inspect every commit reachable from local refs')
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.repo, args.history)))
    except (ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')
if __name__ == '__main__':
    main()
