"""Real manifest-entry checks using synthetic metadata, no release approval."""
import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('dated_asset_verifier', ROOT / 'tools/verify-release.py')
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


@pytest.mark.parametrize('tag,name,valid', [
    ('v0.2.0-20261031.2', 'InkTag-cli-v0.2.0-20261031.2.zip', True),
    ('v0.2.0-20261031.2', 'InkTag-cli-v0.2.0-20261031.1.zip', False),
    ('v0.2.0-20261031.2', 'InkTag-cli-v0.2.1-20261031.2.zip', False),
    ('v0.2.0-20261031.2', 'InkTag-cli-v0.2.0.zip', False),
    ('v0.2.0-20261031.0', 'InkTag-cli-v0.2.0-20261031.0.zip', False),
    ('v0.2.0', 'InkTag-cli-v0.2.0.zip', True),
    ('v0.2.0+20261031.1', 'InkTag-cli-v0.2.0-20261031.1.zip', True),
])
def test_cli_name_binds_exact_version_and_dated_batch(tmp_path, tag, name, valid):
    folder = tmp_path / 'firmware'; folder.mkdir()
    manifest = dict(schemaVersion=2, version='0.2.0', releaseTag=tag,
        components=['cli'], boards=[], candidateOnly=False, releaseReady=True,
        firmwareSelection={'published': False}, cli=dict(type='cli', name=name,
            bytes=1, sha256='a' * 64, requiresPython='>=3.11'))
    (folder / 'manifest.json').write_text(json.dumps(manifest), encoding='utf8')
    _, failures = verifier.validate_manifest(tmp_path)
    assert any('CLI asset name does not match version' in failure for failure in failures) is not valid
