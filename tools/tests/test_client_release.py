"""Offline user-content and corresponding-source delivery regression tests."""
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import sys
from types import SimpleNamespace
import zipfile

import pytest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from client_content import (NOTICE_FILES, document_map, notice_bytes, render_documents,
                            rewrite_reference, validate_html_links, user_web_files)
from release_checks import material_plan, material_binding, public_readiness, verify_material_delivery, download_materials


def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/name)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value


def digest(body):return hashlib.sha256(body).hexdigest()


def test_curated_user_content_excludes_internal_records():
    files=user_web_files(ROOT)
    assert not any('BUNDLED-INVENTORY' in name or 'LEGAL_REVIEW_REQUIRED' in name or name.endswith('.md') for name in files)
    assert 'licenses/dexie-4.4.5-NOTICE.txt' in files
    assert set(name for name in files if name.startswith('licenses/') and name!='licenses/index.html')=={'licenses/'+name for name in NOTICE_FILES}


def test_html_guides_all_local_href_src_resolve():
    assert not validate_html_links(ROOT/'webui')
    for path in (ROOT/'webui/manual').rglob('*.html'):
        text=path.read_text('utf-8')
        assert 'href="../../cli/' not in text
        assert 'BUNDLED-INVENTORY.json' not in text
    for language in ('zh-CN','en'):
        assert (ROOT/'webui/manual'/language/'index.html').is_file()


@pytest.mark.parametrize('target',['missing.html','../outside.html','missing.png','%2e%2e/outside.txt'])
def test_broken_or_outside_anchor_and_image_links_are_rejected(tmp_path,target):
    site=tmp_path/'site';site.mkdir()
    (site/'index.html').write_text('<a href="'+target+'">link</a><img src="'+target+'">')
    (tmp_path/'outside.html').write_text('not public')
    assert validate_html_links(site)


def test_missing_local_manual_fragment_is_rejected(tmp_path):
    (tmp_path/'manual').mkdir()
    (tmp_path/'manual/a.html').write_text('<a href="b.html#missing">broken</a>')
    (tmp_path/'manual/b.html').write_text('<h1 id="real">Heading</h1>')
    assert validate_html_links(tmp_path)


def test_cli_document_and_license_link_rewriting():
    repo=ROOT.resolve();target=repo/'webui/manual/cli/README.html'
    assert rewrite_reference(repo,repo/'cli/README.md',target,'LICENSE')=='../../licenses/CLI-PolyForm-Noncommercial-1.0.0.txt'
    assert rewrite_reference(repo,repo/'README.md',repo/'webui/manual/README.html','webui/licenses/MaShanZheng-OFL.txt')=='../licenses/MaShanZheng-OFL.txt'
    assert rewrite_reference(repo,repo/'README.md',repo/'webui/manual/README.html','cli/README.md#usage')=='cli/README.html#usage'


def test_packages_preserve_applicable_license_bytes():
    manifest=json.loads((ROOT/'firmware/manifest.json').read_text('utf-8'))
    assets=ROOT/'.release-assets'/manifest['releaseTag']
    with zipfile.ZipFile(assets/manifest['cli']['name']) as package:
        for name in ('LICENSE','LICENSING.md','THIRD_PARTY_NOTICES.md',
                     'cli/LICENSE','cli/LICENSING.md','cli/THIRD_PARTY_NOTICES.md',
                     'cli/licenses/COMPILED-RESOURCES.txt','cli/licenses/EPDOPTIMIZE-LICENSE.txt'):
            assert package.read('InkTag-client/'+name)==(ROOT/name).read_bytes()


def fixture_material(repo,kind='release-asset'):
    (repo/'releases').mkdir(parents=True);(repo/'firmware').mkdir()
    runtime_name='cli/src/inktag_cli/_assets/nativePreviewWasm.js'
    runtime=repo/runtime_name;runtime.parent.mkdir(parents=True);runtime.write_bytes(b'compiled-runtime-fixture')
    body=b'complete-reviewed-source-fixture'
    delivery={'kind':'release-asset','name':'renderer-source.zip','url':'https://github.com/solar2ain/ink-tag/releases/download/v0.2.0/renderer-source.zip'}
    if kind=='source-download':delivery={'kind':kind,'url':'https://raw.githubusercontent.com/solar2ain/ink-tag/'+'a'*40+'/sources/renderer.zip'}
    item={'id':'renderer','bytes':len(body),'sha256':digest(body),
          'forComponents':[{'path':runtime_name,'sha256':digest(runtime.read_bytes())}], 'delivery':delivery}
    artifact={'type':'source-material','name':'renderer-source.zip','bytes':len(body),'sha256':digest(body),'downloadUrl':delivery['url']}
    config={'publicReleaseApproved':True,'cliCorrespondingSourceVerified':True,'webuiBinaryTermsApproved':True,
        'thirdPartyInventoryVerified':True,'sourceMaterials':[item],'runtimeHashes':{runtime_name:digest(runtime.read_bytes())}}
    (repo/'releases/readiness.json').write_text(json.dumps(config))
    (repo/'firmware/manifest.json').write_text(json.dumps({'releaseTag':'v0.2.0'}))
    (repo/'releases/record.json').write_text(json.dumps({'releases':[{'releaseTag':'v0.2.0','sourceMaterials':[artifact] if kind=='release-asset' else []}]}))
    return item,body,config


def test_ignored_local_material_with_hash_but_no_delivery_is_rejected(tmp_path):
    item,body,config=fixture_material(tmp_path)
    local=tmp_path/'.ignored';local.mkdir();(local/'source.zip').write_bytes(body)
    config['sourceMaterials']=[{'path':'.ignored/source.zip','sha256':digest(body)}]
    (tmp_path/'releases/readiness.json').write_text(json.dumps(config))
    assert public_readiness(tmp_path)
    assert material_plan(tmp_path)[1]


@pytest.mark.parametrize('kind',['release-asset','source-download'])
def test_delivery_metadata_and_actual_bytes_both_required(tmp_path,kind):
    item,body,config=fixture_material(tmp_path,kind)
    assert not public_readiness(tmp_path)
    output=tmp_path/'downloaded';output.mkdir()
    assert verify_material_delivery(tmp_path,output)
    name=item['delivery'].get('name','source-renderer.download')
    path=output/name;path.write_bytes(body)
    assert not verify_material_delivery(tmp_path,output)
    path.write_bytes(b'not-the-approved-bytes')
    assert verify_material_delivery(tmp_path,output)


@pytest.mark.parametrize('change',['size','runtime','url','registration','components'])
def test_source_material_binding_failures(tmp_path,change):
    item,body,config=fixture_material(tmp_path)
    if change=='size':item['bytes']=0
    if change=='runtime':item['forComponents'][0]['sha256']='0'*64
    if change=='url':item['delivery']['url']='https://example.invalid/unapproved.zip'
    if change=='components':item['forComponents']=[]
    if change=='registration':(tmp_path/'releases/record.json').write_text(json.dumps({'releases':[{'releaseTag':'v0.2.0'}]}))
    (tmp_path/'releases/readiness.json').write_text(json.dumps(config))
    assert material_plan(tmp_path)[1]


def test_required_native_source_binding_cannot_be_disabled(tmp_path):
    item,body,config=fixture_material(tmp_path)
    extra=tmp_path/'cli/src/inktag_cli/helper.py';extra.write_bytes(b'ordinary Python')
    config['runtimeHashes']['cli/src/inktag_cli/helper.py']=digest(extra.read_bytes())
    config['requiredSourceComponents']=[]
    item['forComponents']=[{'path':'cli/src/inktag_cli/helper.py','sha256':digest(extra.read_bytes())}]
    (tmp_path/'releases/readiness.json').write_text(json.dumps(config))
    assert material_plan(tmp_path)[1]


def test_download_materials_checks_received_bytes_and_no_actual_network(tmp_path,monkeypatch):
    import release_checks
    item,body,_=fixture_material(tmp_path,'source-download')
    class Response:
        def __enter__(self):return self
        def __exit__(self,*args):pass
        def geturl(self):return item['delivery']['url']
        def read(self,size):return body[:size]
    monkeypatch.setattr(release_checks,'urlopen',lambda *args,**kwargs:Response())
    output=tmp_path/'downloads'
    download_materials(tmp_path,output)
    assert not verify_material_delivery(tmp_path,output)
    monkeypatch.setattr(Response,'read',lambda self,size:b'wrong')
    with pytest.raises(ValueError):download_materials(tmp_path,output)


def test_document_export_preserves_changed_independent_license(tmp_path):
    exporter=module('export-user-docs.py')
    source=tmp_path/'docs-source';target=tmp_path/'release'
    # An explicit changed license must fail before a single destination write.
    from client_content import SOURCE_DOCUMENTS,LICENSING_DOCUMENTS
    names=(*SOURCE_DOCUMENTS,*LICENSING_DOCUMENTS,'LEGAL_REVIEW_REQUIRED.md','cli/LICENSE',
        'cli/licenses/COMPILED-RESOURCES.txt','cli/licenses/EPDOPTIMIZE-LICENSE.txt',
        'webui/THIRD_PARTY_NOTICES.txt',*('webui/licenses/'+n for n in NOTICE_FILES))
    for name in names:
        p=source/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('source')
    independent=target/'LICENSING.md';independent.parent.mkdir();independent.write_text('independent edit')
    with pytest.raises(ValueError,match='Preserve changed'):exporter.export(source,target,execute=True)
    assert independent.read_text()=='independent edit' and not (target/'README.md').exists()


def test_standalone_bundle_has_readable_notices_without_internal_inventories():
    manifest=json.loads((ROOT/'firmware/manifest.json').read_text('utf-8'))
    with zipfile.ZipFile(ROOT/'.release-assets'/manifest['releaseTag']/manifest['webStandalone']['name']) as package:
        names=package.namelist()
        assert 'InkTag-WebUI/InkTag-WebUI.html' in names
        for name in ('dexie-4.4.5-NOTICE.txt','MaShanZheng-OFL.txt'):
            assert 'InkTag-WebUI/licenses/'+name in names
        notices=[n for n in names if n.startswith('InkTag-WebUI/licenses/') and n.endswith('.txt')]
        assert notices and 'InkTag-WebUI/THIRD_PARTY_NOTICES.txt' in names
        for name in notices:
            body=package.read(name).decode('utf-8')
            assert body.strip()
            for value in ('BUNDLED-INVENTORY','runtimeHashes','newIndependentApprovalRequired','schemaVersion','Packaging review:'):
                assert value not in body
        assert not any('BUNDLED-INVENTORY' in n or 'LEGAL_REVIEW_REQUIRED' in n for n in names)


def test_final_artifacts_manuals_are_identical():
    manifest=json.loads((ROOT/'firmware/manifest.json').read_text('utf-8'))
    assets=ROOT/'.release-assets'/manifest['releaseTag']
    with zipfile.ZipFile(assets/manifest['web']['name']) as static,zipfile.ZipFile(assets/manifest['cli']['name']) as client:
        for name in user_web_files(ROOT):
            if not name.startswith('manual/'):continue
            assert static.read('webui/'+name)==client.read('InkTag-client/webui/'+name)==(ROOT/'webui'/name).read_bytes()
        assert manifest.get('localServer') is None and manifest.get('webResources') is None
        assert not any('BUNDLED-INVENTORY' in n or 'LEGAL_REVIEW_REQUIRED' in n for n in static.namelist()+client.namelist())


@pytest.mark.parametrize('name,expected,retired',[
    ('README.md','后台模式启动','以前台模式启动'),
    ('README.en.md','in the background (`webui start`)','in the foreground (`webui serve`)'),
])
def test_actual_cli_bundle_home_describes_background_installation(name,expected,retired):
    manifest=json.loads((ROOT/'firmware/manifest.json').read_text('utf8'))
    with zipfile.ZipFile(ROOT/'.release-assets'/manifest['releaseTag']/manifest['cli']['name']) as archive:
        text=archive.read('InkTag-client/'+name).decode('utf8')
    assert expected in text and '~/.inktag/' in text
    assert retired not in text


@pytest.mark.parametrize('kind,prefix',[
    ('web','webui/'),('cli','InkTag-client/webui/'),('webStandalone','InkTag-WebUI/'),
])
def test_download_bundles_do_not_embed_self_referencing_or_retired_update_feeds(kind,prefix):
    from client_content import DOWNLOAD_EXCLUDED_WEB_PATHS
    manifest=json.loads((ROOT/'firmware/manifest.json').read_text('utf8'))
    with zipfile.ZipFile(ROOT/'.release-assets'/manifest['releaseTag']/manifest[kind]['name']) as archive:
        names=set(archive.namelist())
    assert all(prefix+name not in names for name in DOWNLOAD_EXCLUDED_WEB_PATHS)


def test_repository_standalone_uses_public_manifest_endpoint_without_local_feed_copies():
    from client_content import DOWNLOAD_EXCLUDED_WEB_PATHS
    assert all(not (ROOT/'webui-standalone'/name).exists() for name in DOWNLOAD_EXCLUDED_WEB_PATHS)
