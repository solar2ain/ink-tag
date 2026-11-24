# DA workspace projection v1
"""DA-only persistent SDK and isolated incremental workspace. No device/signing IO."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import uuid

VERSION_PATH = Path('dialog/DA145xx_SDK/6.0.24.1464')
MARKER = Path('sdk/platform/core_modules/rwip/api/rwip.h')


def plain(path):
    for part in (path, *path.parents):
        try: info = part.lstat()
        except FileNotFoundError: continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('SDK path contains a link/reparse point: ' + str(part))


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def persistent_root(path):
    if not path.is_absolute(): raise ValueError('SDK root must be an explicit absolute persistent path')
    plain(path)
    value = path.resolve()
    if 'worktrees' in (part.lower() for part in value.parts):
        raise ValueError('The persistent SDK entity cannot belong to an archivable worktree')
    # A Git-owned SDK must belong to the same target family, without
    # embedding any private repository names in this public projection.
    for owner in value.parents:
        if (owner / '.git').exists():
            if not (owner / 'app').is_dir():
                # Shared source can keep a verified DA dependency privately.
                if not value.is_relative_to(owner / '.local'):
                    raise ValueError('A shared checkout must keep the DA SDK in .local')
                relative = value.relative_to(owner).as_posix()
                ignored = subprocess.run(['git', '-C', str(owner), 'check-ignore', '-q', '--', relative])
                if ignored.returncode != 0:
                    raise ValueError('A shared checkout must ignore the DA SDK')
            break
    return value


def inventory(root):
    plain(root)
    if not root.is_dir(): raise ValueError('SDK directory missing: ' + str(root))
    files = {}
    pending = [root]
    while pending:
        directory = pending.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                info = entry.stat(follow_symlinks=False)
                if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                    raise ValueError('SDK member contains a link/reparse point: ' + entry.path)
                path = Path(entry.path)
                if stat.S_ISDIR(info.st_mode): pending.append(path)
                elif stat.S_ISREG(info.st_mode):
                    files[path.relative_to(root).as_posix()] = {'bytes': info.st_size, 'sha256': sha(path)}
                else: raise ValueError('Nonregular SDK member: ' + entry.path)
    return dict(sorted(files.items()))


def write_new(path, value):
    plain(path); path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf8') as stream: json.dump(value, stream, indent=2)


def preserve_base(source, sdk_root):
    source, sdk_root = source.absolute(), persistent_root(sdk_root)
    destination = sdk_root / VERSION_PATH
    manifest = sdk_root / 'da6-base.json'
    plain(source); plain(destination); plain(manifest)
    if destination.exists() or manifest.exists(): raise ValueError('Persistent SDK destination exists; no overwrite or merge')
    if not (source / MARKER).is_file(): raise ValueError('Source lacks the exact SDK6 marker')
    before = inventory(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination, copy_function=shutil.copy2)
    if inventory(source) != before or inventory(destination) != before:
        raise ValueError('SDK copy/source changed; preserve partial copy and do not select it')
    write_new(manifest, {'format': 1, 'sdk': 'DA145xx_SDK6.0.24.1464', 'origin': str(source),
              'scope': 'Exact preserved local SDK snapshot, including existing generated sources/objects; not pristine-vendor or new build qualification',
              'files': before})
    return {'sdk': str(destination), 'manifest': str(manifest), 'manifestSha256': sha(manifest),
            'files': len(before), 'bytes': sum(item['bytes'] for item in before.values()), 'sourceRetained': True}


def verified_base(sdk_root):
    sdk_root = persistent_root(sdk_root); manifest = sdk_root / 'da6-base.json'
    plain(manifest)
    data = json.loads(manifest.read_bytes())
    base = sdk_root / VERSION_PATH
    if data.get('format') != 1 or data.get('sdk') != 'DA145xx_SDK6.0.24.1464' or not (base / MARKER).is_file() or inventory(base) != data['files']:
        raise ValueError('Persistent SDK snapshot/hash differs; no automatic repair or rebuild')
    return base, sha(manifest)


def working_path(repo, build_profile=None):
    if build_profile not in (None, 'crc-only'):
        raise ValueError('Unknown DA build profile')
    suffix = '-' + build_profile if build_profile else ''
    return repo.absolute() / ('.local/da-sdk6' + suffix)


def prepare(repo, sdk_root, build_profile=None):
    base, digest = verified_base(sdk_root)
    work = working_path(repo, build_profile); plain(work)
    receipt = work / '.inktag-workspace.json'
    expected = {'format': 1, 'sdkManifestSha256': digest, 'workspace': str(work)}
    if build_profile: expected['buildProfile'] = build_profile
    if work.exists():
        plain(receipt)
        data = json.loads(receipt.read_bytes())
        if data != expected:
            raise ValueError('Working SDK origin differs; no overwrite of an existing cache')
    else:
        work.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(base, work, copy_function=shutil.copy2)
        if inventory(work) != inventory(base): raise ValueError('Workspace initial copy differs; preserve partial output')
        write_new(receipt, expected)
    return work, digest


def isolated_render(repo, workspace):
    if workspace is None: raise ValueError('Use the build wrapper; an explicit isolated SDK render path is required')
    root = (repo / '.local/da-sdk-render').absolute()
    path = workspace.absolute(); plain(path)
    if path.parent != root or not path.name.startswith('r-') or not path.is_dir():
        raise ValueError('Materialization may only modify its own isolated render directory')
    return path


def materialize(repo, sdk_root, forwarded, build_profile=None):
    if build_profile and (build_profile != 'crc-only'):
        raise ValueError('Only the CRC-only source build profile is supported')
    repo = repo.absolute(); work, digest = prepare(repo, sdk_root, build_profile)
    base = sdk_root.absolute() / VERSION_PATH
    render_root = repo / '.local/da-sdk-render'; plain(render_root)
    render_root.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='r-', dir=render_root))
    # Rendering reads SDK sources/templates only. Do not copy manuals, host
    # binaries, unrelated examples or object caches into a disposable render.
    # The live workspace retains the complete, exact initial snapshot.
    render_inputs = ('sdk', 'third_party', 'config',
                     'projects/target_apps/ble_examples/ble_app_peripheral',
                     'projects/target_apps/ble_examples/ble_app_ota',
                     'utilities/secondary_bootloader')
    for relative in render_inputs:
        source = base / relative
        if not source.is_dir(): raise ValueError('Required SDK template/source missing: ' + relative)
        ignore = (lambda directory, names: [name for name in names if name.lower().startswith('out_')]) if relative.startswith(('projects/', 'utilities/')) else None
        shutil.copytree(source, stage / relative, copy_function=shutil.copy2, ignore=ignore)
    script = 'materialize-dialog-port.py'
    command = [sys.executable, '-B', str(repo / 'build-support/sdk' / script), '--repo', str(repo), '--sdk-workspace', str(stage)]
    command += forwarded
    result = subprocess.run(command, cwd=repo, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = repo / '.local/sdk-path-audit' / (uuid.uuid4().hex + '.log')
    log.parent.mkdir(parents=True, exist_ok=True); log.write_bytes(result.stdout)
    if result.returncode: raise ValueError('Isolated materialization failed; stage/log retained: ' + str(log))
    changed = []
    # Only changed inputs enter the live branch workspace. Never replace object
    # caches or delete files absent from a fresh generation; project XML owns
    # the actual source list. Identical bytes retain their compilation mtimes.
    stage_files = inventory(stage)
    checked = set()
    for relative, item in stage_files.items():
        source = stage / relative
        if any(part.lower().startswith('out_') for part in Path(relative).parts): continue
        target = work / relative
        # Check each owned target ancestor once rather than repeatedly statting
        # every Windows ancestor for every SDK member.
        current = target
        while current not in checked:
            try: info = current.lstat()
            except FileNotFoundError: info = None
            if info and (stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400):
                raise ValueError('Linked workspace target refused: ' + str(current))
            checked.add(current)
            if current.parent == current: break
            current = current.parent
        if target.is_file() and item['sha256'] == sha(target): continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target); shutil.copymode(source, target)
        changed.append(relative)
    verified_base(sdk_root)
    # Only this explicitly created temp render is removed; all ancestors and
    # members were checked for links. Never remove the base/live/old SDK tree.
    isolated_render(repo, stage); inventory(stage)
    shutil.rmtree(stage)
    return {'sdk': str(work), 'persistentSdk': str(base), 'sdkManifestSha256': digest,
            'changedInputs': changed, 'materializeLog': str(log), 'nativeExitCode': result.returncode,
            'objectCachesPreserved': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['location', 'preserve-base', 'verify', 'prepare', 'materialize'])
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--build-profile', choices=['crc-only'])
    args, forwarded = parser.parse_known_args()
    if forwarded[:1] == ['--']: forwarded = forwarded[1:]
    if forwarded not in ([], ['--board', 'da14585_aesl']):
        parser.error('Only the DA board may be forwarded')
    if args.action != 'materialize' and forwarded: parser.error('Unknown arguments')
    if args.action == 'location':
        output = {'sdkRoot': str(persistent_root(args.sdk_root))}
    elif args.action == 'preserve-base':
        if args.source is None: parser.error('--source required')
        output = preserve_base(args.source, args.sdk_root)
    elif args.action == 'verify':
        base, digest = verified_base(args.sdk_root)
        output = {'sdk': str(base), 'manifestSha256': digest, 'verified': True}
    elif args.action == 'prepare':
        work, digest = prepare(args.repo, args.sdk_root, args.build_profile)
        output = {'sdk': str(work), 'sdkManifestSha256': digest}
    else: output = materialize(args.repo, args.sdk_root, forwarded, build_profile=args.build_profile)
    print(json.dumps(output))


if __name__ == '__main__': main()
