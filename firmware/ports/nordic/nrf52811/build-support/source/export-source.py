"""Copy an explicit InkTag source allowlist into a fresh local directory."""
import argparse, hashlib, json, shutil, re
from pathlib import Path, PurePosixPath
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT.parents[3]
PROGRAMMER = 'tools/platforms/nordic/nrf52811/programmer/'
FORBIDDEN = {'.git','.local','.sdk','.tools','.secrets','AGENTS.md','Makefile.private','factory_backups',
             }

def selected(root):
    policy = json.loads((root/'build-support/source/source-allowlist.json').read_text())
    names = policy['files']
    if names != sorted(set(names)):
        raise ValueError('Allowlist must be sorted and unique')
    for name in names:
        parts = PurePosixPath(name).parts
        if not parts or parts[0].endswith(':') or PurePosixPath(name).is_absolute() or '..' in parts or set(parts)&FORBIDDEN or any(p.endswith('-projection.json') for p in parts) or '\\' in name:
            raise ValueError('Unsafe export path: '+name)
        owner = BASE if name.startswith(PROGRAMMER) else root
        path = owner/name
        for ancestor in [path, *path.parents]:
            if ancestor == owner: break
            if ancestor.is_symlink() or (hasattr(ancestor,'is_junction') and ancestor.is_junction()):
                raise ValueError('Linked export input: '+name)
        if not path.is_file(): raise ValueError('Missing input: '+name)
        private = re.compile(rb'ink[-_]tag[-_](?:main|nrf52811|da14585)|[A-Za-z]:[/\\]Projects[/\\]', re.I)
        if private.search(path.read_bytes()):raise ValueError('Private provenance in export input: '+name)
    return names

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination',type=Path)
    args=parser.parse_args(); destination=args.destination.absolute()
    names=selected(ROOT)
    destination.mkdir(parents=True,exist_ok=False)
    records={}
    for name in names:
        if name.startswith(PROGRAMMER):
            target, source = name, BASE/name
        else:
            target, source = 'firmware/ports/nordic/nrf52811/'+name, ROOT/name
        data=source.read_bytes(); out=destination/target
        out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
        records[target]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    shared=json.loads((ROOT/'shared-common.json').read_text())['files']
    extras={row['canonical']:BASE/row['canonical'] for row in shared}
    extras.update({name:BASE/name for name in (
        'Makefile','makefile.basic','firmware/boards/nrf52811_aesl.h',
        'docs/build-firmware.md','docs/build-firmware.en.md')})
    extras['LICENSE']=ROOT/'LICENSE'
    for name,path in sorted(extras.items()):
        if not path.resolve().is_relative_to(BASE) or '..' in PurePosixPath(name).parts:
            raise ValueError('Shared source outside the Base tree: '+name)
        data=path.read_bytes();out=destination/name
        out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
        records[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    manifest={'schemaVersion':1,'buildTarget':'nrf52811_aesl','unsigned':True,
              'publicationApproved':False,'rightsStatus':'third-party terms retained; see firmware/ports/nordic/nrf52811/NOTICE.md and firmware/ports/nordic/nrf52811/THIRD_PARTY_NOTICES.md',
              'files':records}
    (destination/'source-export.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'files':len(records),'destination':str(destination),'publicationApproved':False}))
if __name__=='__main__':main()
