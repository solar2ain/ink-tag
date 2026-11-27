from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys
from zipfile import ZipFile

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from basic_release import (INVENTORY, digest, inspect_basic_pair, native_inventory,
                           nordic_version_from_inventory, verify_firmware_bundle)
from publication_scope import artifacts, executable_target, firmware_binding


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
    assert set(bindings) == {board['buildTarget'] for board in manifest['boards']}
    for board in manifest['boards']:
        review = {artifact['type']: artifact for artifact in bindings[board['buildTarget']]['artifacts']}
        assert set(review) == {'ota', 'full'}
        for artifact in board['artifacts']:
            assert inventory[artifact['repositoryPath']] == review[artifact['type']]['sha256']
    generator = load('generate-updates')
    # Do not need historical attachment downloads to prove the candidate is
    # omitted. A ledger containing only the unpublished candidate emits none.
    unpublished = deepcopy(release)
    for item in (unpublished, *unpublished['firmware']):
        item.update(candidateOnly=True, releaseReady=False, published=False)
    feeds = generator.generate(dict(record, releases=[unpublished]), ROOT / '.release-assets')
    assert all(not feed['firmwareSelection']['published'] for feed in feeds.values())


@pytest.mark.parametrize('target, manifest, package_root', [
    *[(target, 'inktag-goodix-tools.json', '.') for target in
      ('gr5331_zkel', 'gr551x_zkel', 'gr551x_zkel_internal')],
    ('da14585_aesl', 'tool-manifest.json', '.'),
    ('nrf52811_aesl', 'package-files.json', '.'),
    ('nrf52811_aesl', 'tools/native-tools.json', 'tools'),
    ('nrf52832_priceetag', 'tools/internal/package-files.json', '.'),
    ('om6626_zkel', 'tools/internal/records/tools-files.json', 'tools/internal'),
    ('tlsr8359_hsel', 'tools/internal/tools-manifest.json', 'tools/internal'),
])
def test_nested_firmware_tool_inventory_matches_published_files(target, manifest, package_root):
    target_root = ROOT / 'release/firmware' / target
    ledger = json.loads((target_root / manifest).read_bytes())
    if isinstance(ledger, dict):
        ledger = ledger['files']
    if isinstance(ledger, dict):
        ledger = [dict(path=path, **record) for path, record in ledger.items()]
    assert ledger
    for record in ledger:
        path = target_root / package_root / record['path']
        contents = path.read_bytes()
        assert len(contents) == record['bytes'], path
        assert digest(contents) == record['sha256'], path


def test_firmware_bundle_covers_the_complete_release_tree_and_is_in_approval_scope(tmp_path):
    manifest, record, release = candidate()
    bundle = manifest['firmwareBundle']
    assert bundle in artifacts(release)
    inventory = native_inventory(ROOT)
    members = (*inventory, INVENTORY)
    assert 'release/firmware/LICENSE' in inventory
    for board in manifest['boards']:
        prefix = f"release/firmware/{board['buildTarget']}/"
        assert any(name.startswith(f'{prefix}full/') for name in inventory)
        assert f'{prefix}tools/flash.bat' in inventory
        assert f'{prefix}tools/flash-zh.bat' in inventory
        assert sum(name.startswith(f'{prefix}tools/configs/') and name.endswith('.toml') for name in inventory) == len(board['boardProfiles'])
        images = [name for name in inventory if name.startswith(f'{prefix}flash-images/')]
        if board['buildTarget'] in {'da14585_aesl', 'nrf52811_aesl', 'om6626_zkel'}:
            assert not images
        else:
            assert len({Path(name).suffix for name in images} & {'.bin', '.hex'}) == 1
        assert not any(name.startswith(f'{prefix}programmer/') for name in inventory)
        assert not any(len(Path(name.removeprefix(prefix)).parts) == 2 and name.endswith('.ps1')
                       for name in inventory if name.startswith(f'{prefix}tools/'))
        assert not any(name.endswith('.zip') for name in images)
        for image in (name for name in images if name.endswith(('.hex', '.bin'))):
            assert Path(image).stem == (
                f"InkTag-BASIC-{manifest['version']}-{board['buildTarget']}-{Path(image).parent.name}-flash"
            )
        if board['buildTarget'] == 'om6626_zkel':
            assert not any(name.startswith(f'{prefix}tools/internal/payload/') for name in inventory)
            assert f'{prefix}tools/internal/device-pack/Devices/Onmicro/OM662XX_nombr.elf' in inventory
            assert f'{prefix}tools/internal/records/tools-files.json' in inventory
        if board['buildTarget'] == 'tlsr8359_hsel':
            assert not any(name.startswith(f'{prefix}tools/internal/runtime/') for name in inventory)
            assert f'{prefix}tools/internal/serial/flash.ps1' in inventory
            assert f'{prefix}tools/internal/python/check.py' in inventory
            assert f'{prefix}tools/licenses/InkTag-CLI-LICENSE.txt' not in inventory
            assert f'{prefix}tools/internal/vendor/LICENSE' in inventory
        if board['buildTarget'].startswith('gr'):
            assert not any(name.startswith(prefix) and name.endswith('.whl') for name in inventory)
            assert not any(name.startswith(f'{prefix}tools/internal/runtime/firmware/') for name in inventory)
        if board['buildTarget'] == 'nrf52811_aesl':
            assert not any(name.startswith(f'{prefix}tools/native-configs/') for name in inventory)
        if board['buildTarget'] == 'da14585_aesl':
            assert not any(name.startswith((f'{prefix}tools/programmer/', f'{prefix}tools/hardware/'))
                           for name in inventory)
            for name in ('flash-launch.bat', 'da14585-clean-bin.py', 'vendor-dependencies.json'):
                assert f'{prefix}tools/internal/{name}' in inventory
    path = tmp_path / bundle['name']
    for corrupt in (False, True):
        with ZipFile(path, 'w') as archive:
            for name in members:
                contents = (ROOT / name).read_bytes()
                if corrupt and name.endswith('-flash.bin'):
                    contents = b'incorrect image'
                archive.writestr(f"{path.stem}/{name.removeprefix('release/firmware/')}", contents)
        if corrupt:
            with pytest.raises(ValueError, match='Firmware bundle member changed'):
                verify_firmware_bundle(ROOT, path)
        else:
            verify_firmware_bundle(ROOT, path)
    with pytest.raises(ValueError, match='differs between manifest and record'):
        firmware_binding(ROOT, record, release, {k: v for k, v in manifest.items() if k != 'firmwareBundle'})


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


def test_four_part_hotfix_sorts_after_the_original_release():
    updates = load('generate-updates')
    assert updates.compare('0.2.2.1', '0.2.2') == 1
    assert updates.compare('0.2.2', '0.2.2.0') == 0
    assert updates.compare('0.2.2.1-preview.1', '0.2.2.1') == -1
    assert updates.TAG.fullmatch('v0.2.2.1-20261126.1')


def test_nordic_revision_requires_the_exact_native_ota_and_full(tmp_path):
    manifest = tmp_path / 'release/firmware/manifest.json'
    manifest.parent.mkdir(parents=True)
    ota, full = b'native OTA', b'native Full'
    manifest.write_text(json.dumps({
        'appVersion': '0.2.2.1',
        'targets': {'nrf52811_aesl': {'artifacts': {
            'ota': [{'bytes': len(ota), 'sha256': digest(ota)}],
            'full': [{'bytes': len(full), 'sha256': digest(full)}],
        }}},
    }), encoding='utf-8')
    assert nordic_version_from_inventory(tmp_path, 'nrf52811_aesl', ota, full) == '0.2.2.1'
    assert nordic_version_from_inventory(tmp_path, 'nrf52811_aesl', ota, b'stale Full') is None
    assert nordic_version_from_inventory(tmp_path, 'nrf52811_aesl', b'stale OTA', full) is None
