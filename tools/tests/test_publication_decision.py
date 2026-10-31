"""Synthetic scope receipts only. Never copy a real publication approval."""
import importlib.util, json, sys
from pathlib import Path
import pytest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('publication_decision',ROOT/'tools/verify-publication-decision.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)

def put(path,value):path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value),encoding='utf-8')
def load(path):return json.loads(path.read_text('utf-8'))
def ref(file,**extra):return dict(bytes=file.stat().st_size,sha256=guard.digest(file),**extra)

@pytest.fixture
def candidate(tmp_path,monkeypatch):
    tag='v0.2.0+20990101.1'
    record=dict(schemaVersion=1,repository='solar2ain/ink-tag',firmwareEnabled=False,releases=[dict(releaseTag=tag,components={'cli':{'artifacts':[dict(name='client.zip',sha256='a'*64)]}},firmware=[])])
    readiness=dict(publicReleaseApproved=True,**{key:False for key in guard.DEFERRED})
    put(tmp_path/'releases/record.json',record);put(tmp_path/'releases/readiness.json',readiness)
    put(tmp_path/'firmware/manifest.json',dict(releaseTag=tag,boards=[],programmerTools=[]))
    decision=dict(ownerAuthorizedPublication=True,releaseTag=tag,deferredChecks=sorted(guard.DEFERRED),readinessSha256=guard.digest(tmp_path/'releases/readiness.json'),recordSha256=guard.digest(tmp_path/'releases/record.json'),artifacts={'client.zip':'a'*64},testOnly='synthetic fixture, not a production approval')
    put(tmp_path/'releases/publication-decision.json',decision)
    monkeypatch.setattr(guard,'public_readiness',lambda repo:sorted(guard.ALLOWED))
    return tmp_path

def add_firmware(repo,authorize):
    record=load(repo/'releases/record.json');release=record['releases'][0];tag=release['releaseTag'];target='gr5331_zkel'
    values=[]
    for kind in ('ota','full'):
        name='fixture-'+kind+'.zip';relative=f'firmware/{target}/{kind}/{name}';p=repo/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((kind+' synthetic scope bytes').encode())
        asset=repo/'.release-assets'/tag/name;asset.parent.mkdir(parents=True,exist_ok=True);asset.write_bytes(p.read_bytes())
        values.append(ref(p,type=kind,name=name,repositoryPath=relative,downloadUrl=f'https://github.com/solar2ain/ink-tag/releases/download/{tag}/{name}'))
    script=repo/f'firmware/{target}/tools/flash.ps1';script.parent.mkdir(parents=True);script.write_text('# synthetic no-op')
    index=f'releases/tool-manifests/{target}.json';put(repo/index,dict(schemaVersion=1,files=[ref(script,path=script.relative_to(repo).as_posix(),category='runtime')]))
    tool=ref(repo/index,buildTarget=target,root=f'firmware/{target}',toolsVersion='0.2.0.1',inventory=index)
    publication=dict(target=dict(boardId=target,family=target,protocolVersion=16,layout='layout-v1',compatibilityEpoch=1),version='0.2.0.0',releaseVersion=200,signaturePolicy={'packageBoard':'gr5331_zkc21v'},pairing=dict(otaSha256=values[0]['sha256'],fullSha256=values[1]['sha256'],embeddedOtaSha256=values[0]['sha256']))
    item=dict(publication,buildTarget=target,buildId='fixture',programmerInventory={k:tool[k] for k in ('toolsVersion','inventory','bytes','sha256')},artifacts=values)
    record['firmwareEnabled']=True;release['firmware']=[item]
    manifest=dict(releaseTag=tag,firmwareRepositoryIncluded=True,boards=[dict(buildTarget=target,publication=publication,artifacts=values)],programmerTools=[tool])
    put(repo/'releases/record.json',record);put(repo/'firmware/manifest.json',manifest)
    decision=load(repo/'releases/publication-decision.json');decision.update(recordSha256=guard.digest(repo/'releases/record.json'),artifacts={a['name']:a['sha256'] for a in guard.artifacts(release)},programmerToolsIncluded=True,programmerInventories={target:item['programmerInventory']})
    if authorize:decision.update(schemaVersion=2,scope='clients-and-firmware',manifestSha256=guard.digest(repo/'firmware/manifest.json'),firmwareBindings=guard.firmware_binding(repo,record,release,manifest))
    put(repo/'releases/publication-decision.json',decision)

def test_owner_decision_preserves_incomplete_verification(candidate):
    assert guard.validate(candidate)
    assert all(load(candidate/'releases/readiness.json')[key] is False for key in guard.DEFERRED)

def test_stale_decision_fails(candidate):
    p=candidate/'releases/record.json';p.write_text(p.read_text()+'\n')
    with pytest.raises(ValueError):guard.validate(candidate)

def test_owner_authorization_required(candidate):
    p=candidate/'releases/publication-decision.json';d=load(p);d['ownerAuthorizedPublication']=False;put(p,d)
    with pytest.raises(ValueError):guard.validate(candidate)

def test_new_failure_is_not_deferred(candidate,monkeypatch):
    monkeypatch.setattr(guard,'public_readiness',lambda repo:['runtime byte mismatch'])
    with pytest.raises(ValueError):guard.validate(candidate)

def test_client_only_authorization_cannot_be_reused_for_firmware(candidate):
    add_firmware(candidate,False)
    with pytest.raises(ValueError):guard.validate(candidate)

def test_new_firmware_scope_binds_exact_pair_and_tools(candidate):
    add_firmware(candidate,True);assert guard.validate(candidate)

@pytest.mark.parametrize('change',['tool','ota','full','target','policy'])
def test_firmware_scope_rejects_changed_bytes_or_context(candidate,change):
    add_firmware(candidate,True)
    if change=='tool':(candidate/'firmware/gr5331_zkel/tools/flash.ps1').write_text('changed')
    elif change in ('ota','full'):(candidate/f'firmware/gr5331_zkel/{change}/fixture-{change}.zip').write_bytes(b'changed')
    else:
        p=candidate/'firmware/manifest.json';m=load(p);m['boards'][0]['publication']['target' if change=='target' else 'signaturePolicy']={};put(p,m)
    with pytest.raises(ValueError):guard.validate(candidate)
