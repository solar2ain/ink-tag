from pathlib import Path
import copy,hashlib,importlib.util,json
import pytest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('feeds',ROOT/'tools/generate-updates.py');feeds=importlib.util.module_from_spec(spec);spec.loader.exec_module(feeds)

def fixture(tmp_path):
 record={'schemaVersion':1,'repository':feeds.REPOSITORY,'firmwareEnabled':False,'releases':[]}
 for tag,channel,build in [('v0.2.0','stable',1),('v0.2.0-pre.20261030.1','preview',2)]:
  folder=tmp_path/tag;folder.mkdir();path=folder/'client.zip';path.write_bytes(tag.encode())
  a={'type':'cli','name':'client.zip','bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'downloadUrl':f'https://github.com/{feeds.REPOSITORY}/releases/download/{tag}/client.zip'}
  record['releases'].append({'releaseTag':tag,'channel':channel,'prerelease':channel=='preview','published':True,'releaseReady':True,'candidateOnly':False,'firmware':[],'components':{'cli':{'version':'0.2.0','buildId':f'fixture-{build}','buildNumber':build,'artifacts':[a]}}})
 return record

def test_preview_does_not_replace_stable(tmp_path):
 record=fixture(tmp_path);result=feeds.generate(record,tmp_path)
 assert result['stable']['components']['cli']['buildNumber']==1
 assert result['preview']['components']['cli']['buildNumber']==2
 assert all(v['firmware']==[] and v['firmwareSelection']=={'published':False} for v in result.values())

@pytest.mark.parametrize('tag,valid',[('v0.2.0-20261031.2',True),('v0.2.0-pre.20261031.2',False),('v0.2.0-20261031.0',False)])
def test_formal_date_batch_is_distinct_from_preview(tmp_path,tag,valid):
 record=fixture(tmp_path);entry=record['releases'][0];entry['releaseTag']=tag
 folder=tmp_path/tag;folder.mkdir();(folder/'client.zip').write_bytes((tmp_path/'v0.2.0/client.zip').read_bytes())
 entry['components']['cli']['artifacts'][0]['downloadUrl']=f'https://github.com/{feeds.REPOSITORY}/releases/download/{tag}/client.zip'
 if valid:assert feeds.generate(record,tmp_path)['stable']['components']['cli']['releaseTag']==tag
 else:
  with pytest.raises(ValueError):feeds.generate(record,tmp_path)

@pytest.mark.parametrize('field,value',[('published',False),('releaseReady',False),('candidateOnly',True)])
def test_unpublished_preview_hidden(tmp_path,field,value):
 record=fixture(tmp_path);record['releases'][1][field]=value
 assert feeds.generate(record,tmp_path)['preview']['components']['cli'] is None

@pytest.mark.parametrize('change',['bad-hash','bad-url','wrong-channel','missing-prerelease','firmware'])
def test_preview_failures_are_closed(tmp_path,change):
 record=fixture(tmp_path);entry=record['releases'][1];asset=entry['components']['cli']['artifacts'][0]
 if change=='bad-hash':asset['sha256']='a'*64
 elif change=='bad-url':asset['downloadUrl']='https://example.invalid/client.zip'
 elif change=='wrong-channel':entry['channel']='stable'
 elif change=='missing-prerelease':entry.pop('prerelease')
 else:entry['firmware']=[{'type':'ota'}]
 with pytest.raises(ValueError):feeds.generate(record,tmp_path)

def test_empty_component_record_does_not_bypass_stable_channel_policy(tmp_path):
 record=fixture(tmp_path);entry=record['releases'][1]
 entry['channel']='stable';entry['components']={}
 with pytest.raises(ValueError):feeds.validate_record(record)
