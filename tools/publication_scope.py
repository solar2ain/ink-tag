"""Exact client/firmware publication scope; no credentials or device operations."""
from pathlib import Path
import hashlib, json, zipfile, struct, sys

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def artifacts(release):
    values=[a for c in release['components'].values() for a in c['artifacts']]
    values += [a for f in release.get('firmware',[]) for a in f['artifacts']]
    values += release.get('sourceMaterials',[])
    result={}
    for a in values:
        if a['name'] in result and result[a['name']]!=a:raise ValueError('Conflicting release attachment')
        result[a['name']]=a
    return list(result.values())

def firmware_binding(repo, record, release, manifest):
    selected=release.get('firmware',[])
    if not selected:return {}
    if record.get('firmwareEnabled') is not True or manifest.get('releaseTag')!=release['releaseTag']:
        raise ValueError('New explicit firmware publication policy is required')
    boards={b['buildTarget']:b for b in manifest['boards'] if b.get('artifacts')}
    tools={t['buildTarget']:t for t in manifest.get('programmerTools',[])}
    result={}
    for item in selected:
        target=item['buildTarget'];board=boards.get(target);tool=tools.get(target)
        if target in result or not board or not tool:raise ValueError('Unknown/duplicate firmware target or missing tools')
        fields=('target','version','releaseVersion','signaturePolicy','pairing')
        if any(item[k]!=board['publication'][k] for k in fields):raise ValueError('Firmware scope differs from native manifest')
        expected={k:tool[k] for k in ('toolsVersion','inventory','bytes','sha256')}
        if item['programmerInventory']!=expected:raise ValueError('Firmware programmer inventory differs')
        inventory=repo/expected['inventory']
        if not inventory.is_file() or inventory.stat().st_size!=expected['bytes'] or sha(inventory)!=expected['sha256']:
            raise ValueError('Programmer inventory bytes changed')
        native={(a['type'],a['name']):(a['bytes'],a['sha256']) for a in board['artifacts']}
        actual={(a['type'],a['name']):(a['bytes'],a['sha256']) for a in item['artifacts']}
        if native!=actual or {a['type'] for a in item['artifacts']}!={'ota','full'} or len(item['artifacts'])!=2:
            raise ValueError('OTA/complete firmware pair differs')
        result[target]={k:item[k] for k in ('target','version','releaseVersion','buildId','signaturePolicy','pairing','programmerInventory','artifacts')}
    if set(result)!=set(boards):raise ValueError('Approved firmware selection must exactly match the manifest')
    # Existing inventories bind every tool file; reject a changed backend even
    # when the high-level inventory row was left untouched.
    from firmware_inventory import programmer_files
    _,errors=programmer_files(repo,manifest,require_assets=False)
    if errors:raise ValueError('; '.join(errors))
    return result

def verify_firmware_bytes(repo, release, assets):
    if not release.get('firmware'):return
    sys.path.insert(0,str(repo/'cli/src'))
    from inktag_cli.ota import inspect_package
    for item in release['firmware']:
        refs={a['type']:a for a in item['artifacts']};ota=assets/refs['ota']['name'];full=assets/refs['full']['name']
        for path,ref in ((ota,refs['ota']),(full,refs['full'])):
            if path.stat().st_size!=ref['bytes'] or sha(path)!=ref['sha256']:raise ValueError('Firmware bytes changed')
        info=inspect_package(ota)
        if info.board!=item['signaturePolicy']['packageBoard']:raise ValueError('Signed firmware family differs from approved target')
        release_version=int(info.details.get('releaseVersion',info.version))
        layout=info.details.get('signedLayout',info.details.get('layout'))
        if (release_version!=item['releaseVersion'] or info.details.get('compatibilityEpoch')!=item['target']['compatibilityEpoch']
            or layout!=item['target']['layout']):raise ValueError('Signed firmware version/layout/epoch differs from approved target')
        version=[int(n) for n in item['version'].split('.')]
        native_release=(release_version & 65535) if info.transport=='nordic' else release_version
        if len(version) not in (3,4) or version[0]*10000+version[1]*100+version[2]!=native_release:
            raise ValueError('Published version does not match the authenticated firmware release')
        if full.suffix=='.zip':
            with zipfile.ZipFile(full) as z:
                if z.read('app-ota.zip')!=ota.read_bytes():raise ValueError('Complete firmware embeds a different OTA')
        else:
            data=full.read_bytes();size=struct.unpack_from('<I',data,0x4004)[0]
            native=data[0x4000:0x4040+size];incoming=ota.read_bytes()
            # SDK6's fixed installed bank is1; a SUOTA package has role0xff.
            # Exactly that one header byte differs. Signed payload and every
            # other header byte must match; never normalize CRC/code/context.
            if native[3]!=1 or incoming[3]!=0xff or native[:3]+native[4:]!=incoming[:3]+incoming[4:]:raise ValueError('DA complete firmware embeds a different App')
        expected={'otaSha256':refs['ota']['sha256'],'fullSha256':refs['full']['sha256'],'embeddedOtaSha256':refs['ota']['sha256']}
        if full.suffix!='.zip':expected.update(nativeHeaderRoles={'ota':255,'full':1},signedApplicationSha256=hashlib.sha256(ota.read_bytes()[64:]).hexdigest())
        if item['pairing']!=expected:
            raise ValueError('Incomplete OTA/complete firmware pairing binding')
