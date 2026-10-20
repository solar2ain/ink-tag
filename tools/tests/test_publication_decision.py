import hashlib
import importlib.util
import json
from pathlib import Path
import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('publication_decision', ROOT/'tools/verify-publication-decision.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)

@pytest.fixture
def candidate(tmp_path, monkeypatch):
    (tmp_path/'releases').mkdir()
    for name in ('readiness.json', 'record.json', 'publication-decision.json'):
        (tmp_path/'releases'/name).write_bytes((ROOT/'releases'/name).read_bytes())
    monkeypatch.setattr(guard, 'public_readiness', lambda repo: sorted(guard.ALLOWED))
    return tmp_path

def test_owner_decision_preserves_incomplete_verification(candidate):
    assert guard.validate(candidate)
    state = json.loads((candidate/'releases/readiness.json').read_text('utf-8'))
    assert all(state[key] is False for key in guard.DEFERRED)

def test_stale_decision_fails(candidate):
    path = candidate/'releases/record.json'
    path.write_text(path.read_text('utf-8')+'\n', encoding='utf-8')
    with pytest.raises(ValueError):
        guard.validate(candidate)

def test_owner_authorization_required(candidate):
    path = candidate/'releases/publication-decision.json'
    value = json.loads(path.read_text('utf-8'))
    value['ownerAuthorizedPublication'] = False
    path.write_text(json.dumps(value), encoding='utf-8')
    with pytest.raises(ValueError):
        guard.validate(candidate)

def test_new_failure_is_not_deferred(candidate, monkeypatch):
    monkeypatch.setattr(guard, 'public_readiness', lambda repo: ['Reviewed runtime hashes are missing or stale'])
    with pytest.raises(ValueError):
        guard.validate(candidate)

def test_firmware_is_not_authorized(candidate):
    path = candidate/'releases/record.json'
    value = json.loads(path.read_text('utf-8'))
    value['firmwareEnabled'] = True
    path.write_text(json.dumps(value), encoding='utf-8')
    approval = candidate/'releases/publication-decision.json'
    decision = json.loads(approval.read_text('utf-8'))
    decision['recordSha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    approval.write_text(json.dumps(decision), encoding='utf-8')
    with pytest.raises(ValueError):
        guard.validate(candidate)
