#!/usr/bin/env python3
"""Release-auditor entry point. Dry-run by default; never used by browsers."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import sys

def module(path):
    prior=sys.dont_write_bytecode
    sys.dont_write_bytecode=True
    try:
        spec=importlib.util.spec_from_file_location('update_generation',path);value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
    finally:sys.dont_write_bytecode=prior
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def git(repo,*args):return subprocess.run(['git','-C',str(repo),*args],check=True,capture_output=True,text=True).stdout.strip()
def promote(repo:Path,tag:str,approval:Path|None,execute=False):
    repo=repo.resolve();generator=module(repo/'tools/generate-updates.py')
    record_path=repo/'releases/record.json';record=json.loads(record_path.read_text(encoding='utf-8'));generator.validate_record(record)
    release=next((r for r in record['releases'] if r['releaseTag']==tag),None)
    if not release:raise ValueError('Release not present in the reviewed record')
    if release['published']:raise ValueError('Release is immutable; use a new tag')
    files=[a for item in release['components'].values() for a in item['artifacts']]
    assets=repo/'.release-assets'/tag
    for item in files:
        path=assets/item['name']
        if not path.is_file() or path.stat().st_size!=item['bytes'] or digest(path)!=item['sha256']:raise ValueError('Attachment mismatch: '+item['name'])
    plan=['require independent hash-bound approval and clean main', 'push reviewed commit to a non-deploying staging branch', 'create draft Release with immutable attachments',
          'download uploaded Release attachments and verify bytes/SHA256', 'publish the verified Release',
          'generate the stable feed from the release record', 'commit/push only record and feeds; existing Pages workflow deploys webui/']
    if not execute:return {'dryRun':True,'plan':plan,'releaseTag':tag}
    if not approval:raise ValueError('An independent approval receipt is required')
    receipt=json.loads(approval.read_text(encoding='utf-8'))
    if receipt.get('allowPublish') is not True or receipt.get('releaseTag')!=tag or receipt.get('recordSha256')!=digest(record_path) or receipt.get('artifacts')!={a['name']:a['sha256'] for a in files}:raise ValueError('Approval does not bind this exact candidate')
    if git(repo,'remote','get-url','origin')!='https://github.com/solar2ain/ink-tag.git' or git(repo,'branch','--show-current')!='main' or git(repo,'status','--porcelain'):raise ValueError('Reviewed clean distribution main with the exact origin is required')
    if receipt.get('distributionHead')!=git(repo,'rev-parse','HEAD'):raise ValueError('Approval does not bind the reviewed distribution commit')
    manifest=json.loads((repo/'firmware/manifest.json').read_text(encoding='utf-8'))
    if manifest.get('sourceTreeDirty') is not False or manifest.get('firmwareSelection',{}).get('published') or any(b.get('artifacts') for b in manifest.get('boards',[])):raise ValueError('Freeze source inputs and keep first-release firmware empty before publication')
    workflow=(repo/'.github/workflows/pages.yml').read_text(encoding='utf-8')
    if 'path: webui' not in workflow or 'needs: validate' not in workflow:raise ValueError('Pages PR #1 must be reviewed/integrated before promotion')
    verifier=module(repo/'tools/verify-release.py')
    failures=verifier.validate_public_tree(repo,True)+verifier.validate_manifest(repo)[1]+verifier.validate_assets(manifest,assets)
    if failures:raise ValueError('Release validation failed: '+'; '.join(failures))
    # gh holds authentication locally/in Actions. Never export a token into JS.
    (repo/'.release-local').mkdir(exist_ok=True)
    command=['gh','release','create',tag,'--repo','solar2ain/ink-tag','--draft','--target',git(repo,'rev-parse','HEAD'),'--title',tag,'--notes','InkTag client release']
    command.extend(str(assets/a['name']) for a in files)
    metadata=['SHA256SUMS.txt',f"InkTag-v{manifest['version']}-manifest.json",'RELEASE_NOTES.md']
    if receipt.get('metadata')!={name:digest(assets/name) for name in metadata}:raise ValueError('Release metadata is not covered by the approval')
    command.extend(str(assets/name) for name in metadata)
    # Make the reviewed target reachable without deploying main before Release
    # verification. The existing Pages workflow runs on main only.
    git(repo,'push','origin','HEAD:refs/heads/codex/release-clients-'+tag)
    subprocess.run(command,cwd=repo,check=True)
    with tempfile.TemporaryDirectory(prefix='verify-release-',dir=repo/'.release-local') as temporary:
        downloaded=Path(temporary)
        subprocess.run(['gh','release','download',tag,'--repo','solar2ain/ink-tag','--dir',str(downloaded)],cwd=repo,check=True)
        for item in files:
            path=downloaded/item['name']
            if not path.is_file() or path.stat().st_size!=item['bytes'] or digest(path)!=item['sha256']:raise ValueError('Uploaded attachment verification failed; feeds unchanged')
        for name in metadata:
            if not (downloaded/name).is_file() or digest(downloaded/name)!=digest(assets/name):raise ValueError('Uploaded metadata verification failed; feeds unchanged')
    subprocess.run(['gh','release','edit',tag,'--repo','solar2ain/ink-tag','--draft=false'],cwd=repo,check=True)
    release.update(candidateOnly=False,releaseReady=True,published=True)
    record_path.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    generator.write_feeds(repo,record,repo/'.release-assets')
    paths=['releases/record.json','webui/updates/stable.json','webui/firmware/manifest.json']
    git(repo,'add','--',*paths);git(repo,'commit','-m','Publish verified client update feed '+tag);git(repo,'push','origin','HEAD:main')
    return {'published':True,'releaseTag':tag,'feedPaths':paths}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=Path.cwd());parser.add_argument('--tag',required=True)
    parser.add_argument('--approval',type=Path);parser.add_argument('--execute',action='store_true')
    args=parser.parse_args();print(json.dumps(promote(args.repo,args.tag,args.approval,args.execute),indent=2))
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,subprocess.CalledProcessError) as error:raise SystemExit('Publication refused/stopped (do not blindly retry): '+str(error))
