"""Offline validation of the actual unsigned source export (Linux/WSL)."""
import argparse, hashlib, json, os, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT.parents[3]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root',type=Path,required=True)
    parser.add_argument('--toolchain-path',type=Path,required=True)
    parser.add_argument('--jobs',type=int,default=4)
    args=parser.parse_args()
    manifest=json.loads((BASE/'source-export.json').read_text())
    for name,row in manifest['files'].items():
        data=(BASE/name).read_bytes()
        assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256'],name
    out=BASE/'.validation';out.mkdir(exist_ok=False);steps=[]
    def run(name,argv):
        log=out/(str(len(steps)).zfill(3)+'.log');start=time.monotonic()
        with log.open('w') as stream:
            result=subprocess.run(list(map(str,argv)),cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,env={**os.environ,'INKTAG_NRF_SDK_ROOT':str(args.sdk_root.absolute())})
        steps.append({'name':name,'argv':list(map(str,argv)),'exit':result.returncode,
                      'seconds':round(time.monotonic()-start,3),'log':log.name,
                      'sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
        (out/'report.json').write_text(json.dumps({'steps':steps},indent=2)+'\n')
        print(('PASS ' if not result.returncode else 'FAIL ')+name,flush=True)
        if result.returncode:raise SystemExit(result.returncode)
    run('unsigned App and Recovery',['make','-C',str(BASE),
        'BUILD_TARGET=nrf52811_aesl','JOBS='+str(args.jobs),
        'SDK_ROOT='+str(args.sdk_root.absolute()),'TOOLCHAIN_ROOT='+str(args.toolchain_path.absolute().parent),
        'OUTPUT='+str(out/'native')])
    matrix=json.loads((ROOT/'tests/source-test-manifest.json').read_text())
    for source in matrix['python']:run(source,[sys.executable,source])
    for entry in matrix['hostC']:
        binary=out/entry['id']
        run('compile '+entry['id'],['cc','-std=c99','-O2','-Ifirmware/common/gui','-Ifirmware/common/ble',
              *entry['defines'],*entry['sources'],'-o',str(binary)])
        run(entry['id'],[str(binary)])
    # Freeze every real compiler dependency, including SDK headers (-MD).
    groups={'source':{},'generated':{},'sdk':{},'compiler':{}}
    tc=args.toolchain_path.absolute().parent; sdk=args.sdk_root.absolute()
    for dep in (out/'native').rglob('*.d'):
        line=dep.read_text().replace('\\\n',' ')
        for word in line.split(':',1)[1].split():
            if word.endswith(':') or word=='\\':continue
            path=Path(os.path.abspath(ROOT/word))
            if not path.is_file():continue
            if path.is_relative_to(BASE):
                key=path.relative_to(BASE).as_posix()
                group='generated' if path.is_relative_to(out) else 'source'
                if group=='source' and key not in manifest['files']:raise ValueError('Unexported dependency '+key)
            elif path.is_relative_to(sdk):group='sdk';key=path.relative_to(sdk).as_posix()
            elif path.is_relative_to(tc):group='compiler';key=path.relative_to(tc).as_posix()
            else:raise ValueError('Unexpected compiler dependency '+str(path))
            data=path.read_bytes();groups[group][key]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    artifacts={}
    for path in (out/'native/firmware').rglob('*'):
        if path.is_file():artifacts[path.relative_to(BASE).as_posix()]={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    (out/'closure.json').write_text(json.dumps({'inputs':groups,'artifacts':artifacts,'publicationApproved':False},indent=2)+'\n')
    for name,row in manifest['files'].items():assert hashlib.sha256((BASE/name).read_bytes()).hexdigest()==row['sha256'],name
    print('PASS immutable exported inputs and compiler closure',flush=True)
if __name__=='__main__':main()
