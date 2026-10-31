"""Anonymous official firmware discovery, independent of BLE and OTA execution."""
import json,re
from urllib.request import Request,urlopen
URL='https://solar2ain.github.io/ink-tag/updates/stable.json'
LAYOUTS={'gr5331_zkel':'layout-v1','gr551x_zkel':'layout-v1','gr5513_zkel_internal':'layout-v1',
 'om6626_zkcxxb':'layout-v1','tlsr8359_hanshow':'layout-v1','aesl22_nrf':'N1V1','nrf52832_priceetag':'N3V1','da14585_aesl':'DAV1'}

def _integer(value,positive=False):
    return type(value) is int and (1 if positive else 0)<=value<=2**53-1

def _version(text):
    if not isinstance(text,str) or not re.fullmatch(r'(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:\.(?:0|[1-9]\d*))?',text):
        raise ValueError('Invalid firmware version')
    parts=tuple(map(int,text.split('.')))
    if any(n>2**53-1 for n in parts):raise ValueError('Invalid firmware version')
    return parts+(0,)*(4-len(parts))

def _validate_fields(feed):
    """Reject malformed JSON types before matching; bool is not an integer."""
    if (not isinstance(feed,dict) or type(feed.get('schemaVersion')) is not int or feed['schemaVersion']!=3
        or feed.get('channel')!='stable' or feed.get('candidateOnly') is not False
        or type(feed.get('releaseReady')) is not bool or not isinstance(feed.get('firmwareSelection'),dict)
        or type(feed['firmwareSelection'].get('published')) is not bool or not isinstance(feed.get('firmware'),list)):
        raise ValueError('Invalid stable firmware manifest')
    seen=set()
    for item in feed['firmware']:
        if (not isinstance(item,dict) or any(type(item.get(k)) is not bool for k in ('published','releaseReady','candidateOnly'))
            or item.get('channel') not in {'stable','preview'} or not _integer(item.get('releaseVersion'))):
            raise ValueError('Invalid firmware publication state')
        _version(item.get('version'))
        target=item.get('target')
        if (not isinstance(target,dict) or set(target)!={'boardId','family','protocolVersion','layout','compatibilityEpoch'}
            or not (_integer(target['boardId']) or isinstance(target['boardId'],str) and re.fullmatch('[a-z0-9_]+',target['boardId']))
            or not _integer(target['protocolVersion']) or not _integer(target['compatibilityEpoch'])
            or not isinstance(target['family'],str) or not re.fullmatch('[a-z0-9_]+',target['family'])
            or not isinstance(target['layout'],str) or not re.fullmatch('[A-Za-z0-9._-]+',target['layout'])):
            raise ValueError('Invalid exact firmware target')
        identity=json.dumps(target,sort_keys=True)
        if identity in seen:raise ValueError('Duplicate firmware target')
        seen.add(identity)
        tag=item.get('releaseTag')
        if not isinstance(tag,str) or not re.fullmatch(r'v\d+\.\d+\.\d+(?:-\d{8}\.[1-9]\d*|\+[A-Za-z0-9.-]+)?',tag):raise ValueError('Invalid stable release tag')
        assets=item.get('artifacts')
        if not isinstance(assets,list) or len(assets)!=2:raise ValueError('Firmware requires an OTA/complete pair')
        names=set();kinds=set()
        for a in assets:
            if (not isinstance(a,dict) or a.get('type') not in {'ota','full'} or a['type'] in kinds
                or not isinstance(a.get('name'),str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,180}',a['name'])
                or a['name'] in names or a.get('downloadUrl')!=f'https://github.com/solar2ain/ink-tag/releases/download/{tag}/{a["name"]}'
                or not isinstance(a.get('sha256'),str) or not re.fullmatch('[a-f0-9]{64}',a['sha256']) or not _integer(a.get('bytes'),True)):
                raise ValueError('Unsafe firmware attachment')
            names.add(a['name']);kinds.add(a['type'])
    return feed

def _validate(feed):
    try:
        return _validate_fields(feed)
    except (KeyError,TypeError,AttributeError,OverflowError) as error:
        raise ValueError('Malformed firmware manifest') from error

def check_catalog(family,current_version,feed=None):
    if family not in LAYOUTS or _version(current_version)[:2]!=(0,2):
        raise ValueError('An exact0.2 firmware family and current version are required')
    if feed is None:
        with urlopen(Request(URL,headers={'Accept':'application/json'}),timeout=12) as response:
            if response.geturl()!=URL:raise ValueError('Unexpected update manifest redirect')
            body=response.read(2*1024*1024+1)
        if len(body)>2*1024*1024:raise ValueError('Update manifest too large')
        feed=json.loads(body)
    _validate(feed)
    exact=dict(boardId=family,family=family,protocolVersion=16,layout=LAYOUTS[family],compatibilityEpoch=1)
    matches=[]
    for item in feed.get('firmware',[]):
        if item.get('target')!=exact:continue
        if not all(item.get(k) is v for k,v in [('published',True),('releaseReady',True),('candidateOnly',False)]) or item.get('channel')!='stable':continue
        matches.append(item)
    if not feed.get('releaseReady') or not feed.get('firmwareSelection',{}).get('published'):matches=[]
    latest=max(matches,key=lambda i:(_version(i['version']),i['releaseVersion']),default=None)
    return dict(status='not-published' if latest is None else 'available' if _version(latest['version'])>_version(current_version) else 'current',target=exact,currentVersion=current_version,firmware=latest)
