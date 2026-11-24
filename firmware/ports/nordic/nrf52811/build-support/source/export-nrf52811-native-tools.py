"""Development-only export of firmware-free, no-Python Windows tools."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination',type=Path)
    args=parser.parse_args()
    policy=json.loads((ROOT/'build-support/source/nrf52811-native-tools.json').read_text())
    names=policy['files'];assert names==sorted(set(names))
    source=ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer'
    destination=args.destination.absolute();destination.mkdir(parents=True,exist_ok=False)
    for name in names:
        parts=PurePosixPath(name).parts
        if '..' in parts or PurePosixPath(name).is_absolute() or '\\' in name:raise ValueError('Unsafe tools entry')
        target=destination/'tools'/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source/name,target)
    for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:
        shutil.copyfile(ROOT/name,destination/name)
    bundle={'schemaVersion':1,'chip':'nrf52811','firmwareFamily':'aesl22_nrf','buildTarget':'nrf52811_aesl','brand':'aesl','artifactKind':'tools-only','fullWriter':'native-v1','baseline':'inktag-release-v1','runtimeKind':'windows-powershell-dotnet','toolsVersion':policy['toolsVersion'],'sourceToolsVersion':policy['toolsVersion'],'firmwareIncluded':False,'pythonRequired':False,'releaseApproved':False,'hardwareQualified':False}
    for original,native in [('LICENSE','LICENSE.native'),('THIRD_PARTY_NOTICES.md','THIRD_PARTY_NATIVE.md')]:
        shutil.copyfile(ROOT/original,destination/'tools'/native)
    native_files={}
    for p in sorted((destination/'tools').rglob('*')):
        if p.is_file():
            data=p.read_bytes();native_files[p.relative_to(destination/'tools').as_posix()]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    (destination/'tools/native-tools.json').write_text(json.dumps({**bundle,'files':native_files},indent=2)+'\n',encoding='utf-8',newline='\n')
    bundle['nativeManifest']='tools/native-tools.json'
    (destination/'bundle.json').write_text(json.dumps(bundle,indent=2)+'\n',encoding='utf-8',newline='\n')
    files={}
    for p in sorted(destination.rglob('*')):
        if p.is_file():
            data=p.read_bytes();files[p.relative_to(destination).as_posix()]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    (destination/'package-files.json').write_text(json.dumps({'schemaVersion':1,'files':files},indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'directory':str(destination),'toolsVersion':policy['toolsVersion'],'filesIncludingManifest':len(files)+1,'firmwareIncluded':False,'pythonRequired':False}))

if __name__=='__main__':main()
