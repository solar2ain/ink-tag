"""Materialize the explicit common source profile at stable TLSR TU names.

No signing, SDK mutation, directory-based fallback or hardware access.
"""
import argparse
import hashlib
import json
from pathlib import Path

from inktag_source_profiles import source_path, profile_defines


def materialize(repo, output, profile):
    repo = Path(repo).resolve()
    output = Path(output).resolve()
    common = repo/'firmware/common'
    manifest = json.loads((repo/'firmware/source-profiles.json').read_text('utf8'))
    receipt = output/'source-profile.json'
    if receipt.exists():
        previous = json.loads(receipt.read_text('utf8'))
        previous_config = {
            'source-profile.cfg': (previous['sourceProfile']+'\n'+json.dumps(previous['defines'], sort_keys=True)+'\n').encode(),
            'source-profile.h': ('\n'.join('#define '+key+' '+str(value)
                                         for key,value in sorted(previous['defines'].items()))+'\n').encode(),
        }
        for name, expected in previous_config.items():
            target = output/name
            if target.exists() and target.read_bytes() != expected:
                raise ValueError('Preserve modified generated profile: '+str(target))
        for row in [*previous['files'], *previous.get('aliases', [])]:
            target = (output/row['target']).resolve()
            if not target.is_relative_to(output):
                raise ValueError('Unsafe previous materialization target')
            if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != row['sha256']:
                raise ValueError('Preserve modified materialized source: '+str(target))
    logical = {p.relative_to(repo).as_posix() for p in common.rglob('*')
               if p.is_file() and p.suffix in ('.c', '.h', '.inc')}
    logical.update(e['logicalPath'] for e in manifest['overrides'])
    files = []
    aliases = []
    selected_files = set()
    for name in sorted(logical):
        selected = source_path(repo, name, profile=profile)
        if selected is None:
            continue
        relative = Path(name).relative_to('firmware/common')
        target = output/'common'/relative
        if not target.resolve().is_relative_to(output):
            raise ValueError('Materialized target escapes output directory')
        data = selected.read_bytes()
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
        selected_files.add(target.resolve())
        files.append(dict(logicalPath=name, source=selected.relative_to(repo).as_posix(),
                          target=target.relative_to(output).as_posix(),
                          bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    # The historical port forwarding TU has colliding public names. Keep its
    # body at a generated private alias, with the real source in the receipt.
    if profile == 'pro':
        for name, alias in [('inktag_nfc_link.c', 'inktag_nfc_link_impl_pro.inc'),
                            ('inktag_nfc_link.h', 'inktag_nfc_link_abi_pro.h')]:
            source = source_path(repo, 'firmware/common/input/'+name, profile=profile)
            target = output/alias
            data = source.read_bytes()
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)
            selected_files.add(target.resolve())
            aliases.append(dict(logicalPath='firmware/common/input/'+name,
                source=source.relative_to(repo).as_posix(), target=target.relative_to(output).as_posix(),
                bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    # Reusing one explicit output across profiles must remove only its own
    # previously materialized, hash-verified files. Never erase user changes.
    if receipt.exists():
        previous = json.loads(receipt.read_text('utf8'))
        for row in previous['files']:
            target = (output/row['target']).resolve()
            if not target.is_relative_to(output):
                raise ValueError('Unsafe previous materialization target')
            if target not in selected_files and target.exists():
                if hashlib.sha256(target.read_bytes()).hexdigest() != row['sha256']:
                    raise ValueError('Preserve modified materialized source: '+str(target))
                target.unlink()
        if previous['sourceProfile'] == 'pro' and profile == 'base':
            for name in ('inktag_nfc_link_impl_pro.inc', 'inktag_nfc_link_abi_pro.h'):
                target = output/name
                if target.exists():
                    # These aliases are exact copies of the prior bound rows.
                    original = 'firmware/common/input/inktag_nfc_link.'+('c' if name.endswith('.inc') else 'h')
                    row = next(e for e in previous['files'] if e['logicalPath'] == original)
                    if hashlib.sha256(target.read_bytes()).hexdigest() != row['sha256']:
                        raise ValueError('Preserve modified private alias')
                    target.unlink()
    result = dict(schemaVersion=1, sourceProfile=profile,
                  defines=profile_defines(repo, profile=profile), files=files, aliases=aliases)
    data = (json.dumps(result, indent=2)+'\n').encode()
    if not receipt.exists() or receipt.read_bytes() != data:
        receipt.write_bytes(data)
    # Only profile/defines invalidate every object. Actual changed C/headers
    # retain normal per-object depfile invalidation; the full receipt still
    # binds every selected source hash for qualification.
    defines = profile_defines(repo, profile=profile)
    for name, data in (
        ('source-profile.cfg', (profile+'\n'+json.dumps(defines, sort_keys=True)+'\n').encode()),
        ('source-profile.h', ('\n'.join('#define '+key+' '+str(value)
                                      for key,value in sorted(defines.items()))+'\n').encode())):
        target = output/name
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, required=True)
    p.add_argument('--output', type=Path)
    p.add_argument('--profile', choices=('base', 'pro'), required=True)
    p.add_argument('--legacy-c-targets', action='store_true')
    p.add_argument('--make-result', action='store_true')
    a = p.parse_args()
    if a.legacy_c_targets:
        manifest = json.loads((a.repo/'firmware/source-profiles.json').read_text('utf8'))
        print(' '.join(e['logicalPath'] for e in manifest['overrides']
                       if e['logicalPath'].endswith('.c') and e[a.profile] is not None))
        raise SystemExit(0)
    if a.output is None:
        p.error('--output is required for materialization')
    result = materialize(a.repo, a.output, a.profile)
    print('INKTAG_PROFILE_OK' if a.make_result else
          json.dumps(dict(sourceProfile=result['sourceProfile'], files=len(result['files']))))
