"""GR presentation over the existing stream/log engine; no device decisions.

PowerShell owns console colours. Children remain piped so redirected output
cannot be bypassed through CONOUT$. Vendor percentages without byte counters
may be shown verbatim, but never acquire a made-up transfer timer or byte count.
"""
import re
import json


def report_confirmed_mcu(log, identity, *, phase='probe'):
    """One green detection receipt, not a backup/Full completion claim."""
    if identity.get('chipConfirmed') is not True:
        raise ValueError('Cannot report confirmed MCU from core-only evidence')
    capacity = (f'package-defined Flash {identity["flashBytes"] // 1024} KiB.'
                if identity.get('flashBytes') is not None else 'Flash capacity not detected.')
    log.emit(phase, f'Probe successful: {identity["chip"].upper()} '
             f'({identity.get("packageName", "known package")}, package ID {identity["packageId"]}); '
             f'CPUID={identity.get("cpuid", "not recorded")}; {capacity}', level='result',
             mcu_detected=True, detected_chip=identity['chip'], package_id=identity['packageId'],
             flash_bytes=identity.get('flashBytes'), detection_only=True)


def log_class(workflow):
    class GoodixLog(workflow.Log):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.vendor_progress = {}
            self.emit('preflight', f'Logs: {self.path}', level='notice')

        def activity_message(self, phase, elapsed=None, *, has_progress=False):
            if phase=='probes':
                return 'Enumerating the exact J-Link; no target memory access in this stage'
            if phase in ('probe', 'detect'):
                return 'Detecting exact target/probe; no Flash transfer started; please wait'
            if phase in ('read-1', 'read-2'):
                if 'vendor_percent' in self.last_tool_progress.get(phase,{}):
                    return f'Read {phase[-1]}/2: latest vendor-reported progress; completed-byte count unavailable'
                detail = 'latest completed block count' if has_progress else 'waiting for confirmed Flash data; please wait'
                return f'Read {phase[-1]}/2: {detail}'
            labels = {
                'boot-read': 'Reading exact boot metadata',
                'full-readback': 'Reading complete Full verification range',
                'single-full-write': 'Writing Full image with the maintained vendor tool',
                'mode-0': 'Reading immutable identity',
                'mode-1': 'Waiting for fixed first-OFF initialization/readback',
                'publish': 'Checking double-read hashes and publishing backup metadata',
                'resume': 'Releasing target after read-only maintenance',
                'restore-app-context': 'Restoring App context for the fixed helper',
                'start-verified-app': 'Starting the verified App',
            }
            label = labels.get(phase, phase.replace('-', ' '))
            detail = 'latest vendor progress follows' if has_progress else 'no reliable incremental byte callback'
            return f'{label}; {detail}; please wait'

        def tool_line(self, phase, line):
            if phase=='full-readback' and line.strip().startswith('{'):
                try:event=json.loads(line)
                except ValueError:event=None
                if (isinstance(event,dict) and event.get('Status')=='Reading' and
                    isinstance(event.get('Progress'),(int,float)) and
                    not isinstance(event.get('Progress'),bool) and 0<=event['Progress']<=100):
                    value=event['Progress']
                    self.last_tool_progress[phase]={'vendor_percent':value}
                    self.emit(phase,f'Vendor read progress: {value:g}% (completed-byte count unavailable)',
                              level='notice',vendor_percent=value)
                    return
            if re.fullmatch(r'\s*Wait:\s*(?:True|False), interval=\d+s, timeout=\d+s\s*',line):
                self.emit(phase,line,level='diagnostic')
                return
            native=re.fullmatch(r'\s*(Downloading|Programming)\s+(\d+)%\s*',line)
            if native and 0<=int(native[2])<=100:
                value=int(native[2])
                self.last_tool_progress[phase]={'vendor_percent':value,'vendor_operation':'Write'}
                self.emit(phase,f'Vendor write progress: {value}% (completed-byte count unavailable)',
                          level='notice',vendor_percent=value)
                return
            vendor=re.fullmatch(r'GR_READ_VENDOR_PROGRESS: (\d+(?:\.\d+)?)',line.strip())
            if vendor:
                value=float(vendor[1])
                if not 0<=value<=100:
                    self.emit(phase,'Invalid vendor progress',level='error');return
                self.last_tool_progress[phase]={'vendor_percent':value}
                self.emit(phase,f'Vendor read progress: {value:g}% (completed-byte count unavailable)',
                          level='notice',vendor_percent=value)
                return
            if line == 'J-Link Commander will now exit on Error':
                # Informational ExitOnError banner, not a failed operation.
                # Preserve the raw line and keep actual errors classified below.
                self.emit(phase, line, level='diagnostic')
                return
            if line.startswith('GR_READ_STAGE:'):
                self.end_progress()
                self.emit(phase, line.split(':', 1)[1].strip(), level='notice')
                return
            counters = re.fullmatch(r'GR_READ_PROGRESS: (\d+)/(\d+)', line.strip())
            if counters:
                completed, total = map(int, counters.groups())
                # Owned reader telemetry only, bounded to the reviewed capture
                # range. Zero is a wait, not confirmed data or a timer origin.
                if total not in (524288, 1048576) or not 0 <= completed <= total:
                    self.emit(phase, 'Invalid reader progress counters', level='error')
                    return
                if not completed:
                    self.emit(phase, 'Waiting for the first complete Flash block', level='notice')
                    return
                fields = dict(percent=completed*100/total, completed_kib=completed/1024, total_kib=total/1024)
                self.last_tool_progress[phase] = fields
                self.emit(phase, f'Reading {fields["percent"]:g}% ({completed/1024:g}/{total/1024:g} KiB)',
                          level='progress', **fields)
                return
            if line.startswith('GR_CAPTURE_READY:'):
                self.emit(phase, line, level='notice')
                return
            # Existing InkTag-owned GProgrammer wrapper header, not a vendor
            # protocol/result parser. Erase can be silent for a long time.
            stage = re.fullmatch(r'== ([^\x00-\x1f\x7f]+) ==', line.strip())
            if stage:
                self.end_progress()
                self.progress_started.pop(phase, None)
                self.last_tool_progress.pop(phase, None)
                self.last_progress.pop(phase, None)
                self.vendor_progress.pop(phase, None)
                self.emit(phase, f'{stage[1]}; please wait (no completed-byte callback)', level='notice')
            else:
                super().tool_line(phase, line)

        def emit(self, phase, message, *, level='diagnostic', **fields):
            if level=='progress' and 'vendor_percent' in fields:
                # Keep the last real value on the same progress row; the
                # heartbeat must not break it with a separate wait message.
                operation='write' if fields.get('vendor_operation')=='Write' else 'read'
                message=f'Vendor {operation} progress: {fields["vendor_percent"]:g}% (completed-byte count unavailable)'
                level='notice'
            if message == 'Starting maintained tool' and 'raw_log' in fields:
                self.vendor_progress.pop(phase, None)
            percent = fields.get('percent')
            if level == 'progress' and percent is not None and not fields.get('completed_kib', 0):
                # A zero-percent/start-address line or native percentage alone
                # does not establish completed bytes or a transfer time origin.
                level = 'diagnostic' if self.vendor_progress.get(phase) == percent else 'notice'
                self.vendor_progress[phase] = percent
                if '%' not in message:
                    message += f'; vendor reports {percent:g}% (completed-byte count unavailable)'
            return super().emit(phase, message, level=level, **fields)

    return GoodixLog
