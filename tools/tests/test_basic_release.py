from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from basic_release import inspect_basic_pair, native_inventory
from publication_scope import executable_target, firmware_binding


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def candidate():
    manifest = json.loads((ROOT / 'firmware/manifest.json').read_bytes())
    record = json.loads((ROOT / 'releases/record.json').read_bytes())
    release, = [r for r in record['releases'] if r['releaseTag'] == manifest['releaseTag']]
    return manifest, record, release


@pytest.mark.parametrize('target', [
    'da14585_aesl', 'gr5331_zkel', 'gr551x_zkel', 'gr551x_zkel_internal',
    'nrf52811_aesl', 'nrf52832_priceetag', 'om6626_zkel', 'tlsr8359_hsel',
])
def test_basic_repository_ota_and_full_match_native_bytes(target):
    manifest, _, _ = candidate()
    board, = [item for item in manifest['boards'] if item['buildTarget'] == target]
    files = {a['type']: ROOT / a['repositoryPath'] for a in board['artifacts']}
    assert inspect_basic_pair(ROOT, target, files['ota'], files['full']) == board['publication']


def test_basic_inventory_binds_all_targets_and_candidate_stays_unpublished():
    manifest, record, release = candidate()
    assert all(item['edition'] == 'basic' for item in release['firmware'])
    inventory = native_inventory(ROOT)
    bindings = firmware_binding(ROOT, record, release, manifest)
    assert len(bindings) == 8 and len(inventory) >= 600
    for entry in bindings.values():
        ota, = (artifact for artifact in entry['artifacts'] if artifact['type'] == 'ota')
        assert inventory[ota['repositoryPath']] == ota['sha256']
    generator = load('generate-updates')
    # Do not need historical attachment downloads to prove the candidate is
    # omitted. A ledger containing only the unpublished candidate emits none.
    unpublished = deepcopy(release)
    for item in (unpublished, *unpublished['firmware']):
        item.update(candidateOnly=True, releaseReady=False, published=False)
    feeds = generator.generate(dict(record, releases=[unpublished]), ROOT / '.release-assets')
    assert all(not feed['firmwareSelection']['published'] for feed in feeds.values())


def test_basic_rejects_same_mcu_from_another_board():
    manifest, _, _ = candidate()
    target = next(b for b in manifest['boards'] if b['buildTarget'] == 'gr551x_zkel_internal')
    files = {a['type']: ROOT / a['repositoryPath'] for a in target['artifacts']}
    with pytest.raises(ValueError, match='native target'):
        inspect_basic_pair(ROOT, 'gr551x_zkel', files['ota'], files['full'])


def test_basic_rejects_withdrawn_onmicro_target():
    with pytest.raises(ValueError, match='Unknown executable firmware target'):
        executable_target('om6626_hsel')


@pytest.mark.parametrize('published, approved', [(True, True), (False, True)])
def test_prepare_refuses_fixed_release_tags(tmp_path, monkeypatch, published, approved):
    prepare = load('prepare-basic-release')
    monkeypatch.setattr(prepare, 'ROOT', tmp_path)
    (tmp_path / 'version.json').write_text('{"version": "0.2.2"}', encoding='utf-8')
    (tmp_path / 'releases').mkdir()
    tag = 'v0.2.2-20261124.1'
    (tmp_path / 'releases/record.json').write_text(json.dumps({'releases': [{
        'releaseTag': tag, 'published': published, 'releaseReady': approved,
        'candidateOnly': not published,
    }]}), encoding='utf-8')
    with pytest.raises(ValueError, match='tags cannot be rebuilt'):
        prepare.prepare(tag, replace=True)
    assert not (tmp_path / '.release-assets').exists()
