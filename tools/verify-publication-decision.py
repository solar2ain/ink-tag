"""Validate an explicit owner decision without changing incomplete review results."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from release_checks import public_readiness
from publication_scope import artifacts, firmware_binding

DEFERRED = {'cliCorrespondingSourceVerified', 'webuiBinaryTermsApproved', 'thirdPartyInventoryVerified'}
ALLOWED = {key + ' is not independently verified' for key in DEFERRED} | {
    'Compiled renderer corresponding-source materials are absent'}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def validate(repo: Path):
    decision = json.loads((repo/'releases/publication-decision.json').read_text('utf-8'))
    readiness_path = repo/'releases/readiness.json'
    readiness = json.loads(readiness_path.read_text('utf-8'))
    record_path = repo/'releases/record.json'
    record = json.loads(record_path.read_text('utf-8'))
    if (decision.get('ownerAuthorizedPublication') is not True or
        readiness.get('publicReleaseApproved') is not True or
        set(decision.get('deferredChecks', [])) != DEFERRED or
        decision.get('readinessSha256') != digest(readiness_path) or
        decision.get('recordSha256') != digest(record_path)):
        raise ValueError('Owner decision is absent or does not bind these release records')
    releases = [r for r in record['releases'] if r['releaseTag'] == decision.get('releaseTag')]
    if len(releases) != 1:
        raise ValueError('Owner decision must select exactly one release')
    actual = {a['name']: a['sha256'] for a in artifacts(releases[0])}
    if decision.get('artifacts') != actual:
        raise ValueError('Owner decision does not bind these client attachments')
    manifest_path = repo/'firmware/manifest.json'
    manifest = json.loads(manifest_path.read_text('utf-8')) if manifest_path.is_file() else {}
    binding = firmware_binding(repo, record, releases[0], manifest)
    if binding and (decision.get('schemaVersion') != 2 or
                    decision.get('scope') != 'clients-and-firmware' or
                    decision.get('firmwareBindings') != binding or
                    decision.get('manifestSha256') != digest(manifest_path)):
        raise ValueError('New exact firmware authorization is required; client-only decisions cannot be reused')
    if not binding and decision.get('scope') == 'clients-and-firmware':
        raise ValueError('Firmware decision has no selected firmware')
    tools = manifest.get('programmerTools', [])
    expected_tools = {item['buildTarget']: {key: item[key] for key in
        ('toolsVersion', 'inventory', 'bytes', 'sha256')} for item in tools}
    if tools and (decision.get('programmerToolsIncluded') is not True or
                  decision.get('programmerInventories') != expected_tools):
        raise ValueError('Owner decision does not bind these programmer inventories')
    if not tools and decision.get('programmerToolsIncluded') is True:
        raise ValueError('Owner decision claims absent programmer tools')
    problems = public_readiness(repo)
    unexpected = set(problems) - ALLOWED
    if unexpected:
        raise ValueError('Non-deferred release checks failed: ' + '; '.join(sorted(unexpected)))
    return problems

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    args = parser.parse_args()
    try:
        outstanding = validate(args.repo.resolve())
        print('Owner-authorized client publication; unresolved review results retained: ' + '; '.join(outstanding))
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(str(error))
