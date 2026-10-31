"""One bounded Full on the maintained, already halted UART/SWS backend.

No serial discovery, reset, retry of writes or independent programmer. The
pre-unlock check is deliberately before the backend's WREN/status write.
"""
import io
import json
from pathlib import Path

from tlsr_full_flash_state import sha
from tlsr_full_flash_write_plan import execute, load_bound_plan


def install(backend, source: Path, output: Path, plan_path: Path, image: bytes,
            expected_jedec: bytes, *, read_uid, confirmed_uid, progress,
            skip_readback=False):
    plan=load_bound_plan(plan_path,image)  # Before backend.main/serial access.
    if expected_jedec.hex().upper()!=plan['observed_jedec']:
        raise ValueError('Programmer JEDEC differs from the bound Full plan')
    receipt=output.with_suffix('.receipt.json')
    if output.exists() or receipt.exists() or output==source or source.is_symlink():
        raise ValueError('Full evidence/source is unsafe or already exists')
    original_unlock=backend.FlashUnlock
    original_write=backend.FlashWriteBlock
    admitted=False
    unlock_started=False
    write_started=False
    completed=False
    reset_started=False
    # The pinned backend normally handles Ctrl+C with sys.exit(0). Full must
    # not turn an interrupted write/verification into a successful process.
    if hasattr(backend,'signal_handler'):
        def interrupt_full(*_):raise KeyboardInterrupt
        backend.signal_handler=interrupt_full

    def guard_unlock(serial_port):
        nonlocal admitted,unlock_started
        if unlock_started:raise ValueError('A Full session may unlock only once')
        unlock_started=True
        if source.read_bytes()!=image or load_bound_plan(plan_path,image)!=plan:
            raise ValueError('Full inputs changed before unlock; no erase')
        samples=[read_uid(backend,serial_port) for _ in range(3)]
        if confirmed_uid(samples).hex().upper()!=plan['uid']:
            raise ValueError('Exact unit changed before unlock; no erase')
        parts=[backend.InkTagReadFlashJedec(serial_port) for _ in range(3)]
        if parts!=[expected_jedec]*3:
            raise ValueError('Flash part changed/incomplete before unlock; no erase')
        from tlsr_full_flash_entitlement import bound_capture
        from tlsr_full_flash_state import FACTORY, SETTINGS
        _, _, factory, settings = bound_capture(plan)
        print('RETENTION: rechecking this unit Factory (4 KiB) and native Settings (8 KiB) before erase; please wait.', flush=True)
        for address, expected in ((FACTORY, factory), (SETTINGS, settings)):
            backend.InkTagSetExpectedReadback(expected)
            current = io.BytesIO()
            if not backend.FlashReadBlock(serial_port, current, address, len(expected)) or current.getvalue() != expected:
                raise ValueError('Same-unit retention media changed/unreadable before erase; no unlock')
        backend.InkTagSetExpectedReadback(None)
        if confirmed_uid([read_uid(backend, serial_port) for _ in range(3)]).hex().upper() != plan['uid']:
            raise ValueError('Unit changed during retention recheck; no unlock')
        admitted=bool(original_unlock(serial_port))
        return admitted

    def write_full(serial_port,stream,offset,size,erase=True):
        nonlocal write_started, completed
        if write_started or not admitted or offset or size!=len(image) or not erase or stream.tell()!=0:
            raise ValueError('Invalid/repeated/unadmitted sparse Full; no erase')
        write_started=True
        if stream.read()!=image:raise ValueError('Prepared Full changed before erase')
        expected=image
        print('FULL: one complete write then one complete readback; Full reinitializes OTA state, allows firmware downgrade, '
              'and does not compare old epochs. New Recovery/Settings commit verification remains mandatory.',flush=True)
        all_written=False
        def write_completed():
            nonlocal all_written
            all_written=True
        try:
            if skip_readback:
                ok=execute(backend,serial_port,original_write,image,plan,None,
                           skip_readback=True,progress=progress,write_completed=write_completed)
            else:
                with output.open('xb') as readback:
                    ok=execute(backend,serial_port,original_write,image,plan,readback,
                               progress=progress,write_completed=write_completed)
        except KeyboardInterrupt:
            result=dict(schema='inktag-tlsr-full-linear-v1',
                status='FULL-WRITTEN-VERIFY-INTERRUPTED' if all_written else 'FULL-WRITE-INTERRUPTED-UNKNOWN',
                preparedSha256=sha(image),planSha256=sha(plan_path.read_bytes()),
                writtenBytes=len(image) if all_written else None,writeCompleted=all_written,
                verificationComplete=False,readbackSkipped=bool(skip_readback),
                applicationRunRequested=False,writeRetryAllowed=False)
            with receipt.open('x',encoding='utf-8') as handle:json.dump(result,handle,indent=2)
            print(result['status']+': cancelled; NOT VERIFIED. No automatic retry or application reset/run.',flush=True)
            raise SystemExit(130)
        if not ok:
            print('FULL FAILED/UNKNOWN: no write replay or application run; keep all partial evidence.',flush=True)
            return False
        if not skip_readback and output.read_bytes()!=expected:
            raise ValueError('Complete linear readback mismatch; CPU must remain halted')
        result=dict(schema='inktag-tlsr-full-linear-v1',
            status='FULL-WRITTEN-NOT-VERIFIED' if skip_readback else 'FULL-READBACK-MATCHED',
            preparedSha256=sha(image),planSha256=sha(plan_path.read_bytes()),
            writtenBytes=len(expected),writtenSha256=sha(expected),readbackOrder='linear-address',
            readbackSha256=None if skip_readback else sha(output.read_bytes()),
            readbackSkipped=bool(skip_readback),recoveryStateInitialized=True,recoveryStateVerified=True,
            settingsCommitVerified=any(row['kind']=='settings-generation' for row in plan['segments']),
            proPreservation=plan['retentionCapture']['proof'],
            retainedDiagnostics='No qualified TLSR persistent crash/reset mailbox; whole-chip reset remains required; App startup not observed',
            externalRetained='NFC EEPROM not accessed; no external erasure claim',
            wholeChipResetRequested=False,
            retainedHolesVerified=False,releaseQualified=False)
        with receipt.open('x',encoding='utf-8') as handle:json.dump(result,handle,indent=2)
        completed=True
        print(result['status']+f": {len(expected)} complete image bytes; SHA256={sha(expected)}",flush=True)
        return True

    backend.FlashUnlock=guard_unlock
    backend.FlashWriteBlock=write_full
    # Use only the existing qualified whole-chip reset boundary. TLSR has no
    # qualified retained crash mailbox to clear by address; never bulk-zero
    # SRAM or touch an unknown reset-cause register/live loader stack.
    original_sws_write = getattr(backend, 'sws_wr_addr_usbcom', None)
    if original_sws_write is None or not callable(getattr(backend, 'sws_wr_addr', None)):
        raise ValueError('Maintained backend reset boundary unavailable; no device accessed')
    def reset_boundary(serial_port, address, data):
        nonlocal reset_started
        is_reset = address == 0x006f and bytes(data) in (b'\x20', b'\x22')
        if is_reset and write_started and not completed:
            raise ValueError('Write/critical verification unknown; final reset forbidden')
        if is_reset and completed:
            if reset_started:
                raise ValueError('Final reset already attempted; no reset replay')
            reset_started=True
        result = original_sws_write(serial_port, address, data)
        if is_reset and completed:
            if type(result) is not int or result != len(backend.sws_wr_addr(address, b'\x20')):
                raise ValueError('Final reset transport write incomplete/unknown; no reset replay or boot claim')
            evidence = json.loads(receipt.read_text(encoding='utf-8'))
            evidence['wholeChipResetRequested'] = True
            evidence['resetAttested'] = False
            evidence['diagnosticCleanup'] = dict(
                flash='fresh Settings generation and erased peer; old history omitted',
                qualifiedRetainedCrashMailbox=False,
                nativeResetRequest='whole-chip0x20; unchanged cold App clears current runtime health state',
                hardwareBootObserved=False)
            receipt.write_text(json.dumps(evidence, indent=2), encoding='utf-8')
        return result
    backend.sws_wr_addr_usbcom = reset_boundary
    return plan
