#!/usr/bin/env python3
"""Generate anonymous Pages feeds from one reviewed release ledger (stdlib)."""
from __future__ import annotations
import argparse
from copy import deepcopy
import functools
import hashlib
import json
from pathlib import Path
import re

REPOSITORY = 'solar2ain/ink-tag'
SCHEMA = 3
KINDS = {'webui': {'web', 'web-standalone'}, 'cli': {'cli'}, 'localServer': {'local-server'}}
NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9._-]{0,180}\Z')
TAG = re.compile(r'v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?\Z')
class UpdateError(ValueError): pass

def version(value):
    if not isinstance(value,str): raise UpdateError('Version must be a string')
    match=re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?',value)
    if not match or any(int(n)>2**53-1 for n in match.groups()[:3]):raise UpdateError('Invalid semantic version')
    pre=match[4].split('.') if match[4] else None
    if pre and any(not n or re.fullmatch(r'0\d+',n) for n in pre):raise UpdateError('Invalid prerelease version')
    return tuple(map(int,match.groups()[:3])),pre

def compare(a,b):
    ac,ap=version(a);bc,bp=version(b)
    if ac!=bc:return (ac>bc)-(ac<bc)
    if ap is None or bp is None:return 0 if ap==bp else 1 if ap is None else -1
    for x,y in zip(ap,bp):
        if x==y:continue
        if x.isdigit() and y.isdigit():return (int(x)>int(y))-(int(x)<int(y))
        if x.isdigit()!=y.isdigit():return -1 if x.isdigit() else 1
        return (x>y)-(x<y)
    return (len(ap)>len(bp))-(len(ap)<len(bp))

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def asset(value,tag):
    if not isinstance(value,dict) or not isinstance(value.get('name'),str) or not NAME.fullmatch(value['name']):raise UpdateError('Invalid artifact name')
    if type(value.get('bytes')) is not int or not 0<value['bytes']<=2**53-1 or not isinstance(value.get('sha256'),str) or not re.fullmatch('[a-f0-9]{64}',value['sha256']):raise UpdateError('Invalid artifact size/SHA256')
    if value.get('downloadUrl')!=f'https://github.com/{REPOSITORY}/releases/download/{tag}/{value["name"]}':raise UpdateError('Only exact project Release URLs are allowed')
    return {k:value[k] for k in ('type','name','bytes','sha256','downloadUrl')}

def validate_record(record):
    if not isinstance(record,dict) or record.get('schemaVersion')!=1 or record.get('repository')!=REPOSITORY or record.get('firmwareEnabled') is not False or not isinstance(record.get('releases'),list):raise UpdateError('Unsupported release record or firmware policy')
    tags=set()
    for release in record['releases']:
        if not isinstance(release,dict):raise UpdateError('Release must be an object')
        tag=release.get('releaseTag','')
        if not isinstance(tag,str) or not TAG.fullmatch(tag) or tag in tags:raise UpdateError('Invalid/duplicate release tag')
        tags.add(tag)
        if release.get('channel') != 'stable' or any(type(release.get(k)) is not bool for k in ('candidateOnly','releaseReady','published')):raise UpdateError('Stable-only updates and explicit publication states are required')
        if release.get('firmware')!=[]:raise UpdateError('First release must not contain firmware')
        components=release.get('components')
        if not isinstance(components,dict) or set(components)-set(KINDS):raise UpdateError('Invalid component set')
        seen=set()
        for value in release.get('sourceMaterials',[]):
            checked=asset(value,tag)
            if checked['type']!='source-material' or checked['name'] in seen:raise UpdateError('Invalid corresponding-source attachment')
            seen.add(checked['name'])
        for key,item in components.items():
            if not isinstance(item,dict):raise UpdateError('Component must be an object')
            version(item.get('version'))
            if not isinstance(item.get('buildId'),str) or not re.fullmatch('[A-Za-z0-9._:-]{1,128}',item['buildId']) or type(item.get('buildNumber')) is not int or not 0<=item['buildNumber']<=2**53-1:raise UpdateError('Invalid build identity')
            if release['channel']=='stable' and ('-' in tag or '-' in item['version']):raise UpdateError('Stable cannot contain prereleases')
            if not isinstance(item.get('artifacts'),list) or not item['artifacts']:raise UpdateError('Empty component artifacts')
            for value in item['artifacts']:
                checked=asset(value,tag)
                if checked['type'] not in KINDS[key] or checked['name'] in seen:raise UpdateError('Artifact kind/duplication mismatch')
                seen.add(checked['name'])
    return record

def generate(record,assets_root:Path):
    validate_record(record)
    feeds={}
    for channel in ('stable',):
        selected={key:None for key in KINDS}
        for release in record['releases']:
            if release['channel']!=channel or release['candidateOnly'] or not release['releaseReady'] or not release['published']:continue
            for key,item in release['components'].items():
                # Verify actual, previously downloaded Release bytes before exposing a link.
                for value in item['artifacts']:
                    path=assets_root/release['releaseTag']/value['name']
                    if path.is_symlink() or not path.resolve().is_relative_to(assets_root.resolve()) or not path.is_file() or path.stat().st_size!=value['bytes'] or digest(path)!=value['sha256']:raise UpdateError('Missing or mismatched verified Release artifact: '+value['name'])
                current=selected[key]
                ordering=compare(item['version'],current['version']) if current else 1
                if current and ordering==0 and item['buildNumber']==current['buildNumber'] and item['buildId']!=current['buildId']:raise UpdateError('Ambiguous same-version build ordering')
                if ordering>0 or ordering==0 and item['buildNumber']>current['buildNumber']:
                    selected[key]={**deepcopy(item),'releaseTag':release['releaseTag'],'channel':channel,
                                   'candidateOnly':False,'releaseReady':True,'published':True}
        feeds[channel]={'schemaVersion':SCHEMA,'channel':channel,'candidateOnly':False,
                        'releaseReady':any(selected.values()),'components':selected,
                        'firmwareSelection':{'published':False},'firmware':[]}
    return feeds

def write_feeds(repo:Path,record:dict,assets:Path):
    feeds=generate(record,assets)
    output=repo/'webui/updates';output.mkdir(parents=True,exist_ok=True)
    for channel,value in feeds.items():
        (output/(channel+'.json')).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # Legacy schema-2 discovery never receives firmware or preview artifacts.
    legacy={'schemaVersion':2,'version':'0.0.0','releaseTag':'v0.0.0','channel':'stable',
            'sourceTreeDirty':False,'candidateOnly':False,'releaseReady':False,'boards':[],
            'firmwareSelection':{'published':False},'updates':'https://solar2ain.github.io/ink-tag/updates/stable.json'}
    old=repo/'webui/firmware/manifest.json';old.parent.mkdir(parents=True,exist_ok=True)
    old.write_text(json.dumps(legacy,indent=2)+'\n',encoding='utf-8')
    return feeds

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=Path.cwd())
    parser.add_argument('--record',type=Path)
    parser.add_argument('--assets',type=Path)
    args=parser.parse_args();repo=args.repo.resolve()
    record=json.loads((args.record or repo/'releases/record.json').read_text(encoding='utf-8'))
    write_feeds(repo,record,args.assets or repo/'.release-assets')
    print('Stable feed generated; no publication or deployment was performed.')
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,KeyError,TypeError) as error:raise SystemExit('Update generation refused: '+str(error))
