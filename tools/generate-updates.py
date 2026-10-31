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
CHANNELS = ('stable', 'preview')
KINDS = {'webui': {'web', 'web-standalone', 'web-resources'}, 'cli': {'cli'}, 'localServer': {'local-server'}}
NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9._-]{0,180}\Z')
TAG = re.compile(r'v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?\Z')
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

def firmware_version(value):
    if not isinstance(value,str) or not re.fullmatch(r'(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:\.(?:0|[1-9]\d*))?',value):raise UpdateError('Invalid firmware version')
    parts=tuple(map(int,value.split('.')))
    if any(n>2**53-1 for n in parts):raise UpdateError('Invalid firmware version')
    return parts+(0,)*(4-len(parts))

def firmware_target(item):
    target=item.get('target')
    if not isinstance(target,dict) or set(target)!={'boardId','family','protocolVersion','layout','compatibilityEpoch'}:raise UpdateError('Missing exact firmware target')
    if not (type(target['boardId']) is int and 0<=target['boardId']<=2**53-1 or isinstance(target['boardId'],str) and re.fullmatch('[a-z0-9_]+',target['boardId'])):raise UpdateError('Invalid exact firmware board identity')
    for key in ('protocolVersion','compatibilityEpoch'):
        if type(target[key]) is not int or not 0<=target[key]<=2**53-1:raise UpdateError('Invalid firmware target number')
    if not isinstance(target['family'],str) or not re.fullmatch('[a-z0-9_]+',target['family']) or not isinstance(target['layout'],str) or not re.fullmatch('[A-Za-z0-9._-]+',target['layout']):raise UpdateError('Invalid firmware family/layout')
    return json.dumps(target,sort_keys=True,separators=(',',':'))

def verified_artifacts(item,tag,assets_root):
    for value in item['artifacts']:
        path=assets_root/tag/value['name']
        if path.is_symlink() or not path.resolve().is_relative_to(assets_root.resolve()) or not path.is_file() or path.stat().st_size!=value['bytes'] or digest(path)!=value['sha256']:raise UpdateError('Missing or mismatched verified Release artifact: '+value['name'])
def asset(value,tag):
    if not isinstance(value,dict) or not isinstance(value.get('name'),str) or not NAME.fullmatch(value['name']):raise UpdateError('Invalid artifact name')
    if type(value.get('bytes')) is not int or not 0<value['bytes']<=2**53-1 or not isinstance(value.get('sha256'),str) or not re.fullmatch('[a-f0-9]{64}',value['sha256']):raise UpdateError('Invalid artifact size/SHA256')
    if value.get('downloadUrl')!=f'https://github.com/{REPOSITORY}/releases/download/{tag}/{value["name"]}':raise UpdateError('Only exact project Release URLs are allowed')
    return {k:value[k] for k in ('type','name','bytes','sha256','downloadUrl')}

def validate_record(record):
    if not isinstance(record,dict) or record.get('schemaVersion')!=1 or record.get('repository')!=REPOSITORY or type(record.get('firmwareEnabled')) is not bool or not isinstance(record.get('releases'),list):raise UpdateError('Unsupported release record or firmware policy')
    tags=set()
    for release in record['releases']:
        if not isinstance(release,dict):raise UpdateError('Release must be an object')
        tag=release.get('releaseTag','')
        if not isinstance(tag,str) or not TAG.fullmatch(tag) or tag in tags:raise UpdateError('Invalid/duplicate release tag')
        tags.add(tag)
        if release.get('channel') not in CHANNELS or any(type(release.get(k)) is not bool for k in ('candidateOnly','releaseReady','published')):raise UpdateError('Known update channels and explicit publication states are required')
        if release['channel']=='preview' and ('-' not in tag or release.get('prerelease') is not True):raise UpdateError('Preview requires a prerelease tag and explicit prerelease status')
        dated_release=bool(re.fullmatch(r'v\d+\.\d+\.\d+-\d{8}\.[1-9]\d*',tag))
        if release['channel']=='stable' and (release.get('prerelease') is True or '-' in tag and not dated_release):raise UpdateError('Stable cannot contain prereleases')
        if not isinstance(release.get('firmware'),list):raise UpdateError('Firmware must be a list')
        if not record['firmwareEnabled'] and release['firmware']:raise UpdateError('This client-only release must not contain firmware')
        components=release.get('components')
        if not isinstance(components,dict) or set(components)-set(KINDS):raise UpdateError('Invalid component set')
        seen=set()
        for key,item in components.items():
            if not isinstance(item,dict):raise UpdateError('Component must be an object')
            version(item.get('version'))
            if not isinstance(item.get('buildId'),str) or not re.fullmatch('[A-Za-z0-9._:-]{1,128}',item['buildId']) or type(item.get('buildNumber')) is not int or not 0<=item['buildNumber']<=2**53-1:raise UpdateError('Invalid build identity')
            if release['channel']=='stable' and '-' in item['version']:raise UpdateError('Stable cannot contain prereleases')
            if not isinstance(item.get('artifacts'),list) or not item['artifacts']:raise UpdateError('Empty component artifacts')
            for value in item['artifacts']:
                checked=asset(value,tag)
                if checked['type'] not in KINDS[key] or checked['name'] in seen:raise UpdateError('Artifact kind/duplication mismatch')
                seen.add(checked['name'])
        targets=set();firmware_assets={}
        for item in release['firmware']:
            if not isinstance(item,dict) or any(type(item.get(k)) is not bool for k in ('candidateOnly','releaseReady','published')):raise UpdateError('Explicit firmware publication states are required')
            firmware_version(item.get('version'));key=firmware_target(item)
            if key in targets:raise UpdateError('Duplicate exact firmware target')
            targets.add(key)
            if type(item.get('releaseVersion')) is not int or not 0<=item['releaseVersion']<=2**53-1 or type(item.get('buildNumber')) is not int or not 0<=item['buildNumber']<=2**53-1 or not isinstance(item.get('buildId'),str) or not re.fullmatch('[A-Za-z0-9._:-]{1,128}',item['buildId']):raise UpdateError('Invalid firmware build identity')
            if not isinstance(item.get('artifacts'),list) or not item['artifacts']:raise UpdateError('Missing firmware artifacts')
            kinds=[];entry_names=set()
            for value in item['artifacts']:
                checked=asset(value,tag)
                if checked['type'] not in {'ota','full'} or checked['name'] in seen or checked['name'] in entry_names:raise UpdateError('Invalid firmware artifact kind/duplication')
                if checked['name'] in firmware_assets and firmware_assets[checked['name']]!=checked:raise UpdateError('Conflicting shared firmware artifact')
                firmware_assets[checked['name']]=checked;entry_names.add(checked['name']);kinds.append(checked['type'])
            if kinds.count('ota')!=1 or 'full' not in kinds:raise UpdateError('Firmware requires OTA and complete images')
    return record

def generate(record,assets_root:Path):
    validate_record(record)
    feeds={}
    for channel in CHANNELS:
        selected={key:None for key in KINDS}
        firmware={}
        for release in record['releases']:
            if release['channel']!=channel or release['candidateOnly'] or not release['releaseReady'] or not release['published']:continue
            for key,item in release['components'].items():
                # Verify actual, previously downloaded Release bytes before exposing a link.
                verified_artifacts(item,release['releaseTag'],assets_root)
                current=selected[key]
                ordering=compare(item['version'],current['version']) if current else 1
                if current and ordering==0 and item['buildNumber']==current['buildNumber'] and item['buildId']!=current['buildId']:raise UpdateError('Ambiguous same-version build ordering')
                if ordering>0 or ordering==0 and item['buildNumber']>current['buildNumber']:
                    selected[key]={**deepcopy(item),'releaseTag':release['releaseTag'],'channel':channel,
                                   'candidateOnly':False,'releaseReady':True,'published':True}
            for item in release['firmware']:
                if item['candidateOnly'] or not item['releaseReady'] or not item['published']:continue
                verified_artifacts(item,release['releaseTag'],assets_root)
                key=firmware_target(item);current=firmware.get(key)
                order=(firmware_version(item['version']),item['releaseVersion'])
                previous=(firmware_version(current['version']),current['releaseVersion']) if current else None
                if previous==order and any(item[k]!=current[k] for k in ('buildId','artifacts')):raise UpdateError('Ambiguous firmware release identity')
                if previous is None or order>previous:
                    firmware[key]={**deepcopy(item),'releaseTag':release['releaseTag'],'channel':channel}
        feeds[channel]={'schemaVersion':SCHEMA,'channel':channel,'candidateOnly':False,
                        'releaseReady':bool(firmware) or any(selected.values()),'components':selected,
                        'firmwareSelection':{'published':bool(firmware)},'firmware':[firmware[k] for k in sorted(firmware)]}
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
    print('Stable and prerelease metadata generated; no publication or deployment was performed.')
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,KeyError,TypeError) as error:raise SystemExit('Update generation refused: '+str(error))
