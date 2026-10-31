"""All GitHub, git mutation and network operations are mocked."""
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
from types import SimpleNamespace

import pytest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import release_checks


def load(path):
    spec=importlib.util.spec_from_file_location(path.stem,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def setup(tmp_path,monkeypatch,failure=None):
    from test_client_release import fixture_material
    pub=load(ROOT/'tools/publish-updates.py');repo=tmp_path/'repo';repo.mkdir()
    source,body,config=fixture_material(repo)
    assets=repo/'.release-assets/v0.2.0';assets.mkdir(parents=True)
    (assets/'renderer-source.zip').write_bytes(body);(assets/'client.zip').write_bytes(b'client-fixture')
    sources=json.loads((repo/'releases/record.json').read_text())['releases'][0]['sourceMaterials']
    client={'type':'cli','name':'client.zip','bytes':len(b'client-fixture'),'sha256':sha(assets/'client.zip'),'downloadUrl':'https://github.com/solar2ain/ink-tag/releases/download/v0.2.0/client.zip'}
    release={'releaseTag':'v0.2.0','published':False,'candidateOnly':True,'releaseReady':False,
        'components':{'cli':{'artifacts':[client]}},'sourceMaterials':sources}
    (repo/'releases/record.json').write_text(json.dumps({'releases':[release]}))
    (repo/'firmware/manifest.json').write_text(json.dumps({'version':'0.2.0','releaseTag':'v0.2.0','boards':[],'sourceMaterials':sources}))
    (repo/'.github/workflows').mkdir(parents=True)
    (repo/'.github/workflows/pages.yml').write_text('path: webui\nneeds: validate')
    metadata=['SHA256SUMS.txt','InkTag-v0.2.0-manifest.json','RELEASE_NOTES.md']
    for name in metadata:(assets/name).write_bytes(b'metadata-fixture')
    receipt={'schemaVersion':3,'purpose':'public-client-release','allowPublish':True,'releaseTag':'v0.2.0',
        'distributionHead':'a'*40,'recordSha256':sha(repo/'releases/record.json'),
        'artifacts':{item['name']:item['sha256'] for item in [client,*sources]},
        'metadata':{name:sha(assets/name) for name in metadata},
        'sourceMaterials':release_checks.material_binding([source])}
    approval=repo/'approval.json';approval.write_text(json.dumps(receipt))
    calls=[];state={'head':'a'*40}
    generator=SimpleNamespace(validate_record=lambda r:None,write_feeds=lambda *args:None)
    verifier=SimpleNamespace(validate_public_tree=lambda *a:[],validate_manifest=lambda *a:({},[]),validate_assets=lambda *a:[])
    monkeypatch.setattr(pub,'module',lambda path:release_checks if path.name=='release_checks.py' else generator if path.name=='generate-updates.py' else verifier)
    def git(r,*args):
        calls.append(('git',*args))
        if args==('remote','get-url','origin'):return 'https://github.com/solar2ain/ink-tag.git'
        if args==('branch','--show-current'):return 'main'
        if args==('status','--porcelain'):return ''
        if args==('rev-parse','HEAD'):return state['head']
        if args[0]=='commit':state['head']='b'*40
        if args[0]=='ls-remote' and failure=='tag-conflict':return 'c'*40+'\trefs/tags/v0.2.0'
        if failure=='feed-push' and args==('push','origin','HEAD:main') and state['head']=='b'*40:raise subprocess.CalledProcessError(1,args)
        return ''
    monkeypatch.setattr(pub,'git',git)
    monkeypatch.setattr(pub.uuid,'uuid4',lambda:SimpleNamespace(hex='request-fixture'))
    monkeypatch.setattr(pub.time,'sleep',lambda _:None)
    def run(command,**kwargs):
        calls.append(tuple(command))
        stage=tuple(command[1:3])
        if (failure=='create' and stage==('release','create') or failure=='edit' and stage==('release','edit') or failure=='dispatch' and stage==('workflow','run') or failure=='watch' and stage==('run','watch')):
            raise subprocess.CalledProcessError(1,command)
        if stage==('release','download'):
            directory=Path(command[command.index('--dir')+1])
            for path in assets.iterdir():shutil.copy2(path,directory/path.name)
            if failure=='source-hash':(directory/'renderer-source.zip').write_bytes(b'wrong')
            if failure=='source-missing':(directory/'renderer-source.zip').unlink()
            if failure=='firmware-hash':(directory/'fixture-ota.zip').write_bytes(b'changed after upload')
        text=''
        if stage==('run','list'):text=json.dumps([{'databaseId':123,'headSha':'b'*40,'displayTitle':'InkTag Pages request-fixture'}])
        if stage==('run','view'):text=json.dumps({'headSha':('c' if failure=='wrong-pages-sha' else 'b')*40,'event':'workflow_dispatch','status':'completed','conclusion':'success','jobs':[{'name':'deploy','conclusion':'success'}]})
        return SimpleNamespace(stdout=text,returncode=0)
    monkeypatch.setattr(pub.subprocess,'run',run)
    # Reject accidental real network calls even when no source URL is expected.
    monkeypatch.setattr(release_checks,'urlopen',lambda *a,**k:(_ for _ in ()).throw(AssertionError('No actual network in tests')))
    return pub,repo,approval,calls


def test_complete_sequence_and_source_attachment_download(tmp_path,monkeypatch):
    pub,repo,approval,calls=setup(tmp_path,monkeypatch)
    result=pub.promote(repo,'v0.2.0',approval,True,True)
    assert result['complete'] and result['pagesDeployed'] and result['pagesCommit']=='b'*40
    create=next(c for c in calls if c[:3]==('gh','release','create'))
    assert any(str(c).endswith('renderer-source.zip') for c in create)
    positions=[next(i for i,c in enumerate(calls) if c[:3]==key) for key in [('gh','release','create'),('gh','release','download'),('gh','release','edit'),('gh','workflow','run')]]
    assert positions==sorted(positions)
    push=max(i for i,c in enumerate(calls) if c==('git','push','origin','HEAD:main'))
    assert positions[2]<push<positions[3]
    assert not any('staging' in str(c) or 'refs/heads/codex/' in str(c) for c in calls)
    assert '--verify-tag' in create


@pytest.mark.parametrize('failure',['tag-conflict','create','source-hash','source-missing','edit','feed-push','dispatch','watch','wrong-pages-sha'])
def test_failure_paths_never_report_complete(tmp_path,monkeypatch,failure):
    pub,repo,approval,calls=setup(tmp_path,monkeypatch,failure)
    with pytest.raises((ValueError,subprocess.SubprocessError)):pub.promote(repo,'v0.2.0',approval,True,True)
    if failure in {'source-hash','source-missing'}:assert not any(c[:3]==('gh','release','edit') for c in calls)
    if failure in {'tag-conflict','create','source-hash','source-missing','edit','feed-push'}:assert not any(c[:3]==('gh','workflow','run') for c in calls)
    if failure=='tag-conflict':assert not any(c[:3]==('git','push','origin') or c[:3]==('gh','release','create') for c in calls)
    status=repo/'.release-local/publication-v0.2.0.json'
    if status.exists():assert not json.loads(status.read_text())['complete']


@pytest.mark.parametrize('schema',[1,2])
def test_old_receipts_rejected_before_external_writes(tmp_path,monkeypatch,schema):
    pub,repo,approval,calls=setup(tmp_path,monkeypatch)
    value=json.loads(approval.read_text());value['schemaVersion']=schema;approval.write_text(json.dumps(value))
    with pytest.raises(ValueError):pub.promote(repo,'v0.2.0',approval,True,True)
    assert not any(c[0]=='gh' for c in calls)


def test_material_delivery_not_bound_by_receipt_is_rejected(tmp_path,monkeypatch):
    pub,repo,approval,calls=setup(tmp_path,monkeypatch)
    value=json.loads(approval.read_text());value.pop('sourceMaterials');approval.write_text(json.dumps(value))
    with pytest.raises(ValueError):pub.promote(repo,'v0.2.0',approval,True,True)
    assert not any(c[0]=='gh' for c in calls)


def test_manual_deploy_boundary_and_dry_run(tmp_path,monkeypatch):
    pub,repo,approval,calls=setup(tmp_path,monkeypatch)
    assert pub.promote(repo,'v0.2.0',None,False,True)['dryRun'] and not calls
    result=pub.promote(repo,'v0.2.0',approval,True,False)
    assert result['releasePublished'] and not result['pagesDeployed'] and not result['complete']
    assert not any(c[:3]==('gh','workflow','run') for c in calls)


def test_preview_release_is_created_as_github_prerelease(tmp_path,monkeypatch):
    pub,repo,approval,calls=setup(tmp_path,monkeypatch)
    path=repo/'releases/record.json';record=json.loads(path.read_text())
    record['releases'][0].update(channel='preview',prerelease=True)
    path.write_text(json.dumps(record))
    receipt=json.loads(approval.read_text());receipt['recordSha256']=sha(path);approval.write_text(json.dumps(receipt))
    pub.promote(repo,'v0.2.0',approval,True,False)
    create=next(c for c in calls if c[:3]==('gh','release','create'))
    assert '--prerelease' in create
    commit=next(c for c in calls if c[:3]==('git','add','--'))
    assert 'webui/updates/preview.json' in commit


def firmware_setup(tmp_path,monkeypatch,failure=None):
    from test_publication_decision import add_firmware,put
    pub,repo,approval,calls=setup(tmp_path,monkeypatch,failure)
    put(repo/'releases/publication-decision.json',{})
    add_firmware(repo,True)
    record=json.loads((repo/'releases/record.json').read_text());release=record['releases'][0]
    manifest=json.loads((repo/'firmware/manifest.json').read_text());manifest.update(version='0.2.0',sourceMaterials=release['sourceMaterials'])
    put(repo/'firmware/manifest.json',manifest)
    receipt=json.loads(approval.read_text());receipt.update(schemaVersion=4,purpose='public-firmware-release',recordSha256=sha(repo/'releases/record.json'),manifestSha256=sha(repo/'firmware/manifest.json'),firmwareBindings=pub.firmware_binding(repo,record,release,manifest),artifacts={a['name']:a['sha256'] for a in pub.artifacts(release)})
    put(approval,receipt)
    # Native authentication has a separate real signed-fixture matrix. Here
    # exercise transaction ordering/byte download gates with synthetic content.
    monkeypatch.setattr(pub,'verify_firmware_bytes',lambda repo,release,assets:calls.append(('native-authentication',str(assets))))
    return pub,repo,approval,calls


def test_explicit_firmware_scope_uploads_downloads_and_publishes_every_pair(tmp_path,monkeypatch):
    pub,repo,approval,calls=firmware_setup(tmp_path,monkeypatch)
    result=pub.promote(repo,'v0.2.0',approval,True,False)
    create=next(c for c in calls if c[:3]==('gh','release','create'))
    assert all(any(str(a).endswith(name) for a in create) for name in ('fixture-ota.zip','fixture-full.zip'))
    assert sum(c[0]=='native-authentication' for c in calls)==2
    entry=json.loads((repo/'releases/record.json').read_text())['releases'][0]['firmware'][0]
    assert entry['published'] and entry['releaseReady'] and not entry['candidateOnly']
    assert result['releasePublished'] and not result['pagesDeployed']


@pytest.mark.parametrize('failure',['firmware-hash','tool-change','old-client-approval','layout-change'])
def test_firmware_transaction_failure_cannot_publish_or_dispatch_pages(tmp_path,monkeypatch,failure):
    pub,repo,approval,calls=firmware_setup(tmp_path,monkeypatch,failure)
    if failure=='tool-change':(repo/'firmware/gr5331_zkel/tools/flash.ps1').write_text('changed')
    elif failure=='old-client-approval':
        value=json.loads(approval.read_text());value.update(schemaVersion=3,purpose='public-client-release');approval.write_text(json.dumps(value))
    elif failure=='layout-change':
        value=json.loads((repo/'releases/record.json').read_text());value['releases'][0]['firmware'][0]['target']['layout']='other';(repo/'releases/record.json').write_text(json.dumps(value))
        receipt=json.loads(approval.read_text());receipt['recordSha256']=sha(repo/'releases/record.json');approval.write_text(json.dumps(receipt))
    with pytest.raises(ValueError):pub.promote(repo,'v0.2.0',approval,True,True)
    assert not any(c[:3] in [('gh','release','edit'),('gh','workflow','run')] for c in calls)


@pytest.mark.parametrize('wrong_transition',[False,True])
def test_deferred_decision_binds_exact_post_download_transition(tmp_path,monkeypatch,wrong_transition):
    from copy import deepcopy
    pub,repo,approval,calls=firmware_setup(tmp_path,monkeypatch)
    record=json.loads((repo/'releases/record.json').read_text());projected=deepcopy(record)
    release=projected['releases'][0];release.update(candidateOnly=False,releaseReady=True,published=True)
    for item in release['firmware']:item.update(candidateOnly=False,releaseReady=True,published=True)
    bound=hashlib.sha256((json.dumps(projected,ensure_ascii=False,indent=2)+'\n').encode()).hexdigest()
    decision={'permittedPublishedRecordSha256':('0'*64 if wrong_transition else bound)}
    path=repo/'releases/publication-decision.json';path.write_text(json.dumps(decision))
    receipt=json.loads(approval.read_text());receipt['decisionSha256']=sha(path);approval.write_text(json.dumps(receipt))
    previous=pub.module
    guard=SimpleNamespace(validate=lambda r:None,ALLOWED={'cliCorrespondingSourceVerified is not independently verified'})
    monkeypatch.setattr(pub,'module',lambda path:guard if path.name=='verify-publication-decision.py' else previous(path))
    monkeypatch.setattr(release_checks,'public_readiness',lambda r:list(guard.ALLOWED))
    if wrong_transition:
        with pytest.raises(ValueError,match='post-download record transition'):pub.promote(repo,'v0.2.0',approval,True,True)
        assert not any(c[0]=='gh' for c in calls)
    else:
        pub.promote(repo,'v0.2.0',approval,True,False)
        assert sha(repo/'releases/record.json')==bound
        assert json.loads(path.read_text())['recordSha256']==bound
        assert 'releases/publication-decision.json' in next(c for c in calls if c[:3]==('git','add','--'))
