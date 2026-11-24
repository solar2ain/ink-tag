"""Admit the single CRC-only Recovery project; never select a weaker policy."""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
POLICY_CODES = {'integrity-only': 2}

def assert_recovery_policy(project, expected='integrity-only'):
    if expected != 'integrity-only': raise ValueError('Retired Recovery signature policy')
    targets=[t for t in ET.parse(project).getroot().findall('./Targets/Target') if t.findtext('TargetName')=='DA14585']
    if len(targets)!=1: raise ValueError('Missing unique DA14585 Recovery target')
    target=targets[0];text=ET.tostring(target,encoding='unicode')
    if any(name in text for name in ('INKTAG_DA_OTA_SIGNATURE_REQUIRED','INKTAG_DA_OTA_INTEGRITY_ONLY','INKTAG_DA_OTA_POLICY_CODE')):
        raise ValueError('Recovery policy override or retired signature switch refused')
    flags=target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/MiscControls') or ''
    if re.findall(r'(?:^|\s)-DINKTAG_DA_FIXED_RECOVERY=([^\s]+)',flags)!=['1']:
        raise ValueError('Missing fixed Recovery role')
    if any(n in text.lower() for n in ('sha2.c','uecc_patch','micro_ecc','inktag_da_ota_public_key','app_easy_crypto.c')):
        raise ValueError('CRC-only Recovery retains a publisher-verification SDK dependency')
    return {'otaSignaturePolicy':'integrity-only','policyCode':2,'project':str(project)}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--expected',choices=('integrity-only',),default='integrity-only');a=p.parse_args()
    print(json.dumps(assert_recovery_policy(a.project,a.expected)))
if __name__=='__main__': main()
