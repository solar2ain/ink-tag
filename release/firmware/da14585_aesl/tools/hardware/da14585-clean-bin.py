"""DA initialization material and guarded programming; no CLI/BLE dependency."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import subprocess

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('inktag_da_full_core', HERE/'da14585-full-transaction.py')
core = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = core
spec.loader.exec_module(core)


def digest(data): return hashlib.sha256(data).hexdigest()


def check_tool_closure():
    root=HERE.parents[1]
    manifest=root/'tool-manifest.json'
    if not manifest.exists():
        if not (root/'tools/build/source-files.json').is_file():
            raise ValueError('Programmer manifest missing; no source-checkout fallback')
        return
    value=json.loads(manifest.read_text(encoding='utf-8'))
    if (value.get('format')!='DA-programmer-tools-v1' or value.get('kind')!='tools-only' or
            value.get('buildTarget')!='da14585_aesl' or value.get('firmwareIncluded') is not False):
        raise ValueError('Wrong programmer manifest')
    for name,entry in value['files'].items():
        relative=Path(name)
        if relative.is_absolute() or '..' in relative.parts or ':' in name or '\\' in name:
            raise ValueError('Unsafe programmer manifest path')
        data=core.plain(root/relative).read_bytes()
        if len(data)!=entry['bytes'] or digest(data)!=entry['sha256']:
            raise ValueError('Programmer dependency changed: '+name)


def generate(firmware, config, output):
    review = core.validate_inputs(firmware, config, require_target_defaults=False)
    image = bytearray(core.prepare_image(review))
    # Generic material must contain no current-unit address or factory data,
    # even when the supplied valid Full previously belonged to one device.
    image[0x3800c:0x39000] = b'\xff' * (0x1000-12)
    image[0x39000:0x3b000] = b'\xff' * 8192
    image = bytes(image)
    output = core.plain(output)
    if output.exists(): raise ValueError('Output already exists; no overwrite')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.da-init-', dir=output.parent) as temp:
        stage = Path(temp)/'material'
        stage.mkdir()
        (stage/'initialization-template.bin').write_bytes(image)
        (stage/'config.toml').write_bytes(review['config'])
        segments = []
        for low, high in core.WRITE_RANGES:
            name = f'spi-{low:06x}.bin'
            data = image[low:high]
            (stage/name).write_bytes(data)
            segments.append(dict(path=name, address=low, bytes=len(data), sha256=digest(data)))
        files = {p.name:dict(bytes=p.stat().st_size,sha256=digest(p.read_bytes())) for p in stage.iterdir()}
        manifest = dict(format='DA-initialization-v1', addressSpace='external-spi-flash',
            firmwareFamily='da14585_aesl', layout='DAV1', windowBytes=core.FLASH_BYTES,
            segments=segments, skipRanges=list(core.PROTECTED), files=files,
            sourceFullSha256=review['firmware_sha'], configuration=review['plan'],
            targetIdentityIncluded=False, proApplicable=False, physicalWriterQualified=False,
            addressOnFirstBoot='hardware public address, otherwise persisted SDK static random',
            externalDependencies=['licensed pinned DA SRAM helper', 'SEGGER J-Link 7.88j'])
        (stage/'addresses.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
        stage.rename(output)
    return manifest


def material_inputs(directory):
    directory = core.plain(directory)
    manifest = json.loads((directory/'addresses.json').read_text(encoding='utf-8'))
    if (manifest.get('format') != 'DA-initialization-v1' or
            manifest.get('addressSpace') != 'external-spi-flash' or
            manifest.get('skipRanges') != [[0x39000,0x3b000]] or
            manifest.get('targetIdentityIncluded') is not False):
        raise ValueError('Wrong initialization material/skip ranges')
    names = {'initialization-template.bin','config.toml','spi-000000.bin','spi-03b000.bin'}
    if set(manifest['files']) != names: raise ValueError('Unexpected material files')
    for name, entry in manifest['files'].items():
        data = core.plain(directory/name).read_bytes()
        if len(data) != entry['bytes'] or digest(data) != entry['sha256']:
            raise ValueError('Initialization material changed: '+name)
    image = (directory/'initialization-template.bin').read_bytes()
    if len(manifest['segments']) != 2: raise ValueError('Wrong segment count')
    for segment,(low,high),name in zip(manifest['segments'],core.WRITE_RANGES,
            ('spi-000000.bin','spi-03b000.bin')):
        if (segment != dict(path=name,address=low,**manifest['files'][name]) or
                (directory/name).read_bytes() != image[low:high]):
            raise ValueError('Segment differs from exact writable map/template')
    return directory/'initialization-template.bin',directory/'config.toml'


def choose_config(language='en', strict=False):
    load=core.production_validators(require_target_defaults=False)[1]
    presets=[]
    for path in sorted((HERE.parent/'programmer/configs').glob('*.toml'),key=lambda p:p.stem):
        plan=load(path,'DA14585')
        presets.append(path)
        screen=plan['screen']
        print(f"{len(presets)}. {plan['board_marking']} / {plan['product']} / "
              f"{screen['ic']} {screen['width']}x{screen['height']} {screen['color']}, recipe {screen['recipe']}")
    if not presets:raise ValueError('没有找到随附主板配置。' if language=='zh' else 'No supported bundled board configurations')
    print('型号别名用于识别配置，请核对实际主板和屏幕。' if language=='zh' else 'Model aliases identify the preset; verify the actual board and panel.')
    while True:
        choice=input('选择主板／屏幕编号（空输入取消）：' if language=='zh' else 'Select board/panel number (empty cancels): ').strip()
        if not choice:return None
        if choice.isdecimal() and 1<=int(choice)<=len(presets):return presets[int(choice)-1]
        if strict:raise ValueError('主板编号无效，请重新启动并选择列出的编号。' if language=='zh' else 'Invalid board number; restart and select a listed number.')
        print('Select one of the listed numbers.')


def choose_firmware(language):
    entry=HERE.parent/'programmer'
    folders=(entry,entry/'firmware',HERE.parents[1]/'firmware',HERE.parents[1]/'full')
    verify=core.production_validators(require_target_defaults=False)[0]
    candidates=[]
    for path in sorted({p for folder in folders for pattern in ('*.bin','*.img') for p in folder.glob(pattern) if p.is_file()}):
        try:verify(core.plain(path).read_bytes())
        except ValueError:continue
        candidates.append(path)
    if len(candidates)==1:
        print(('自动使用完整 Full 固件：' if language=='zh' else 'Using Full firmware: ')+str(candidates[0]),flush=True)
        return candidates[0]
    if candidates:
        for number,path in enumerate(candidates,1):print(f'{number}. {path}')
        value=input('选择完整 Full 固件编号（空输入取消）：' if language=='zh' else 'Select Full firmware number (empty cancels): ').strip()
        if not value:return None
        if not value.isdecimal() or not 1<=int(value)<=len(candidates):raise ValueError('固件编号无效。' if language=='zh' else 'Invalid firmware number')
        return candidates[int(value)-1]
    print('没有有效的随附 Full，请在文件对话框选择完整 Full。' if language=='zh' else 'No valid adjacent Full; select a complete Full in the file dialog.',flush=True)
    title='DA14585 完整 Full 固件' if language=='zh' else 'DA14585 Full firmware'
    command="[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false); Add-Type -AssemblyName System.Windows.Forms; $d=New-Object Windows.Forms.OpenFileDialog; $d.Title='"+title+"'; $d.Filter='Full firmware (*.bin)|*.bin|All files (*.*)|*.*'; if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.FileName)}; $d.Dispose()"
    result=subprocess.run(['powershell.exe','-NoLogo','-NoProfile','-STA','-Command',command],capture_output=True,encoding='utf-8')
    if result.returncode:raise ValueError('Windows file picker failed: '+result.stderr.strip())
    return Path(result.stdout.strip()) if result.stdout.strip() else None


def prompt_path(message,language='en'):
    value=input(message+('（空输入取消）：' if language=='zh' else ' (empty cancels): ')).strip().strip('"')
    return Path(value) if value else None


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action',required=True,choices=['generate','flash-bin','flash'],
        help='flash: normal/recommended installation with current-unit preservation and missing-record defaults')
    parser.add_argument('--firmware','-Firmware',type=Path)
    parser.add_argument('--config','-Config',type=Path)
    parser.add_argument('--input','-Input',type=Path)
    parser.add_argument('--output','-Output',type=Path)
    parser.add_argument('--helper','-Helper',type=Path,help='Externally acquired fixed SRAM helper; never bundled/downloaded')
    parser.add_argument('--jlink-exe','-JLinkExe',default='')
    parser.add_argument('--serial','-Serial',default='')
    parser.add_argument('--yes','-Yes',action='store_true')
    parser.add_argument('--dry-run','-DryRun',action='store_true')
    parser.add_argument('-Help',action='help')
    parser.add_argument('--language',choices=['en','zh'],default='en')
    parser.add_argument('--guided',action='store_true',help='Interactive Explorer launch; Full selection precedes board selection')
    args=parser.parse_args(argv)
    try:
        check_tool_closure()
        if args.guided:
            print(('DA14585 完整刷机：连接 J-Link 的 SWDIO、SWCLK、GND，按板卡要求供电。\n需要 Python 3.12+、SEGGER J-Link 7.88j 和已许可的固定 SRAM helper。\n流程：选择 Full → 主板 → 确认清理范围（默认否）→ 等待设备 → 校验身份、写入、回读。\n同意前不等待或探测设备；同意后持续等待明确未连接的设备，Ctrl+C 取消。只能使用完整 DAV1 Full，不能用单独 App 或 OTA；空输入取消选择。' if args.language=='zh' else
                'DA14585 Full installation: connect J-Link SWDIO, SWCLK and GND; power the board as specified.\nRequires Python 3.12+, SEGGER J-Link 7.88j and the licensed pinned SRAM helper.\nFlow: Full firmware -> board -> cleanup confirmation (default No) -> wait for device -> identity checks/write/read back.\nNo device waiting or discovery before consent. After consent, known-absent discovery waits indefinitely; Ctrl+C cancels. Use a complete DAV1 Full, never a standalone App or OTA; empty input cancels selection.'),flush=True)
            if not args.firmware:
                args.firmware=choose_firmware(args.language)
                if args.firmware is None:
                    print(json.dumps(dict(status='cancelled',deviceAccess=False)));return 0
            # Reject App/OTA/corruption before asking for a board. The normal
            # combined verification still runs after selecting the configuration.
            core.production_validators(require_target_defaults=False)[0](core.plain(args.firmware).read_bytes())
            if not args.config:
                args.config=choose_config(args.language,strict=True)
                if args.config is None:
                    print(json.dumps(dict(status='cancelled',deviceAccess=False)));return 0
        required=(['input','output'] if args.action=='flash-bin' else ['config','firmware','output'])
        missing=any(not getattr(args,name) for name in required)
        if missing and args.yes:raise ValueError('Automated mode requires explicit '+', '.join(required))
        interactive=args.guided or missing or (args.action!='generate' and not args.dry_run and not args.helper and
            not args.yes and sys.stdin.isatty())
        # Normal installation asks for the verified preset before the Full.
        # Generation starts with its source Full; flash-bin uses its stored plan.
        order=(['firmware','config','output'] if args.action=='generate' else required)
        labels=({'firmware':'完整 Full 固件路径','input':'初始化物料目录','output':'新的输出／事务日志目录'} if args.language=='zh' else
                {'firmware':'DA14585 Full firmware path','input':'Initialization material directory','output':'New output / transaction directory'})
        for name in order:
            if getattr(args,name):continue
            value=choose_config(args.language) if name=='config' else prompt_path(labels[name],args.language)
            if value is None:
                print(json.dumps(dict(status='cancelled',success=False,deviceAccess=False)));return 0
            setattr(args,name,value)
        if interactive and args.action!='generate' and not args.dry_run and not args.helper:
            args.helper=prompt_path('外部已许可 SRAM helper 路径' if args.language=='zh' else 'External licensed SRAM helper path',args.language)
            if args.helper is None:
                print(json.dumps(dict(status='cancelled',success=False,deviceAccess=False)));return 0
        if not args.output:raise ValueError('Specify -Output <new directory>')
        if args.action=='generate':
            if not args.firmware or not args.config: raise ValueError('Firmware and config are required')
            result=generate(args.firmware,args.config,args.output)
        else:
            firmware,config=(material_inputs(args.input) if args.action=='flash-bin' and args.input else
                (args.firmware,args.config))
            if not firmware or not config: raise ValueError('Select material input or explicit Full/config')
            review=core.validate_inputs(firmware,config,require_target_defaults=False)
            if interactive and args.action=='flash':
                print('Board/panel: '+review['plan']['board_marking']+' / '+review['plan']['screen']['ic'])
                print('Full firmware: '+str(firmware.resolve()))
                print('Keep available current-unit address and protected factory/OTP; missing optional records use defaults.')
                print('Clear ordinary settings/time and image slots in the configured 256 KiB window.')
            if args.dry_run:
                result=dict(status='validated',deviceAccess=False,writeRanges=core.WRITE_RANGES,
                    skipRanges=core.PROTECTED,physicalWriterQualified=False,sourceSha256=review['firmware_sha'])
            else:
                if not args.helper: raise ValueError('External licensed SRAM helper required; no device accessed')
                # Exact maintained backend with the same pinned helper hash,
                # power/range/probe/ACK/no-replay/independent-readback checks.
                options=dict(retry_detect=True,readiness_timeout=0,interval=1,
                             emit=lambda text: print(text,flush=True))
                if args.guided:
                    def confirm(warning):
                        if args.language=='zh':
                            print(warning,flush=True)
                            return input('完整刷写会清空设置、时间和图片，保留工厂／OTP保护区。确认继续？[y/N] ')
                        return input(warning)
                    options.update(confirm=confirm,
                                   emit=lambda text: print(('状态：' if args.language=='zh' else '')+text,flush=True))
                result=core.run_full(firmware,config,args.output,
                    lambda:core.production_backend(args.serial,helper_path=core.plain(args.helper),
                        jlink_exe=args.jlink_exe,evidence_root=args.output/'commander-readback'),
                    yes=args.yes,serial=args.serial,**options)
                if not result['success'] and result['status']!='cancelled':
                    print(json.dumps(result));return 1
        print(json.dumps(result));return 0
    except KeyboardInterrupt:
        print('\n已取消。' if args.language=='zh' else '\nCancelled.',flush=True)
        return 130
    except Exception as error:
        if args.language=='zh':print('操作失败，具体原因：',file=sys.stderr)
        print(json.dumps(dict(success=False,error=str(error),noReplay=True)),file=sys.stderr)
        return 1


if __name__=='__main__':raise SystemExit(main())
