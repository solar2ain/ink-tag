#!/usr/bin/env python3
"""GR host adapter to maintained hardware readers and shared log/publication.

No BLE, Flash writer, unlock, arbitrary register access or image build. Only
no-contact probe attempts may repeat. Captures always retain their raw session.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


workflow = load('gr_shared_flash_workflow', Path(__file__).with_name('flash-workflow.py'))
presentation = load('gr_tool_presentation', Path(__file__).with_name('goodix_tool_log.py'))
Log = presentation.log_class(workflow)
catalog = load('gr_stock_catalog', Path(__file__).with_name('goodix-backup-catalog.py'))
SCHEMA = workflow.SCHEMA
LABEL = re.compile(r'^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$')
GR551X = ('gr551x', 'gr5513', 'gr5515')


def compatible_chip(requested, actual):
    return requested == actual or requested in GR551X and actual in GR551X


def label(value):
    if (not LABEL.fullmatch(value) or value.endswith('.') or
            re.match(r'(?i)^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)', value)):
        raise ValueError('Board must be a safe PCB label, not a path')
    return value


def selection(args):
    records = {} if args.action == 'probe' else catalog.board_records()
    legacy = records.get(args.board.lower())
    chip = args.chip
    if chip not in ('gr5331', *GR551X):
        raise ValueError('-Chip gr5331/gr551x/gr5513/gr5515 is required')
    if args.action == 'backup' and not args.board:
        raise ValueError('Backup requires -Board <PCB-label>; no hardware accessed')
    if args.family == 'gr5331' and chip != 'gr5331' or args.family == 'gr551x' and chip == 'gr5331':
        raise ValueError('Chip does not belong to this owning port')
    if legacy and not compatible_chip(chip, legacy['chip']):
        raise ValueError('Requested Chip/logical Board mismatch')
    if args.action == 'probe' and (args.board or args.board_note or args.product_name):
        raise ValueError('Probe selects Chip only; PCB/BoardNote/ProductName metadata belongs to backup')
    board = '' if args.action == 'probe' else legacy['pcb'] if legacy else args.board or 'UNIDENTIFIED-PCB'
    if args.action != 'probe':
        label(board)
    if len(args.board_note) > 256 or any(ord(c) < 32 or ord(c) == 127 for c in args.board_note):
        raise ValueError('BoardNote must be at most 256 characters without controls')
    if not 1 <= args.interval_seconds <= 60 or not 0 <= args.timeout_seconds <= 86400:
        raise ValueError('Invalid detection interval/deadline')
    if not 3 <= args.operation_timeout_seconds <= 7200 or not 1000 <= args.frequency_hz <= 1000000:
        raise ValueError('Invalid operation timeout/SWD frequency')
    if args.snr and not args.snr.isdecimal():
        raise ValueError('Snr must be numeric')
    if args.product_name not in ('', 'zkc21v', 'zkc42v', 'zkc42vm'):
        raise ValueError('ProductName must be zkc21v/zkc42v/zkc42vm')
    if args.output_dir:
        path = catalog.reject_links(Path(args.output_dir))
        if any(p.lower() in ('factory_backups', 'backups') for p in path.parts):
            raise ValueError('Unverified raw captures cannot start in a factory-only directory')
    return chip, board, legacy


def command(args, chip, action, directory, result, hold=False, request=None):
    # Family aliases are detection selectors only. Reads and publication must
    # use the exact returned MCU, never the operator's alias/default capacity.
    if action == 'probe' and chip in GR551X:
        chip = 'gr551x'
    cmd = [args.powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
           str(ROOT / 'firmware/ports/goodix/common/tools/internal/goodix-stock-entry.ps1'),
           '-Action', action, '-Chip', chip, '-EvidenceDir', str(directory), '-ResultFile', str(result),
           '-FrequencyHz', str(args.frequency_hz)]
    if action == 'probe' and args.action == 'probe':
        cmd += ['-McuOnly']
    if args.jlink_exe:
        cmd += ['-JLinkExe', args.jlink_exe]
    if args.snr:
        cmd += ['-Snr', args.snr]
    if hold:
        cmd.append('-LeaveHalted')
    if request:
        cmd += ['-RequestFile', str(request)]
    return cmd


def json_file(path, record):
    catalog.reject_links(path)
    with path.open('x', encoding='utf-8') as stream:
        json.dump(record, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def read_identity(path, chip, *, mcu_only=False):
    identity = json.loads(catalog.reject_links(path).read_text(encoding='utf-8-sig'))
    if identity.get('chipConfirmed'):
        actual = identity['chip']
        if not mcu_only and not compatible_chip(chip, actual):
            raise ValueError('Exact MCU mismatch; no retry or Flash capture')
        admitted = {4: ('gr5515', 1048576), 5: ('gr5513', 524288), 7: ('gr5513', 524288)}
        if mcu_only:
            admitted = {1:('gr5515',None), 2:('gr5515',None), 3:('gr5515',1048576),
                        4:('gr5515',None), 5:('gr5513',524288), 6:('gr5515',524288),
                        7:('gr5513',524288), 8:('gr5515',None), 9:('gr5515',524288)}
            # Older host fixtures/qualified adapters may still supply a
            # board-derived external-Flash length; it is not MCU probe evidence.
            if identity.get('packageId') == 4:
                identity['flashBytes'] = None
        if admitted.get(identity.get('packageId')) != (actual, identity.get('flashBytes')):
            raise ValueError('Unknown package/capacity identity')
    elif (not mcu_only and chip != 'gr5331') or identity.get('chip') != 'UNIDENTIFIED-MCU':
        raise ValueError('Unconfirmed foreign identity refused')
    return identity


def verify_pair(first, second, size):
    if first.absolute() == second.absolute():
        raise ValueError('Two independent read files required')
    hashes = []
    for path in (first, second):
        catalog.reject_links(path)
        if path.stat().st_size != size:
            raise ValueError('Incomplete Flash read; preserve both, no replay')
        hashes.append(hashlib.sha256(path.read_bytes()).hexdigest())
    if hashes[0] != hashes[1]:
        raise ValueError('Independent reads differ; no factory publication')
    return hashes[0]


def report_probe_success(log, identity, args):
    if identity['chipConfirmed']:
        presentation.report_confirmed_mcu(log, identity)
        if not compatible_chip(args.chip, identity['chip']):
            log.emit('probe', f'Requested MCU {args.chip.upper()} does not match detected '
                     f'{identity["chip"].upper()}. Detection succeeded; this is not permission to read or flash the wrong target.', level='warning')
        elif args.chip != identity['chip']:
            log.emit('probe', f'{args.chip.upper()} accepts this GR551x family member; '
                     'subsequent backup uses the actual MCU and capacity.', level='notice')
    else:
        log.emit('probe', f'Probe successful: SWD connected; ARM Cortex-M4 core responds; '
                 f'CPUID={identity.get("cpuid", "not recorded")}.', level='result')
        log.emit('probe', f'Requested MCU: {args.chip.upper()}. Exact silicon/package is not independently verified; '
                 'core-only success does not qualify a factory backup or Full flash.', level='warning')
        if identity.get('identityReason'):
            log.emit('probe', identity['identityReason'], level='warning')
    connection = ('RESET-assisted' if identity.get('resetFallbackObserved') else
                  'direct attach' if identity.get('directAttachObserved') else 'not recorded')
    log.emit('probe', f'SWD requested: {args.frequency_hz} Hz; connection: {connection}.', level='notice')


def execute(args, run=workflow.run_command, sleep=time.sleep, monotonic=time.monotonic):
    chip, pcb, legacy = selection(args)
    primary = catalog.primary_root(ROOT)
    token = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '-' + uuid.uuid4().hex[:8]
    session = Path(args.output_dir) if args.output_dir else primary / '.local/backup-archive' / chip / 'pending' / token
    session = catalog.reject_links(session)
    log = Log(session, workflow.Board(chip, chip, 'goodix'), args.action, 1, verbose=args.verbose)
    record = dict(schema=SCHEMA, action=args.action, requested_chip=chip, pcb_silkscreen=pcb,
                  board_note=args.board_note, product_name=args.product_name or (legacy or {}).get('productName', ''),
                  started_utc=workflow.utc(), status='running', hardware_started=False,
                  flash_writes=False, raw_session_retained=True, current_device_qualified=False)
    if args.action == 'probe':
        for field in ('pcb_silkscreen','board_note','product_name'):
            record.pop(field)
    outcome = 1
    try:
        start = monotonic()
        # Explicit RetryDetect is still bounded when the caller leaves the
        # legacy zero deadline unset. This limits detection only, never reads.
        detect_timeout = args.timeout_seconds or (120 if args.retry_detect else args.operation_timeout_seconds)
        log.emit('probe', 'Trying direct SWD attach first; J-Link may use the ordinary RESET-pin connection fallback. '
                 'If RESET is not wired, press RESET manually (or power-cycle). No BOOT/ROM ISP is needed.', level='notice')
        attempt = 0
        while True:
            attempt += 1
            identity_file = session / f'identity-{attempt:03d}.json'
            remaining = detect_timeout - (monotonic() - start)
            if remaining <= 0:
                raise ValueError('Detection deadline reached; no Flash read started')
            record['hardware_started'] = True
            record['probe_attempts'] = attempt
            budget = min(60, args.operation_timeout_seconds, remaining)
            detect = command(args, chip, 'probe', session / f'probe-{attempt:03d}', identity_file,
                             hold=args.action == 'backup')
            if args.retry_detect and budget >= 45:
                detect += ['-CaptureWindow']
            result = run(detect, log, 'probe', attempt, operation_timeout=budget)
            if result.code == 0:
                identity = read_identity(identity_file, chip, mcu_only=args.action == 'probe')
                break
            if monotonic() - start >= detect_timeout:
                raise ValueError('Detection deadline reached; no Flash read started')
            if (not args.retry_detect or result.code == 124 or workflow.REFUSAL.search(result.text)
                    or not workflow.TRANSIENT.search(result.text)):
                raise ValueError('Probe failed/refused; raw attempts retained, no replay')
            if monotonic() - start + args.interval_seconds >= detect_timeout:
                raise ValueError('Detection deadline reached; no Flash read started')
            log.emit('wait', 'Still no target contact; retrying detection only within the deadline. '
                     'If RESET is not wired, press RESET manually (or power-cycle).', level='progress', attempt=attempt)
            sleep(args.interval_seconds)
        record['identity'] = identity
        record['probe_attempts'] = attempt
        if args.action == 'probe':
            record['status'] = 'tool-success' if identity['chipConfirmed'] else 'core-only-unconfirmed'
            report_probe_success(log, identity, args)
            outcome = 0
        else:
            # Probe must not release the short factory attach window before
            # the reader starts. Resolve all later operations to actual silicon.
            chip = identity['chip'] if identity['chipConfirmed'] else chip
            if identity.get('probeSerial'):
                if args.snr and args.snr != identity['probeSerial']:
                    raise ValueError('Probe serial changed; no Flash capture')
                args.snr = identity['probeSerial']
            report_probe_success(log, identity, args)
            files = []
            try:
                for number in (1, 2):
                    directory = session / f'read-{number}'
                    result = run(command(args, chip, 'read', directory, session / f'read-{number}.json', request=identity_file),
                                 log, f'read-{number}', 1, operation_timeout=args.operation_timeout_seconds)
                    if result.code != 0:
                        raise ValueError(f'Read {number} failed; no automatic read replay')
                    images = list(directory.rglob('*-xip-flash.bin'))
                    if len(images) != 1:
                        raise ValueError('Expected exactly one complete image per read')
                    files.append(images[0])
                digest = verify_pair(*files, identity['flashBytes'])
                source = catalog.inspect(files[0], chip, legacy and args.board or (pcb if pcb != 'UNIDENTIFIED-PCB' else ''))
                if source['classification'] in ('invalid-capture', 'identity-mismatch'):
                    raise ValueError(f"Capture refused: {source['classification']}; no publication")
                if source['stockQualified'] and args.product_name and args.product_name != catalog.board_records()[source['pcb']]['productName']:
                    raise ValueError('Factory content does not match explicit ProductName; no publication')
                original = bool(identity['chipConfirmed'] and source['stockQualified'] and pcb != 'UNIDENTIFIED-PCB')
                record.update(status='tool-success' if identity['chipConfirmed'] else 'matched-unconfirmed-identity',
                              verification='matched-double-read', classification=source['classification'] if identity['chipConfirmed'] else 'unknown-actual-chip',
                              content_classification=source['classification'],
                              original_qualified=original, sha256=digest, bytes=identity['flashBytes'])
                # Raw evidence stays fixed. Publish only known stock content +
                # live admitted package; matching unknown/current reads stay local.
                if original and not args.output_dir:
                    request = session / 'publication-request.json'
                    identity.update(sourceQualification='reviewed-exact-chip-content-SHA', productName=record['product_name'],
                                    nfcFitted=None, identityEvidenceSha256=hashlib.sha256(identity_file.read_bytes()).hexdigest())
                    json_file(request, dict(session=dict(Root=str(primary), Directory=str(session), Chip=chip,
                              Board=pcb, BoardNote=args.board_note, Token=token, ExplicitOutput=False),
                              first=str(files[0]), second=str(files[1]), bytes=identity['flashBytes'],
                              originalQualified=True, sha256=digest, identity=identity,
                              range=dict(address=identity['baseAddress'], bytes=identity['flashBytes'])))
                    published_file = session / 'publication-result.json'
                    result = run(command(args, chip, 'publish', session, published_file, request=request),
                                 log, 'publish', 1, operation_timeout=args.operation_timeout_seconds)
                    if result.code != 0:
                        raise ValueError('Publication failed; raw captures retained, no device replay')
                    record['factory_directory'] = json.loads(published_file.read_text(encoding='utf-8-sig'))['directory']
                log.emit('backup', f'Two reads matched ({digest}); classification {record["classification"]}; original qualified={original}. Target cleanup is still pending.', level='notice')
                outcome = 0
            finally:
                # One cleanup only: GR5331 register halt/go; GR551x ordinary
                # reset after its RAM reader. Neither replays a failed read.
                cleanup_command = command(args, chip, 'resume', session / 'resume', session / 'resume.json')
                markers = sorted(session.glob('read-*/vendor-read-started.json')) if chip in GR551X else []
                if markers:
                    # The vendor readFlash RAM helper destroys App context.
                    # One ordinary reset is required; never GO into its RAM.
                    cleanup_command += ['-AfterVendorRead', '-RequestFile', str(markers[-1])]
                cleanup = run(cleanup_command,
                              log, 'resume', 1, operation_timeout=min(45, args.operation_timeout_seconds))
                record['resume_exit_code'] = cleanup.code
                if cleanup.code != 0:
                    record['resume_state'] = 'unknown; captures retained; no cleanup replay'
                    record['status'] = 'cleanup-failed'
                    log.emit('resume', record['resume_state'], level='error')
                    outcome = 1
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        record.update(status='failed', error=str(exc))
        log.emit('result', str(exc), level='error')
    except KeyboardInterrupt:
        record.update(status='interrupted', error='Canceled; retain evidence; target halt/resume state may be unknown')
        outcome = 130
    finally:
        try:
            record['ended_utc'] = workflow.utc()
            record['exit_code'] = outcome
            json_file(session / 'result.json', record)
            if outcome == 0 and args.action == 'backup':
                if record.get('factory_directory') and record.get('original_qualified'):
                    log.emit('result', 'Congratulations! Your factory backup is complete: both independent reads matched and the archive was validated.', level='result')
                elif record.get('identity', {}).get('chipConfirmed'):
                    log.emit('result', 'Congratulations! Your backup is complete: both independent reads matched. This is not a published factory-qualified backup.', level='result')
                else:
                    log.emit('result', 'Backup read successful: both independent diagnostic reads matched and target cleanup completed.', level='result')
                    log.emit('result', 'Exact MCU identity remains unconfirmed. This is not a factory-qualified backup.', level='warning')
            evidence_level = ('notice' if args.action == 'probe' else
                              'result' if record.get('identity', {}).get('chipConfirmed') else 'warning') if outcome == 0 else 'error'
            log.emit('result', f'Evidence: {session}', level=evidence_level, exit_code=outcome)
        finally:
            log.close()
    return outcome


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('probe', 'backup'))
    p.add_argument('--chip', default='', type=str.lower); p.add_argument('--family', choices=('gr5331', 'gr551x'), required=True)
    for name in ('board', 'board-note', 'product-name', 'output-dir', 'jlink-exe', 'snr'):
        p.add_argument('--' + name, default='')
    for name in ('retry-detect', 'dry-run', 'verbose'):
        p.add_argument('--' + name, action='store_true')
    for name, default in (('frequency-hz',500000),('interval-seconds',1),('timeout-seconds',0),('operation-timeout-seconds',60)):
        p.add_argument('--' + name, type=int, default=default)
    p.add_argument('--powershell', default='')
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        chip, pcb, legacy = selection(args)
        if args.dry_run:
            primary = catalog.primary_root(ROOT)
            plan=dict(schema=SCHEMA, action=args.action, chip=chip, pcb_silkscreen=pcb,
                                  primaryRoot=str(primary),
                                  board_note=args.board_note, hardware_started=False, retryDetect=args.retry_detect,
                                  flashAccess=args.action=='backup', flashWrites=False, reads=2 if args.action=='backup' else 0,
                                  registerAddresses=['0xE000ED00'] if chip=='gr5331' else ['0xE000ED00','0x00803214','0x0080322E'],
                                  exactPackageIdentified=False, gr5331CoreOnly=chip=='gr5331')
            if args.action == 'probe':
                for field in ('pcb_silkscreen','board_note'):
                    plan.pop(field)
            print(json.dumps(plan, indent=2))
            return 0
        args.powershell = args.powershell or shutil.which('pwsh') or shutil.which('powershell')
        if not args.powershell:
            raise ValueError('PowerShell missing; no hardware started')
        return execute(args)
    except (OSError, ValueError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
