"""Segmented stock-read continuation; invokes the maintained SWS reader only.

No serial/BLE implementation, writes to targets, capture repair or write replay.
Old evidence stays immutable. A new receipt distinguishes segmented reads.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
from datetime import datetime, timezone

from tlsr_backup_verify import GEOMETRY, bound, text

SCHEMA = 'inktag-tlsr-backup-resume-v1'
BLOCK = 256
SEGMENT = 65536


def safe(path):
    path = Path(path).absolute()
    if '..' in path.parts or any(p.is_symlink() or (p.exists() and
        getattr(p.lstat(),'st_file_attributes',0) & 0x400) for p in (path, *path.parents)):
        raise ValueError(f'Unsafe linked/traversal resume path: {path}')
    return path


def load(path):
    return json.loads(text(safe(path)))


def check_binding(item):
    path = safe(item['path'])
    if bound(path) != item:
        raise ValueError('Preserved resume input changed; no device access.')
    return path


def descriptor(path):
    return bound(safe(path)) if path and Path(path).is_file() else None


def find_request(root, *, chip, pcb, note, port, verify_only=False):
    if not re.fullmatch(r'COM[1-9][0-9]*', port, re.I) or pcb == 'UNIDENTIFIED-PCB':
        raise ValueError('Resume requires an exact Port and PCB marking.')
    consoles = []
    for parent in (root / '.local/backups' / chip / pcb, root / '.local/backup-archive' / chip):
        if parent.exists():
            consoles.extend(parent.rglob('console.log'))
    originals, continuations = [], []
    for console in consoles:
        safe(console)
        events_path = console.parent / 'events.jsonl'
        if not events_path.exists():
            continue
        events = [json.loads(s) for s in text(events_path).splitlines() if s.strip()]
        if not events:
            continue
        meta = events[0]
        if (meta.get('schema') != 'inktag-flash-workflow-v1' or meta.get('action') != 'backup' or
                meta.get('pcb_silkscreen') != pcb or meta.get('board_note', '') != note):
            continue
        link = meta.get('verification_of') or meta.get('resumption_of')
        if link:
            continuations.append((console.stat().st_mtime_ns, console.parent, meta, link))
            continue
        commands = [e['command'] for e in events if e.get('phase') == 'backup' and
                    e.get('message') == 'Starting maintained tool' and isinstance(e.get('command'), list)]
        if len(commands) != 1:
            raise ValueError('Matching capture has ambiguous original command; no older fallback.')
        cmd = commands[0]
        def value(key):
            return cmd[cmd.index(key)+1] if key in cmd else None
        if value('-Chip') == chip and (value('-Port') or '').upper() == port.upper():
            originals.append((console.stat().st_mtime_ns, console.parent, meta))
    if not originals:
        raise ValueError('No matching interrupted backup found; Resume will not start a new first read.')
    _, original, original_meta = max(originals, key=lambda x: x[0])
    candidates = [(0, original, original_meta)] + [(stamp, unit, meta) for stamp, unit, meta, link in continuations
        if safe(link) == safe(original)]
    _, unit, meta = max(candidates, key=lambda x: x[0])
    result_path = unit / 'result.json'
    if not result_path.exists():
        raise ValueError('Latest capture has not exited cleanly; stop its process before Resume. No older fallback.')
    result = load(result_path)
    if any(result.get(k) != meta.get(k) for k in ('schema','board','action','unit','pcb_silkscreen','board_note')):
        raise ValueError('Capture result identity differs from events.')
    if result.get('status') != 'failed' or not result.get('ended_utc'):
        raise ValueError('Resume selects an exited interrupted/failed capture only, not a completed backup.')
    details = result.get('failure_details') or {}
    if details.get('decoded_mismatches', 0) or details.get('kind') in ('readback-data-mismatch','readback-mixed-failure'):
        raise ValueError('Data mismatch cannot be resumed or skipped; preserve both captures and investigate.')
    for diagnostic in (unit / 'backup').glob('*.mismatch.json'):
        if load(diagnostic).get('decodedMismatchCount', 0):
            raise ValueError('Decoded mismatch evidence forbids Resume.')
    geometry_path = original / 'backup/jedec-identification.log'
    geometry = text(geometry_path)
    m = re.search(r'Flash JEDEC samples:\s+([0-9A-F]{6})\s+\1\s+\1\s*(?:\r?\n|$)', geometry)
    if not m or m[1] not in GEOMETRY:
        raise ValueError('Original geometry lacks three admitted matching JEDEC samples.')
    for reg in (1,2):
        if not re.search(rf'Flash status-{reg} samples:\s+(0x[0-9A-Fa-f]{{2}})\s+\1\s+\1\s*(?:\r?\n|$)', geometry):
            raise ValueError('Original status samples missing/inconsistent.')
    size, jedec = GEOMETRY[m[1]], m[1]
    first = original / 'backup/tlsr8359-flash.bin'
    second = unit / 'backup/tlsr8359-flash-verify.bin'
    evidence = [bound(result_path), bound(unit/'events.jsonl'), bound(geometry_path)]
    if meta.get('resumption_of'):
        state = load(unit/'backup/resume-state.json')
        request = load(unit/'resume-request.json')
        if (state['originalUnit'] != str(original.resolve()) or request['chip'] != chip or request['port'] != port.upper()
                or state.get('dataMismatch') or state['requestSha256'] != bound(unit/'resume-request.json')['sha256']):
            raise ValueError('Continuation state/identity mismatch.')
        for item in request['evidence'] + request.get('readerInputs',[]) + [i for i in (request['first'],request['second']) if i]:
            check_binding(item)
        first = check_binding(state['first'])
        second = check_binding(state['second']) if state['second'] else None
        evidence += [bound(unit/'resume-request.json'), bound(unit/'backup/resume-state.json')]
    elif meta.get('verification_of'):
        request = load(unit/'verification-request.json')
        if request['chip'] != chip or request['port'] != port.upper():
            raise ValueError('Verification identity mismatch.')
        first = check_binding(request['primary'])
        for item in request['evidence']:
            check_binding(item)
        evidence += [bound(unit/'verification-request.json')]
    first_item, second_item = descriptor(first), descriptor(second)
    for item in (first_item, second_item):
        if item and (item['bytes'] > size or item['bytes'] % BLOCK):
            raise ValueError('Capture prefix is oversized or not a complete 256-byte block; no truncation/repair.')
    if not first_item or not first_item['bytes']:
        raise ValueError('No complete first-read blocks to resume.')
    if first_item['bytes'] < size:
        if verify_only or (second_item and second_item['bytes']):
            raise ValueError('First read is incomplete; Resume without VerifyOnly must finish it first.')
    elif second_item and second_item['bytes']:
        if Path(second_item['path']).read_bytes() != Path(first_item['path']).read_bytes()[:second_item['bytes']]:
            raise ValueError('Preserved second-read prefix differs; no repair/resume.')
    if first_item['bytes'] == size and second_item and second_item['bytes'] == size:
        raise ValueError('Both reads are already full; inspect the previous failure, do not replay hardware.')
    return dict(schema=SCHEMA, chip=chip, pcb=pcb, boardNote=note, port=port.upper(),
        originalUnit=str(original.resolve()), previousUnit=str(unit.resolve()), sizeBytes=size,
        flashJedec=jedec, first=first_item, second=second_item, evidence=evidence,
        sameBoardConfirmationRequired=True, segmented=True, flashWrites=False)


def save_new(path, data):
    safe(path)
    with path.open('xb') as f:
        f.write(data)
        f.flush()
        import os
        os.fsync(f.fileno())


def assert_no_competitor(shell, port):
    # Process inspection only; never opens a serial port or terminates a process.
    query = 'Get-CimInstance Win32_Process | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress'
    p = subprocess.run([shell,'-NoProfile','-Command',query], capture_output=True, text=True, timeout=20)
    if p.returncode:
        raise ValueError('Cannot inspect competing hardware processes; no Resume access.')
    records = json.loads(p.stdout or '[]')
    if isinstance(records,dict):
        records = [records]
    for row in records:
        command = row.get('CommandLine') or ''
        import os
        if row.get('ProcessId') in (os.getpid(),os.getppid()):
            continue
        if re.search(r'TLSR825xComFlasher|tlsr825x-early-activate|invoke-tlsr8359-uart-sws|backup-operation|backup-verify-operation|flash-workflow\.py|[\\/]backup\.ps1',command,re.I):
            ports = re.findall(r'\bCOM[1-9][0-9]*\b',command,re.I)
            if not ports or port.upper() in [v.upper() for v in ports]:
                raise ValueError('Target/unknown SWS tool is still running; do not Resume concurrently.')


def run_resume(args, board, workflow, *, run=None, read=input, compete=None):
    request = find_request(workflow.primary_root(workflow.ROOT), chip=args.chip,
        pcb=workflow.backup_pcb_label(args,board), note=args.board_note, port=args.port,
        verify_only=args.verify_only)
    if args.plan:
        print(json.dumps(dict(request, hardware_started=False, resume_parameter='-Resume',
            native_linux_hardware_qualified=False, overlapBytes=4096, segmentBytes=SEGMENT),indent=2))
        return 0
    if sys.platform != 'win32' and run is None:
        raise ValueError('Native Linux Resume is not qualified; use --dry-run.')
    compete = compete or assert_no_competitor
    compete(args.powershell,request['port'])
    reply = read(f"Resume {request['pcb']} / {request['boardNote']} on {request['port']}. "
                 "Confirm the same physical board is still connected and Flash was not written. [y/N]: ")
    if reply.strip().lower() != 'y':
        raise ValueError('Same-board confirmation missing; no hardware started.')
    for item in request['evidence'] + [i for i in (request['first'],request['second']) if i]:
        check_binding(item)
    parent = Path(args.factory_session_dir) if args.factory_session_dir else workflow.primary_root(workflow.ROOT)/'.local/backup-archive'/args.chip/'pending'
    unit = safe(parent/f'resume-only-{datetime.now(timezone.utc):%Y%m%dT%H%M%S}-{uuid.uuid4().hex[:8]}'/'unit-0001')
    class ResumeLog(workflow.Log):
        progress_base = 0
        progress_role = 'boundary'
        role_started = None
        def tool_line(self,phase,line):
            m = workflow.BYTE_PROGRESS.search(workflow.ANSI.sub('',line))
            if m and self.progress_role != 'boundary':
                self.progress_started[phase] = self.role_started
                done = self.progress_base + int(float(m[1])*1024)
                self.emit(phase,f'Resume {self.progress_role}: confirmed {done/1024:.2f}/{request["sizeBytes"]/1024:.2f} KiB',
                    level='progress',percent=done*100/request['sizeBytes'],completed_kib=done/1024,total_kib=request['sizeBytes']/1024)
            else:
                super().tool_line(phase,line)
    log = ResumeLog(unit,board,'backup',1,verbose=args.verbose,target_label=request['pcb'],console_output=args.console_output)
    log.common.update(pcb_silkscreen=request['pcb'],board_note=request['boardNote'],resumption_of=request['originalUnit'])
    helper = workflow.ROOT/'firmware/ports/telink/tlsr8359/tools/internal/invoke-tlsr8359-uart-sws.ps1'
    request['readerInputs']=[bound(helper),bound(helper.with_name('tlsr825x-early-activate.py')),
                            bound(helper.with_name('tlsr8359-uart-sws-common.ps1'))]
    save_new(unit/'resume-request.json',(json.dumps(request,indent=2)+'\n').encode())
    output = unit/'backup'; output.mkdir()
    first = Path(request['first']['path']).read_bytes()
    second = Path(request['second']['path']).read_bytes() if request['second'] else b''
    segments, status, mismatch = [], 'failed', False
    runner = run or workflow.run_command
    def capture(label,start,count,expected=None):
        compete(args.powershell,request['port'])
        for item in request['readerInputs']:check_binding(item)
        name = output/f'{label}.bin'
        cmd = [args.powershell,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(helper),
            '-Operation','read','-Port',request['port'],'-Address',str(start),'-SizeBytes',str(count),
            '-File',str(name),'-ReportFlashJedec','-ExpectedFlashJedec',request['flashJedec'],
            '-Baud',str(args.backup_baud or 921600),'-ActivationBaud',str(args.backup_activation_baud),
            '-ActivationMs',str(args.backup_activation_ms),'-ClockMHz',str(args.backup_clock_mhz),'-Python',sys.executable]
        if args.tool_path: cmd += ['-ToolPath',args.tool_path]
        if label.startswith('second-') and start+count==request['sizeBytes']:
            cmd += ['-Run'] # Only the final second-read segment requests App return.
        if not args.no_wait: cmd += ['-RetryDetect','-DetectTimeoutSeconds',str(args.timeout_seconds),'-DetectIntervalSeconds',str(args.interval_seconds)]
        if expected is None:
            cmd += ['-SinglePassRead']
        else:
            reference = output/f'{label}-reference.bin'
            save_new(reference,expected)
            cmd += ['-ExpectedReadFile',str(reference)]
        workflow.check_command(cmd)
        result = runner(cmd,log,'backup',len(segments)+1,operation_timeout=args.operation_timeout_seconds)
        details = workflow.tlsr_read_failure(result.text)
        data = name.read_bytes() if name.exists() else b''
        if len(data)>count or len(data)%BLOCK or (expected is not None and data != expected[:len(data)]):
            raise ValueError('Segment has invalid length/data; no repair.')
        raw_path=unit/f'backup-{len(segments)+1:03d}.raw.log'
        native_exit=result.code
        if not result.code and (len(data)!=count or not raw_path.is_file() or
            not re.search(r'(?m)^Done!\s*$',text(raw_path)) or
            f'Observed Flash JEDEC: {request["flashJedec"]}' not in text(raw_path)):
            result=workflow.Result(1,result.text+'\nMissing native completion/geometry; preserved, not passed.')
        segments.append(dict(label=label,address=start,requestedBytes=count,read=descriptor(name),command=cmd,
            rawLog=descriptor(raw_path),exit=result.code,nativeExit=native_exit,
            complete=result.code==0 and len(data)==count, failure=details))
        return result,data,details
    try:
        log.emit('resume','Same-board human confirmation recorded. Old captures remain immutable; segmented double-read.',level='notice')
        # Re-read immutable start and boundary before trusting an old prefix.
        log.progress_role = 'boundary'
        anchors = [('first-anchor',0,first[:min(256,len(first))])]
        prefix = second or first
        if len(prefix)>256:
            start = max(0,len(prefix)-4096)
            anchors.append(('boundary-anchor',start,prefix[start:]))
        for label,start,expected in anchors:
            result,data,details = capture(label,start,len(expected),expected)
            if result.code or data != expected:
                mismatch = bool(details and details['decoded_mismatches'])
                raise ValueError('Resume anchor failed; no tail read or automatic recovery.')
        for role in ('first','second'):
            data = first if role=='first' else second
            log.role_started=time.monotonic()
            while len(data)<request['sizeBytes']:
                start=len(data); count=min(SEGMENT,request['sizeBytes']-start)
                log.progress_base=start; log.progress_role=role
                result,chunk,details = capture(f'{role}-{start:06x}',start,count,
                    None if role=='first' else first[start:start+count])
                data += chunk
                if role=='first': first=data
                else: second=data
                if result.code or len(chunk)!=count:
                    mismatch = bool(details and details['decoded_mismatches'])
                    raise ValueError('Resume read stopped; complete blocks retained. No automatic replay.')
            if role=='first': first=data
            else: second=data
        if first != second:
            mismatch=True
            raise ValueError('Final independent segmented captures differ; no archive.')
        for item in request['evidence']+request['readerInputs']+[i for i in (request['first'],request['second']) if i]:
            check_binding(item)
        # Analyze the complete assembled first image, never its old partial prefix.
        assembled=output/'analysis-complete-first.bin'
        save_new(assembled,first)
        analysis=subprocess.run([sys.executable,str(workflow.ROOT/'tools/hardware/analyze-tlsr8359-backup.py'),
            str(assembled),'--base-address','0','--output',str(output/'analysis.txt')],capture_output=True,text=True)
        if analysis.returncode or not (output/'analysis.txt').is_file():
            raise ValueError('Offline complete-image analysis failed; no archive.')
        status='tool-success'
        log.emit('complete','MATCHED segmented independent reads. Not exact-board/panel/release qualification.',level='result')
        return 0
    except (ValueError,OSError,KeyboardInterrupt,EOFError) as exc:
        log.emit('failed',str(exc) or 'Interrupted; evidence preserved.',level='error')
        return 130 if isinstance(exc,KeyboardInterrupt) else 1
    finally:
        first_path=output/'tlsr8359-flash.bin'; second_path=output/'tlsr8359-flash-verify.bin'
        save_new(first_path,first)
        if second: save_new(second_path,second)
        state=dict(schema=SCHEMA,originalUnit=request['originalUnit'],first=bound(first_path),
            second=descriptor(second_path),dataMismatch=mismatch,segments=segments,
            requestSha256=bound(unit/'resume-request.json')['sha256'],sameBoardConfirmed=True,
            status='MATCHED-SEGMENTED-DOUBLE-READ' if status=='tool-success' else 'PARTIAL',
            segmented=True,flashWrites=False,finishedUtc=workflow.utc())
        save_new(output/'resume-state.json',(json.dumps(state,indent=2)+'\n').encode())
        report=dict(log.common,status=status,ended_utc=workflow.utc(),device_attested=False,firmware_built=False)
        if mismatch: report['failure_details']=dict(kind='readback-data-mismatch',decoded_mismatches=1)
        save_new(unit/'result.json',(json.dumps(report,indent=2)+'\n').encode())
        log.close()


def validate_complete(unit):
    """Offline archive gate; every fresh segment and every old bound input."""
    unit=safe(unit)
    request=load(unit/'resume-request.json'); state=load(unit/'backup/resume-state.json')
    result=load(unit/'result.json')
    if (request['schema'] != SCHEMA or state['schema'] != SCHEMA or result['status']!='tool-success' or
            state['status']!='MATCHED-SEGMENTED-DOUBLE-READ' or state['dataMismatch'] or not state['sameBoardConfirmed'] or
            state['requestSha256']!=bound(unit/'resume-request.json')['sha256']):
        raise ValueError('Segmented continuation is incomplete/unbound.')
    for item in request['evidence']+request['readerInputs']+[i for i in (request['first'],request['second']) if i]: check_binding(item)
    first=check_binding(state['first']); second=check_binding(state['second'])
    if state['first']['bytes']!=request['sizeBytes'] or state['second']['bytes']!=request['sizeBytes'] or first==second or state['first']['sha256']!=state['second']['sha256']:
        raise ValueError('Final segmented independent lengths/hashes differ.')
    for segment in state['segments']:
        check_binding(segment['read'])
        raw=check_binding(segment['rawLog'])
        content=text(raw)
        cmd=segment['command']
        def option(name):return cmd[[i for i,v in enumerate(cmd) if v==name][-1]+1]
        if (not segment['complete'] or segment['exit'] or segment['failure'] or
            segment['read']['bytes']!=segment['requestedBytes'] or option('-Operation')!='read' or
            int(option('-Address'))!=segment['address'] or int(option('-SizeBytes'))!=segment['requestedBytes'] or
            option('-Port')!=request['port'] or option('-ExpectedFlashJedec')!=request['flashJedec'] or
            safe(option('-File'))!=safe(segment['read']['path']) or
            not re.search(r'(?m)^Done!\s*$',content) or f'Observed Flash JEDEC: {request["flashJedec"]}' not in content):
            raise ValueError('Fresh segment lacks successful native evidence.')
    # Reconstruct both roles from their old prefixes and the actual fresh reads.
    assembled={role:Path(request[role]['path']).read_bytes() if request[role] else b'' for role in ('first','second')}
    anchors={s['label']:s for s in state['segments'] if s['label'].endswith('-anchor')}
    if 'first-anchor' not in anchors or check_binding(anchors['first-anchor']['read']).read_bytes()!=assembled['first'][:256]:
        raise ValueError('Fresh first anchor missing/different.')
    prefix=assembled['second'] or assembled['first']
    if len(prefix)>256:
        anchor=anchors.get('boundary-anchor')
        if not anchor or anchor['address']!=max(0,len(prefix)-4096) or check_binding(anchor['read']).read_bytes()!=prefix[anchor['address']:]:
            raise ValueError('Fresh boundary anchor missing/different.')
    for segment in state['segments']:
        if segment['label'].endswith('-anchor'): continue
        role=segment['label'].split('-')[0]
        if role not in assembled or segment['address']!=len(assembled[role]):
            raise ValueError('Segment coverage gap/overlap/wrong role.')
        assembled[role]+=check_binding(segment['read']).read_bytes()
    if assembled['first']!=first.read_bytes() or assembled['second']!=second.read_bytes():
        raise ValueError('Assembled captures do not match immutable source segments.')
    return dict(chip=request['chip'],pcb=request['pcb'],boardNote=request['boardNote'],flashJedec=request['flashJedec'],
        sizeBytes=request['sizeBytes'],first=str(first),second=str(second),sha256=state['first']['sha256'],
        segmented=True,resumeReceiptSha256=bound(unit/'backup/resume-state.json')['sha256'])


if __name__=='__main__':
    if len(sys.argv)!=3 or sys.argv[1]!='--validate-complete':
        raise SystemExit('Use the public backup.ps1 -Resume entry.')
    print(json.dumps(validate_complete(Path(sys.argv[2]))))
