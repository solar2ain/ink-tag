#!/usr/bin/env python3
"""Repackage existing compiled clients only. Never publish or read another repo."""
from __future__ import annotations
import argparse, hashlib, html, importlib.metadata, importlib.util, json, os, re
from pathlib import Path
import shutil, subprocess, sys, tempfile, uuid, zipfile
from datetime import datetime, timezone
from urllib.parse import urlsplit,unquote
import markdown
from release_checks import check_cli_archive

ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write_json(p,value):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def zip_files(path,files):
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,body in sorted(files.items()):
            i=zipfile.ZipInfo(name,(2026,10,20,0,0,0));i.compress_type=zipfile.ZIP_DEFLATED;i.external_attr=0o100644<<16;z.writestr(i,body)
def files_below(root):
    files={}
    for p in root.rglob('*'):
        if any(x in {'__pycache__','.venv','build','dist'} or x.endswith('.egg-info') for x in p.relative_to(root).parts):continue
        if p.is_symlink():raise ValueError('Packaging links are forbidden')
        if p.is_file():files[p.relative_to(root).as_posix()]=p.read_bytes()
    return files
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def manuals():
    sources=list((ROOT/'docs').rglob('*.md'))
    sources += [ROOT/n for n in ['README.md','README.en.md','LICENSING.md','THIRD_PARTY_NOTICES.md','SECURITY.md','LEGAL_REVIEW_REQUIRED.md','release-materials.md']]
    mapping={p:ROOT/'webui/manual'/p.relative_to(ROOT).with_suffix('.html') for p in sources}
    for p in list(mapping):
        if p.is_relative_to(ROOT/'docs'):mapping[p]=ROOT/'webui/manual'/p.relative_to(ROOT/'docs').with_suffix('.html')
    for source,target in mapping.items():
        body=markdown.markdown(source.read_text('utf-8'),extensions=['tables','fenced_code'])
        def fix(match):
            value=html.unescape(match[1]);parts=urlsplit(value)
            if parts.scheme or parts.netloc or not parts.path:return match[0]
            original=(source.parent/unquote(parts.path)).resolve()
            if not original.is_relative_to(ROOT):return match[0]
            if original in mapping:new=mapping[original]
            elif original==ROOT/'cli/LICENSE':new=ROOT/'webui/licenses/CLI-AGPL-3.0-only.txt'
            else:return match[0]
            return 'href="'+html.escape(os.path.relpath(new,target.parent).replace('\\','/')+('#'+parts.fragment if parts.fragment else ''),quote=True)+'"'
        body=re.sub(r'href="([^"]+)"',fix,body)
        lang='en' if source.name.endswith('.en.md') or 'en' in source.parts else 'zh-CN'
        css=os.path.relpath(ROOT/'webui/manual/style.css',target.parent).replace('\\','/')
        target.parent.mkdir(parents=True,exist_ok=True)
        home=os.path.relpath(ROOT/'webui/index.html',target.parent).replace('\\','/')
        target.write_text('<!doctype html>\n<html lang="'+lang+'"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="'+css+'"><title>InkTag manual</title></head><body><nav><a href="'+home+'">InkTag WebUI</a></nav><main>'+body+'</main></body></html>\n',encoding='utf-8',newline='\n')
    shutil.copy2(ROOT/'cli/LICENSE',ROOT/'webui/licenses/CLI-AGPL-3.0-only.txt')
    return len(mapping)

def prepare():
    origin=subprocess.check_output(['git','remote','get-url','origin'],cwd=ROOT,text=True).strip()
    if origin!='https://github.com/solar2ain/ink-tag.git':raise ValueError('This tool is restricted to the release repository')
    for package,version in [('setuptools','80.9.0'),('wheel','0.45.1'),('build','1.2.2.post1'),('Markdown','3.7')]:
        if importlib.metadata.version(package)!=version:raise ValueError('Use the pinned local build environment: '+package+'=='+version)
    manifest=json.loads((ROOT/'firmware/manifest.json').read_text('utf-8'));tag=manifest['releaseTag']
    if tag!='v0.2.0' or manifest.get('flashTools') or any(b.get('artifacts') for b in manifest['boards']):raise ValueError('Only the clients-only first candidate may be repackaged')
    local=ROOT/'.release-local';local.mkdir(exist_ok=True)
    assets=ROOT/'.release-assets'/tag
    backup=local/'repack-snapshots'/uuid.uuid4().hex
    backup.mkdir(parents=True)
    for p in assets.iterdir():
        if p.is_file():shutil.copy2(p,backup/p.name)
    notice_names=['LICENSING.md','THIRD_PARTY_NOTICES.md','LEGAL_REVIEW_REQUIRED.md','release-materials.md']
    for name in notice_names:(ROOT/'webui/licenses'/name.replace('.md','.txt')).write_text((ROOT/name).read_text('utf-8'),encoding='utf-8',newline='\n')
    manual_count=manuals()
    notice_files={'THIRD_PARTY_NOTICES.txt':(ROOT/'webui/THIRD_PARTY_NOTICES.txt').read_bytes(),**files_below(ROOT/'webui/licenses')}
    links=''.join('<li><a href="'+html.escape(n,quote=True)+'">'+html.escape(n)+'</a></li>' for n in sorted(notice_files) if n not in {'THIRD_PARTY_NOTICES.txt','index.html'})
    (ROOT/'webui/licenses/index.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>InkTag licensing</title><h1>许可与第三方声明</h1><p>当前候选尚未通过公开许可审查。</p><ul>'+links+'</ul></html>',encoding='utf-8',newline='\n')
    with tempfile.TemporaryDirectory(prefix='client-package-',dir=local) as temp:
        temp=Path(temp);output=temp/'assets';output.mkdir();work=temp/'cli'
        shutil.copytree(ROOT/'cli',work,ignore=shutil.ignore_patterns('__pycache__','.venv','*.egg-info','build','dist'))
        with (local/'cli-build.log').open('w',encoding='utf-8') as log:
            env=dict(os.environ,TMP=str(temp),TEMP=str(temp),TMPDIR=str(temp),SOURCE_DATE_EPOCH='1792454400')
            subprocess.run([sys.executable,'-m','build','--no-isolation','--outdir',str(output),str(work)],check=True,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        cli_name=manifest['cli']['name'];zip_files(output/cli_name,{'inktag-cli/'+n:b for n,b in files_below(ROOT/'cli').items()})
        for p in [output/cli_name,*output.glob('*.whl'),*output.glob('*.tar.gz')]:
            errors=check_cli_archive(ROOT,p)
            if errors:raise ValueError('; '.join(errors))
        web_files=files_below(ROOT/'webui')
        static={n:b for n,b in web_files.items() if n not in {'.nojekyll','updates/stable.json','firmware/manifest.json'}}
        zip_files(output/manifest['web']['name'],{'webui/'+n:b for n,b in static.items()})
        with zipfile.ZipFile(assets/manifest['localServer']['name']) as original:
            service={n:original.read(n) for n in original.namelist() if not n.endswith('/') and '/webui/' not in n}
        prefix='InkTag-local-server/'
        for n in list(service):
            if n.startswith(prefix+'cli/'):
                current=ROOT/n[len(prefix):]
                if current.is_file():service[n]=current.read_bytes()
        for n in ['LICENSE','LICENSING.md','THIRD_PARTY_NOTICES.md',*['licenses/'+p.name for p in (ROOT/'cli/licenses').glob('*.txt')]]:
            service[prefix+'cli/'+n]=(ROOT/'cli'/n).read_bytes()
        for n in notice_names:service[prefix+n]=(ROOT/n).read_bytes()
        service.update({prefix+'webui/'+n:b for n,b in static.items()})
        zip_files(output/manifest['localServer']['name'],service)
        standalone=(assets/manifest['webStandalone']['name']).read_bytes().decode('utf-8')
        standalone=re.sub(r'<!--INKTAG-LICENSES-START-->[\s\S]*?<!--INKTAG-LICENSES-END-->','',standalone)
        notices='\n\n'.join('=== '+n+' ===\n'+b.decode('utf-8').replace('\r\n','\n') for n,b in sorted(notice_files.items()) if n.endswith(('.txt','.md','.json')))
        block='<!--INKTAG-LICENSES-START--><aside><details><summary>许可与第三方声明 / Licenses and notices</summary><pre style="white-space:pre-wrap;overflow-wrap:anywhere">'+html.escape(notices)+'</pre></details></aside><!--INKTAG-LICENSES-END-->'
        before,after=standalone.rsplit('</body>',1);standalone=before+block+'</body>'+after
        (output/manifest['webStandalone']['name']).write_bytes(standalone.encode('utf-8'))
        def artifact(name,kind):
            p=output/name
            return {'type':kind,'name':name,'bytes':p.stat().st_size,'sha256':sha(p),'downloadUrl':f'https://github.com/solar2ain/ink-tag/releases/download/{tag}/{name}'}
        for key in ('cli','web','webStandalone','localServer'):
            old=manifest[key];manifest[key]={**old,**artifact(old['name'],old['type'])}
        manifest['cliDistributions']=[artifact(p.name,'cli') for p in sorted(output.iterdir()) if p.suffix=='.whl' or p.name.endswith('.tar.gz')]
        manifest.pop('sourceRevision',None);manifest.pop('sourceTreeDirty',None)
        head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
        manifest.update(distributionBaseCommit=head,distributionTreeDirty=True,candidateOnly=True,releaseReady=False,licensingStatus='blocked-compiled-source-and-provenance-review',generatedAt=datetime.now(timezone.utc).isoformat())
        write_json(ROOT/'firmware/manifest.json',manifest);write_json(output/f'InkTag-v{manifest["version"]}-manifest.json',manifest)
        record=json.loads((ROOT/'releases/record.json').read_text('utf-8'));entry=next(x for x in record['releases'] if x['releaseTag']==tag)
        entry.update(candidateOnly=True,releaseReady=False,published=False,firmware=[])
        for k,items in [('webui',[manifest['web'],manifest['webStandalone']]),('cli',[manifest['cli'],*manifest['cliDistributions']]),('localServer',[manifest['localServer']])]:
            entry['components'][k]['artifacts']=items
        entry['components']['cli'].update(buildId='sha256:'+manifest['cli']['sha256'],buildNumber=2026102002)
        write_json(ROOT/'releases/record.json',record)
        notes=re.sub(r'\nPackaging review:[^\n]*(?:\n|$)','\n',(assets/'RELEASE_NOTES.md').read_text('utf-8'))
        (output/'RELEASE_NOTES.md').write_text(notes.rstrip()+'\n\nPackaging review: CLI-owned source AGPL-3.0-only; actual source ZIP/sdist/wheel license checks. Compiled renderer source, provenance and WebUI terms remain blockers. Not approved or published.\n',encoding='utf-8',newline='\n')
        lines=[sha(p)+'  '+p.name for p in sorted(output.iterdir()) if p.is_file() and p.name not in {'SHA256SUMS.txt','RELEASE_NOTES.md'}]
        (output/'SHA256SUMS.txt').write_text('\n'.join(lines)+'\n',encoding='ascii')
        for p in output.iterdir():shutil.copy2(p,assets/p.name)
    generator=module('feeds',ROOT/'tools/generate-updates.py');generator.write_feeds(ROOT,record,ROOT/'.release-assets')
    runtime={p.relative_to(ROOT).as_posix():sha(p) for base in ('cli/src','webui/assets') for p in (ROOT/base).rglob('*') if p.is_file() and '__pycache__' not in p.parts}
    write_json(ROOT/'releases/readiness.json',{'schemaVersion':1,'publicReleaseApproved':False,'cliCorrespondingSourceVerified':False,'webuiBinaryTermsApproved':False,'thirdPartyInventoryVerified':False,'sourceMaterials':[],'runtimeHashes':runtime,'blockers':['Renderer preferred-form source and reproducible build materials missing','Exact plugin/font/image provenance incomplete','WebUI binary terms require rights-holder decision','Pages not enabled; Actions account-payment/spending restriction is external'],'newIndependentApprovalRequired':True})
    subprocess.run([sys.executable,str(ROOT/'tools/verify-release.py'),'--repo',str(ROOT),'--assets',str(assets),'--candidate'],check=True,cwd=ROOT)
    names=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=ROOT).decode('utf-8').split('\0')
    snapshot={n:sha(ROOT/n) if (ROOT/n).is_file() else None for n in sorted(set(names)) if n}
    fingerprint=hashlib.sha256(json.dumps(snapshot,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    binding={'distributionBaseCommit':head,'finalCommit':None,'workingTreeUncommitted':True,'approval':False,'candidateFingerprint':fingerprint,'files':snapshot,'assets':{p.name:sha(p) for p in assets.iterdir() if p.is_file()},'manualsGenerated':manual_count,'publicReleaseBlocked':True}
    write_json(local/'prelaunch-candidate-binding.json',binding)
    print(json.dumps({'manualsGenerated':manual_count,'assets':len(binding['assets']),'publicReleaseBlocked':True,'finalCommit':None}))

if __name__=='__main__':prepare()
