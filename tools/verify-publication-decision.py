"""Validate an explicit owner decision without changing incomplete review results."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from release_checks import public_readiness

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
    if len(releases) != 1 or record.get('firmwareEnabled') is not False or releases[0].get('firmware') != []:
        raise ValueError('Owner decision must select one clients-only release')
    actual = {a['name']: a['sha256'] for component in releases[0]['components'].values() for a in component['artifacts']}
    if decision.get('artifacts') != actual:
        raise ValueError('Owner decision does not bind these client attachments')
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
