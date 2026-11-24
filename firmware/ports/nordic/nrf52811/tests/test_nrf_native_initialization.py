"""Real Windows BAT/PS5.1 entries, old pure codecs and a fake vendor boundary.

Developer test runner may use Python. User programmer entries do not. No real
J-Link executable is run; the temporary helper supplies an explicit fake.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,io,json,os,shutil,struct,subprocess,sys,zipfile
from fixtures import nrf11_initialization_oracle as oracle
from fixtures.jlink_close_model import CoreState

ROOT=Path(__file__).resolve().parents[1]
TOOLS=ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer'

def load(name,path):
    s=importlib.util.spec_from_file_location(name,ROOT/path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir',type=Path,required=True)
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This explicit Windows matrix requires Windows PowerShell5.1; not a skipped pass.')
    e=args.evidence_dir.absolute();e.mkdir(parents=True,exist_ok=False)
    package=e/'native package with spaces'
    subprocess.run([sys.executable,'-B',str(ROOT/'build-support/source/export-nrf52811-native-tools.py'),str(package)],check=True)
    tools=package/'tools'
    assert sorted(p.name for p in tools.glob('*.bat'))==['flash-hex.bat','flash-zh.bat','flash.bat','generate-hex.bat']
    assert json.loads((tools/'native-tools.json').read_text())['toolsVersion']=='0.2.2.5'
    helper=tools/'internal/initialization-jlink.ps1'
    helper.write_bytes(helper.read_bytes()+b'\n'+(ROOT/'tests/fixtures/nrf11_initialization_fake.ps1').read_bytes())
    # Replace only the native Windows dialog boundary in the temporary package.
    picker=tools/'internal/initialization-format.ps1'
    picker.write_bytes(picker.read_bytes()+b'''\nfunction Select-InitFirmwareFile([string]$Title) {
        [IO.File]::WriteAllText($env:INKTAG_NRF_TEST_DIALOG, $Title)
        return $env:INKTAG_NRF_TEST_SELECTED_FULL
    }\n''')
    native=e/'fake JLink with spaces';native.mkdir()
    for name in ['JLink.exe','JLinkARM.dll']:(native/name).write_bytes(b'TEST ONLY: NOT A NATIVE EXECUTABLE '+name.encode())
    lockpath=tools/'internal/initialization-dependencies.json';lock=json.loads(lockpath.read_text())
    for row in lock['native']['files']:row['sha256']=hashlib.sha256((native/row['name']).read_bytes()).hexdigest()
    lockpath.write_text(json.dumps(lock)+'\n')
    # Rebind only this test fixture package to its fake process boundary. The
    # production exporter and native file pins are unchanged.
    marker=tools/'native-tools.json';native_inventory=json.loads(marker.read_text())
    for name,row in native_inventory['files'].items():
        data=(tools/name).read_bytes();row.update(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    marker.write_text(json.dumps(native_inventory)+'\n')
    inventory=json.loads((package/'package-files.json').read_text())
    for name,row in inventory['files'].items():
        data=(package/name).read_bytes();row.update(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    (package/'package-files.json').write_text(json.dumps(inventory)+'\n')
    codec=load('native_init_format','../../../../tools/platforms/nordic/nrf52811/programmer/internal/nrf11_full_format.py')
    packer=load('native_init_ota','build-support/ota/package-nrf52811.py')
    builder=load('native_init_build','build-support/build/build-full.py')
    app=struct.pack('<II',0x20006000,0x19009)+b'HOST-ONLY-APP'*8+codec.FACTORY_PIN_MARKER
    app+=b'\xff'*(-len(app)%4)
    rec=struct.pack('<II',0x20006000,0x2a009)+b'HOST-ONLY-RECOVERY'*8
    sd=b'HOST-ONLY-S112'*7;sd_memory={0x1000+i:b for i,b in enumerate(sd)}
    payload=bytes((i*13+3)&255 for i in range(12228))
    assets=struct.pack('<IIII',0x44464b49,1,len(payload),__import__('zlib').crc32(payload))+payload+b'\xff'*44
    memory,ota,manifest,seed=builder.compose(codec,packer,sd_memory,app,rec,assets,202,'crc-only',struct.unpack_from('<I',assets,12)[0])
    firmware=e/'synthetic full with spaces.zip';builder.write_package(codec,firmware,memory,ota,assets,manifest)
    env=dict(os.environ,PATH=os.environ['SystemRoot']+'/System32',PYTHONHOME=str(e/'NO-PYTHON'),PYTHONPATH=str(e/'NO-CLI'))
    rows=[]
    def run(name,bat,arguments,expected=0,mode=None,input_text=None,entry_tools=None,selected_full=None):
        caseenv=dict(env,INKTAG_NRF_TEST_CASE=mode or 'normal',INKTAG_NRF_TEST_TRACE=str(e/(name+'-trace.json')),INKTAG_NRF_TEST_OLD_JOURNAL=str(e/'old-settings.bin'))
        caseenv.update(INKTAG_NRF_TEST_SELECTED_FULL=str(firmware if selected_full is None else selected_full),INKTAG_NRF_TEST_DIALOG=str(e/(name+'-dialog.txt')))
        caseenv['INKTAG_JLINK_HOME']=str(native)
        caseenv['TEMP']=caseenv['TMP']=str(e/'temp')
        (e/'temp').mkdir(exist_ok=True)
        cmdline=subprocess.list2cmdline([str((entry_tools or tools)/bat),*map(str,arguments)])
        # cmd.exe /s /c has its own outer-quote grammar, not CRT argv parsing.
        command='"'+str(Path(os.environ['SystemRoot'])/'System32/cmd.exe')+'" /d /s /c "'+cmdline+'"'
        r=subprocess.run(command,cwd=e,env=caseenv,input=input_text,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (e/(name+'.stdout.log')).write_text(r.stdout);(e/(name+'.stderr.log')).write_text(r.stderr)
        row={'name':name,'argv':command,'exit':r.returncode,'expected':expected,'fakeMode':mode,'realVendorProcess':False}
        rows.append(row);(e/'running.json').write_text(json.dumps(rows,indent=2)+'\n')
        assert r.returncode==expected,(name,r.returncode,r.stdout[-1000:],r.stderr[-2000:])
        print('PASS '+name,flush=True)
        return r
    config=tools/'native-configs/EL022HWRL.toml';materials=e/'generated materials with spaces'
    run('generate-original','generate-hex.bat',['-Firmware',firmware,'-Config',config,'-Output',materials])
    cfg=oracle.read_config(config)
    factory=b'N1P1'+bytes.fromhex(cfg['factory_pins_hex'])+bytes.fromhex(cfg['screen_wire_hex'])+struct.pack('<H',cfg['active_low']);factory+=struct.pack('<I',oracle.fnv(factory))
    screen=b'SP\x01\x11'+bytes.fromhex(cfg['screen_wire_hex'])+bytes(3);screen+=struct.pack('<I',oracle.fnv(screen))
    boot=struct.pack('<IBBHII',0x3153424e,1,1,0,len(app),__import__('zlib').crc32(app))
    journal=struct.pack('<4I',0x31505349,1,0xfffffffe,0xffffffff)+b''.join(oracle.compact_record(k,v) for k,v in [(1,factory),(3,screen),(12,boot)])
    def assert_image(name, start, expected):
        reference=e/('oracle-'+name)
        codec.write_hex(reference, {start+i:value for i,value in enumerate(expected)})
        assert (materials/name).read_bytes()==reference.read_bytes()
    assert not list(materials.glob('*.bin'))
    assert_image('settings.hex',0,journal.ljust(8192,b'\xff'))
    (e/'old-settings.bin').write_bytes(journal.ljust(8192,b'\xff'))
    expected=bytes(memory.get(a,255) for a in range(196608))
    assert_image('internal.hex',0,expected)
    assert_image('gui-assets.hex',8192,assets)
    for a in [0x2e000,0x2f000]:assert expected[a+0x323:a+4096]==b'\xff'*(4096-0x323)
    run('generate-a0','generate-hex.bat',['-Firmware',firmware,'-Config',tools/'native-configs/EL022HWRL-A0.toml','-Output',e/'A0 materials'])
    run('reject-existing-output','generate-hex.bat',['-Firmware',firmware,'-Config',config,'-Output',materials],1)
    for bat in ['generate-hex.bat','flash-hex.bat','flash.bat']:
        run('cancel-'+bat,bat,[],input_text='\n')
        help_result=run('help-'+bat,bat,['-Help'])
        assert 'Recommended: flash.bat -Firmware' in help_result.stdout
    run('flash-dry-run','flash-hex.bat',['-Materials',materials,'-DryRun'])
    parent_bundle=package/'bundle.json';parent_bytes=parent_bundle.read_bytes()
    parent_bundle.write_text(json.dumps({'runtimeKind':'portable-python','toolsVersion':'0.2.0.3'})+'\n')
    run('legacy-parent-coexistence','flash-hex.bat',['-Materials',materials,'-DryRun'])
    parent_bundle.write_bytes(parent_bytes)
    # Guided input uses the same extracted package and fake native boundary.
    guided=['-JLinkExe',native/'JLink.exe','-Snr','12345','-Output',e/'guided flash']
    r=run('guided-board-flash','flash.bat',guided,input_text='1\n"'+str(firmware)+'"\ny\n')
    assert '1. EL022HWRL / CAL_VER06 [EL022HWRL]' in r.stdout
    assert '2. EL022HWRL / CAL_VER06 [EL022HWRL-A0]' in r.stdout
    assert 'Board: EL022HWRL / CAL_VER06' in r.stdout and 'Preservation:' in r.stdout
    assert json.loads((e/'guided-board-flash-trace.json').read_text())['flashWrites']==1
    r=run('guided-a0-config','flash.bat',['-Firmware',firmware,'-DryRun'],input_text='2\n')
    assert 'Full ZIP path with' not in r.stdout and not (e/'guided-a0-config-trace.json').exists()
    assert hashlib.sha256((tools/'native-configs/EL022HWRL-A0.toml').read_bytes()).hexdigest() in r.stdout
    r=run('guided-supplied-config','flash.bat',['-Config',config,'-DryRun'],input_text=str(firmware)+'\n')
    assert 'Board configuration number' not in r.stdout and not (e/'guided-supplied-config-trace.json').exists()
    r=run('guided-final-cancel','flash.bat',['-Firmware',firmware,'-Config',config],input_text='\n')
    assert 'Board: EL022HWRL / CAL_VER06' in r.stdout and not (e/'guided-final-cancel-trace.json').exists()
    run('guided-empty-firmware','flash.bat',['-Config',config],input_text='\n')
    assert not (e/'guided-empty-firmware-trace.json').exists()
    r=run('guided-invalid-selection','flash.bat',[],1,input_text='0\n')
    assert 'Invalid board number' in r.stderr and not (e/'guided-invalid-selection-trace.json').exists()
    generated=e/'guided generation with spaces'
    run('guided-generate','generate-hex.bat',[],input_text='1\n'+str(firmware)+'\n'+str(generated)+'\n')
    assert (generated/'settings.hex').read_bytes()==(materials/'settings.hex').read_bytes()
    assert not (e/'guided-generate-trace.json').exists()
    r=run('guided-materials-cancel','flash-hex.bat',[],input_text=str(materials)+'\n\n')
    assert 'Board configuration number' not in r.stdout and not (e/'guided-materials-cancel-trace.json').exists()
    run('automated-missing-input','flash.bat',['-Config',config,'-Yes'],1)
    assert not (e/'automated-missing-input-trace.json').exists()
    # Double-click confirms complete flashing before any device access.
    r=run('dialog-cancel','flash.bat',[],input_text='',selected_full='')
    assert 'Press any key to close' in r.stdout and not (e/'dialog-cancel-trace.json').exists()
    bad_dialog=e/'not-a-full.zip';bad_dialog.write_bytes(b'not a ZIP')
    run('dialog-invalid','flash.bat',[],1,input_text='',selected_full=bad_dialog)
    assert not (e/'dialog-invalid-trace.json').exists()
    ota_only=e/'only-ota.zip';ota_only.write_bytes(ota)
    run('dialog-reject-ota','flash.bat',[],1,input_text='',selected_full=ota_only)
    supplied=package/'firmware';supplied.mkdir()
    shutil.copyfile(firmware,supplied/'full.zip')
    second_full=supplied/'second-full.zip';shutil.copyfile(firmware,second_full)
    run('interactive-invalid-firmware-number','flash.bat',[],1,input_text='9\n')
    second_full.unlink()
    run('interactive-zh-invalid-board','flash-zh.bat',[],1,input_text='9\n')
    for bat,board_prompt,confirm_prompt,wait_prompt in [
        ('flash.bat','Select the board','Proceed with complete flashing?','Waiting for'),
        ('flash-zh.bat','请选择主板','是否继续完整刷机','正在等待')]:
        language='zh' if bat=='flash-zh.bat' else 'en'
        for mode in ['probe-late','target-late']:
            name='interactive-'+language+'-'+mode
            r=run(name,bat,[],mode=mode,input_text='1\ny\n')
            assert r.stdout.index('full.zip')<r.stdout.index(board_prompt)<r.stdout.index(confirm_prompt)<r.stdout.index(wait_prompt)
            assert json.loads((e/(name+'-trace.json')).read_text())['flashWrites']==1
            assert not (e/(name+'-dialog.txt')).exists()
        for answer,label in [('n','no'),('','blank')]:
            name='consent-'+language+'-'+label
            r=run(name,bat,[],mode='probe-never',input_text='1\n'+answer+'\n')
            assert confirm_prompt in r.stdout and wait_prompt not in r.stdout
            assert not (e/(name+'-trace.json')).exists(), 'Declining must not enumerate a probe or target'
            assert not (e/(name+'-dialog.txt')).exists()
        if language=='zh':
            assert '完整刷机' in r.stdout and '按任意键关闭窗口' in r.stdout
    r=run('interactive-cancel-wait','flash.bat',[],130,mode='cancel-wait',input_text='1\ny\n')
    assert 'Waiting for' in r.stdout and 'Cancelled; no programming' in r.stdout and 'Exit code: 130.' in r.stdout
    assert json.loads((e/'interactive-cancel-wait-trace.json').read_text())['flashWrites']==0
    for mode in ['multiple-probes','wrong-chip','low-supply','write-fail']:
        r=run('interactive-'+mode,'flash.bat',[],1,mode=mode,input_text='1\ny\n')
        assert 'Press any key to close' in r.stdout and 'Exit code: 1.' in r.stdout
    r=run('automation-zh-dryrun','flash-zh.bat',['-Firmware',firmware,'-Config',config,'-DryRun'])
    assert 'validated-no-probe' in r.stdout and '\u6309\u4efb\u610f\u952e' not in r.stdout and not (e/'automation-zh-dryrun-trace.json').exists()
    assert 'Press any key' not in run('automation-en-dryrun','flash.bat',['-Firmware',firmware,'-Config',config,'-DryRun']).stdout
    run('automation-overrides-interactive','flash.bat',['-Interactive','-Yes','-DryRun','-Firmware',firmware,'-Config',config])
    # Launcher/dependency failures still reach the real BAT's pause and retain
    # the child exit code; test directories contain no vendor executable.
    missing_script=e/'missing launcher dependency';missing_script.mkdir()
    for entry in ['flash.bat','flash-zh.bat']:shutil.copyfile(tools/entry,missing_script/entry)
    (missing_script/'internal').mkdir()
    shutil.copyfile(tools/'internal/initialization-close-zh.txt',missing_script/'internal/initialization-close-zh.txt')
    powershell=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
    missing_code=subprocess.run([str(powershell),'-NoProfile','-STA','-File',str(missing_script/'initialization.ps1')],capture_output=True).returncode
    for entry in ['flash.bat','flash-zh.bat']:
        run('missing-script-'+entry,entry,[],missing_code,input_text='',entry_tools=missing_script)
    for missing in ['native-configs','initialization-dependencies.json']:
        incomplete=e/('missing '+missing)
        shutil.copytree(tools,incomplete,ignore=shutil.ignore_patterns(missing))
        r=run('missing-'+missing,'flash.bat',[],1,input_text='',entry_tools=incomplete)
        assert 'Press any key to close' in r.stdout and not (e/('missing-'+missing+'-trace.json')).exists()
    no_powershell=e/'missing powershell';no_powershell.mkdir()
    (no_powershell/'internal').mkdir()
    shutil.copyfile(tools/'internal/initialization-close-zh.txt',no_powershell/'internal/initialization-close-zh.txt')
    for entry in ['flash.bat','flash-zh.bat']:
        launcher=(tools/entry).read_text(encoding='ascii')
        launcher=launcher.replace('"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"','"'+str(no_powershell/'not-installed.exe')+'"')
        (no_powershell/entry).write_text(launcher,encoding='ascii',newline='\n')
        run('missing-powershell-'+entry,entry,[],9009,input_text='',entry_tools=no_powershell)
    r=run('automation-error-no-pause','flash.bat',['-Firmware',bad_dialog,'-Config',config,'-Yes'],1)
    assert 'Press any key' not in r.stdout
    for mode in ['normal','valid-old','corrupt-old','missing-old','short-old','blank-old','opaque-pro','wrong-chip','connect-fail','target-changed','write-fail','late-write-fail','worker-write-fail','readback-fail']:
        good=mode in ['normal','valid-old','corrupt-old','missing-old','short-old','blank-old','opaque-pro']
        output=e/('run-'+mode)
        options = ['-Firmware',firmware,'-Config',config,'-JLinkExe',native/'JLink.exe',
                   '-Snr','12345','-Output',output,'-Yes']
        if mode == 'connect-fail':
            options.append('-NoWait')
        run('preserve-'+mode,'flash.bat',options,0 if good else 1,mode)
        trace=json.loads((e/('preserve-'+mode+'-trace.json')).read_text())
        assert trace['factoryUnchanged'] and trace['flashWrites']<=1
        if good:
            assert (trace['flashWrites'],trace['externalWrites'],trace['resets'],trace['appStarts'])==(1,5,1,1),trace
            result=json.loads((output/'result.json').read_text());assert result['readbackMatched'] and not result['pythonRequired']
            core=CoreState()
            for call in trace['calls']:
                if 'connect' in call['script']:
                    core.begin()
                    for line in call['script']:core.observe(line)
            assert (core.implicit_go,core.app_starts,core.resets,core.interrupted_refresh)==(0,1,1,0)
        else:
            assert trace['resets']==trace['appStarts']==0
            assert (output/'failure.json').is_file() and not (output/'result.json').exists()
            failure=json.loads((output/'failure.json').read_text())
            if mode=='readback-fail':assert failure['programmingSucceeded'] and not failure['readbackMatched']
    run('clean-direct','flash-hex.bat',['-Materials',materials,'-JLinkExe',native/'JLink.exe','-Snr','12345','-Output',e/'clean run','-Yes'])
    for name, args in [
        ('flash.bat', ['-Firmware',firmware,'-Config',config]),
        ('flash-hex.bat', ['-Materials',materials]),
    ]:
        label = 'automated-target-late-' + name
        run(label,name,[*args,'-JLinkExe',native/'JLink.exe','-Snr','12345',
                        '-Output',e/label,'-Yes'],mode='target-late')
        trace = json.loads((e/(label+'-trace.json')).read_text())
        assert trace['flashWrites'] == 1 and trace['resets'] == 1
        assert sum(call['label'] == 'chip-info' for call in trace['calls']) == 3

    assert not any('old-' in c['label'] for c in json.loads((e/'clean-direct-trace.json').read_text())['calls'])
    for name,change in [('protected-pin',('P0.20','P0.00')),('wrong-board',('EL022HWRL','UNKNOWN'))]:
        badconfig=e/(name+'.toml');badconfig.write_text(config.read_text().replace(*change))
        run('reject-'+name,'generate-hex.bat',['-Firmware',firmware,'-Config',badconfig,'-Output',e/(name+'-output')],1)
        assert not (e/('reject-'+name+'-trace.json')).exists()
    appfile=e/'app-only.bin';appfile.write_bytes(app)
    run('reject-app-as-full','generate-hex.bat',['-Firmware',appfile,'-Config',config,'-Output',e/'app-only-output'],1)
    for kind in ['rebound-bytes','factory-address']:
        altered=e/('tampered '+kind);shutil.copytree(materials,altered)
        am=json.loads((altered/'addresses.json').read_text())
        if kind=='rebound-bytes':
            blob=bytearray((altered/'internal.hex').read_bytes());blob[16]^=1;(altered/'internal.hex').write_bytes(blob)
            am['files']['internal.hex']['sha256']=hashlib.sha256(blob).hexdigest()
        else:am['segments'][0]['address']=0x10001000
        (altered/'addresses.json').write_text(json.dumps(am))
        run('reject-'+kind,'flash-hex.bat',['-Materials',altered,'-JLinkExe',native/'JLink.exe','-Snr','12345','-Output',e/('never-'+kind),'-Yes'],1)
        assert not (e/('reject-'+kind+'-trace.json')).exists()
    broken=e/'broken native package';shutil.copytree(package,broken)
    with (broken/'tools/internal/initialization-format.ps1').open('ab') as stream:stream.write(b'\n# corrupted fixture\n')
    run('reject-broken-tools','flash-hex.bat',['-Materials',materials,'-Yes'],1,entry_tools=broken/'tools')
    assert not (e/'reject-broken-tools-trace.json').exists()
    # Rebind a bad signature into an otherwise valid self-built archive: it
    # must reach signature verification and may not fall back to unsigned.
    with zipfile.ZipFile(io.BytesIO(ota)) as z:of={n:z.read(n) for n in z.namelist()}
    app_manifest=json.loads(of['manifest.json'])['manifest']['application'];datname=app_manifest['dat_file']
    packet=codec.fields(of[datname]);command=packet[1]
    badsigned=packer.fields([(2,packer.fields([(1,command),(2,0),(3,bytes(64))]))])
    of[datname]=badsigned
    def archive(files):
        target=io.BytesIO()
        with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as z:
            for n,b in files.items():z.writestr(n,b)
        return target.getvalue()
    with zipfile.ZipFile(firmware) as z:bf={n:z.read(n) for n in z.namelist()}
    bf['app-ota.zip']=archive(of);bm=json.loads(bf['base.json']);bm['images']['app-ota.zip']={'bytes':len(bf['app-ota.zip']),'sha256':hashlib.sha256(bf['app-ota.zip']).hexdigest()};bf['base.json']=json.dumps(bm).encode()
    bad=e/'invalid supplied signature.zip';bad.write_bytes(archive(bf))
    run('reject-present-bad-signature','generate-hex.bat',['-Firmware',bad,'-Config',config,'-Output',e/'bad-signature-output'],1)
    summary={'status':'passed','cases':len(rows),'realBATandPowerShell51':True,'runtimePythonPATHDisabled':True,'realDeviceOperations':0,'productionSigning':0,'legacyPythonOracleByteEquality':True,'steps':rows}
    (e/'RESULT.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({k:v for k,v in summary.items() if k!='steps'}))

if __name__=='__main__':main()
