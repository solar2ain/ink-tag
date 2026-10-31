"""Operation-local fixed loader registration; no installed pack/device writes."""
import argparse
import copy
import hashlib
import json
import re
from pathlib import Path
import xml.etree.ElementTree as ET
from onmicro_full_loader import patched_loader,SOURCE_SHA256

XML_SHA256='12f821b52aabddca30300bd10710ecae3be313d08d3b70da90481abf36f9ec60'
MBR_SHA256='8523643fc138b84601c850571499068d36da954b60e7a3aa0be68386ad8e13af'
NAMES=('INKTAG_DEV_OM_NOMBR','INKTAG_DEV_OM_MBR','INKTAG_DEV_OM_LAST')
def sha(data):return hashlib.sha256(data).hexdigest()

def prepare(pack,output,namespace=''):
    if namespace and not re.fullmatch(r'[a-f0-9]{32}',namespace):
        raise ValueError('registration namespace must be a fresh hexadecimal UUID')
    names=tuple(name+'_'+namespace if namespace else name for name in NAMES)
    pack=Path(pack);out=Path(output)
    raw_xml=(pack/'JLinkDevices.xml').read_bytes()
    if sha(raw_xml)!=XML_SHA256:raise ValueError('maintained device XML changed')
    root=ET.fromstring(raw_xml)
    raw=(pack/'Devices/Onmicro/OM662XX_nombr.elf').read_bytes()
    mbr=(pack/'Devices/Onmicro/OM662XX.elf').read_bytes()
    if sha(raw)!=SOURCE_SHA256 or sha(mbr)!=MBR_SHA256:raise ValueError('maintained loader changed')
    sources={}
    for source_name in ('OM662XX_NOMBR','OM662XX'):
        rows=[node for node in root.findall('Device') if node.find('ChipInfo').get('Name')==source_name]
        if len(rows)!=1 or len(rows[0].findall('FlashBankInfo'))!=1:raise ValueError('ambiguous fixed device registration')
        sources[source_name]=rows[0]
    out.mkdir(parents=True,exist_ok=False);database=ET.Element('DataBase');hashes={}
    for name,source,data in zip(names,('OM662XX_NOMBR','OM662XX','OM662XX_NOMBR'),(raw,mbr,patched_loader(raw))):
        node=copy.deepcopy(sources[source]);node.find('ChipInfo').set('Name',name)
        filename=name+'.elf';node.find('FlashBankInfo').set('Loader',str((out/filename).resolve()) if namespace else filename)
        database.append(node);path=out/filename;path.write_bytes(data);hashes[str(path.resolve())]=sha(data)
    # Commander 7.88j silently ignores ElementTree's single-quoted XML prolog.
    # Match the maintained SDK database (no prolog); test with real ExpDevList,
    # not merely Python's more permissive XML parser.
    xml=out/'JLinkDevices.xml';ET.ElementTree(database).write(xml,encoding='utf-8',xml_declaration=False)
    hashes[str(xml.resolve())]=sha(xml.read_bytes())
    receipt={'directory':str(out.resolve()),'namespace':namespace,'devices':list(names),'files':hashes,
        'sourceXMLSHA256':XML_SHA256,'originalNoMBRSHA256':SOURCE_SHA256,'originalMBRSHA256':MBR_SHA256,
        'modifiedDeviceFields':['ChipInfo.Name','FlashBankInfo.Loader'],
        'loaderDelta':'final no-MBR variant only: two capacity-subtraction instructions; original loaders untouched',
        'installedPackModified':False,'statusRegisterCodeChanged':False,'CPFTCodeChanged':False}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    return receipt

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack',required=True);parser.add_argument('--out',required=True)
    parser.add_argument('--namespace',default='')
    args=parser.parse_args();print(json.dumps(prepare(args.pack,args.out,args.namespace)))
