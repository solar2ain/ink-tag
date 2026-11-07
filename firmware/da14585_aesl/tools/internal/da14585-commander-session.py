"""Pinned, serialized Commander session for the guarded Full backend.

Each mutating helper command must finish and pass the transcript gate before
another is submitted. SRAM staging/capture can group native file commands;
the host verifies every result before committing the helper mailbox.
There is no process/command retry, automatic reset or transport fallback.
"""
import hashlib
import json
from pathlib import Path
import queue
import re
import runpy
import threading
import time

PROMPT = b'J-Link>'


def qualify_serial(requested, startup, inventory=None):
    serials = set(re.findall(r'(?im)^\s*S/N:\s*(\d+)\s*$', startup))
    if len(serials) != 1 or requested and serials != {requested}:
        raise RuntimeError('Commander probe identity incomplete/different')
    serial = next(iter(serials))
    if not requested:
        available = {str(int(number)) for number in re.findall(r'(?i)Serial number:\s*(\d+)', inventory or '')}
        if available != {str(int(serial))}:
            raise RuntimeError('Select an exact J-Link serial; inventory is absent/ambiguous')
    return serial


class Session:
    def __init__(self, executable, manifest, serial, speed, directory, timeout=15):
        if (serial and (not str(serial).isdigit() or not 4 <= len(str(serial)) <= 16)) or type(speed) is not int or not 1 <= speed <= 4000:
            raise ValueError('Exact numeric Commander serial and bounded clock required')
        self.reader = runpy.run_path(str(Path(__file__).with_name('da14585-commander-readback.py')))
        executable = self.reader['verify_executable'](executable, manifest)
        self.directory = Path(directory)
        self.reader['quote'](self.directory/'capture-ffffffff.bin')
        for part in (self.directory, *self.directory.parents):
            if part.exists() and (part.is_symlink() or getattr(part.lstat(), 'st_file_attributes', 0) & 0x400):
                raise ValueError('Linked Commander session directory refused')
        self.directory.mkdir(parents=True, exist_ok=False)
        self.serial, self.speed = str(serial), speed
        self.failed, self.closed, self.counter = False, False, 0
        self.pending = b''
        self.outputs = queue.Queue()
        self.log = (self.directory/'native.log').open('xb')
        self.records = (self.directory/'commands.jsonl').open('xb')
        argv = [str(executable), '-NoGui', '1', '-ExitOnError', '1',
                '-Device', 'Cortex-M0', '-If', 'SWD', '-Speed', str(speed), '-AutoConnect', '0']
        if self.serial:
            argv += ['-SelectEmuBySN', self.serial]
        conpty = runpy.run_path(str(Path(__file__).with_name('da14585-conpty.py')))
        try:
            self.process = conpty['Process'](argv)
        except BaseException:
            self.records.close()
            self.log.close()
            raise
        self.thread = threading.Thread(target=self._drain, daemon=True)
        self.thread.start()
        try:
            text = self._receive(timeout)
            if 'J-Link Commander V7.88j' not in text:
                raise RuntimeError('Unexpected Commander banner')
            for line in ('EoE 1', 'exec SetRestartOnClose = 0', 'exec SetSkipDebugDeInit = 1',
                         'exec DisableFlashDL', 'exec DisableFlashBPs'):
                text += self.exchange(line, timeout)
            inventory = self.exchange('ShowEmuList USB', timeout) if not self.serial else None
            self.serial = qualify_serial(self.serial, text, inventory)
        except BaseException:
            self.failed = True
            self.close()
            raise

    def _drain(self):
        try:
            while True:
                chunk = self.process.stdout.read1(4096)
                if not chunk:
                    break
                self.log.write(chunk)
                self.log.flush()
                self.outputs.put(chunk)
        finally:
            self.outputs.put(None)

    def _receive(self, timeout, *, final=False, expected_reset=False):
        def checked(text):
            value = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]|\x1b\][^\x07]*(?:\x07|\x1b\\)', '', text)
            if expected_reset:
                value = re.sub(r'(?im)^Reset: Halt core after reset via DEMCR\.VC_CORERESET\.\r?$', '', value)
            return value
        deadline = time.monotonic()+timeout
        while True:
            if not final and PROMPT in self.pending:
                text, self.pending = self.pending.split(PROMPT, 1)
                text = checked(text.decode('utf8', errors='replace'))
                if self.reader['BAD'].search(text):
                    raise RuntimeError('Commander reported an error; no next command or replay')
                return text
            remaining = deadline-time.monotonic()
            if remaining <= 0:
                raise TimeoutError('Commander command outcome unknown; no replay')
            try:
                chunk = self.outputs.get(timeout=remaining)
            except queue.Empty as error:
                raise TimeoutError('Commander command outcome unknown; no replay') from error
            if chunk is None:
                text, self.pending = self.pending.decode('utf8', errors='replace'), b''
                if not final or self.reader['BAD'].search(text):
                    raise RuntimeError('Commander exited before a qualified boundary; no replay')
                return text
            self.pending += chunk
            partial = checked(self.pending.decode('utf8', errors='replace'))
            if self.reader['BAD'].search(partial.rsplit('\n',1)[0] if '\n' in partial else ''):
                raise RuntimeError('Commander reported an error before its prompt; no replay')

    def exchange(self, command, timeout=15):
        if self.failed or self.closed or timeout <= 0 or not command or any(c in command for c in '\r\n'):
            raise RuntimeError('Terminal/invalid Commander exchange; no replay')
        self.counter += 1
        record = dict(index=self.counter, command=command, nativeCommandSubmitted=False,
                      complete=False, elapsedSeconds=None, error=None)
        started = time.monotonic()
        try:
            record['nativeCommandSubmitted'] = True
            self.process.stdin.write((command+'\r').encode('ascii'))
            self.process.stdin.flush()
            text = self._receive(timeout, expected_reset=command=='r')
            record.update(complete=True, responseSHA256=hashlib.sha256(text.encode()).hexdigest())
            return text
        except BaseException as error:
            self.failed = True
            record['error'] = f'{type(error).__name__}: {error}'
            raise
        finally:
            record['elapsedSeconds'] = time.monotonic()-started
            self.records.write((json.dumps(record, sort_keys=True)+'\n').encode())
            self.records.flush()

    def new_file(self, data=None):
        self.file_counter = getattr(self, 'file_counter', 0)+1
        path = self.directory/f'capture-{self.file_counter:08x}.bin'
        if data is not None:
            self.reader['write_new'](path, data)
        elif path.exists():
            raise RuntimeError('Commander capture would overwrite prior evidence')
        return path

    def memory_batch(self, commands, timeout=15):
        """Only non-Flash SRAM file operations; no Go/reset/mailbox commit."""
        if self.failed or self.closed or timeout <= 0 or not 1 <= len(commands) <= 64:
            raise RuntimeError('Terminal/invalid Commander memory group')
        for command in commands:
            if any(c in command for c in '\r\n'):
                raise ValueError('Multiline Commander memory operation')
            match = re.fullmatch(r'(loadbin|verifybin|savebin) "([^"]+)", 0x([0-9a-f]+)(?:, (noreset|0x[0-9a-f]+))?', command)
            if match is None:
                raise ValueError('Only allocated SRAM file operations may be grouped')
            operation, path, address, tail = match.groups()
            path, address = Path(path), int(address, 16)
            if path.parent.resolve() != self.directory.resolve() or path.is_symlink():
                raise ValueError('Memory group file must belong to this native session')
            if operation == 'loadbin' and tail != 'noreset' or operation == 'verifybin' and tail is not None:
                raise ValueError('Reset/extra argument in memory group')
            if operation == 'savebin' and (not tail or not tail.startswith('0x') or path.exists()):
                raise ValueError('Invalid/overwriting SRAM capture')
            length = int(tail, 16) if operation == 'savebin' else path.stat().st_size
            if not 0 < length <= 4096 or not 0x07FC0000 <= address < address+length <= 0x07FD8000:
                raise ValueError('Memory group outside owned SRAM')
        return self._group(commands, timeout)

    def helper_group(self, stage, *, command=None, wait_ms=None, timeout=15):
        """Fixed one-helper boundaries, never a queue of Flash operations.

        A commit only updates the held mailbox and reads it back. A run-wait
        contains exactly one Go, followed only by wait/read observations.
        Host qualification separates commit, PC selection, run and the next
        helper command. No reset, arbitrary address or caller command list.
        """
        if stage == 'commit' and type(command) is int and 1 <= command <= 5 and wait_ms is None:
            commands = [f'w4 0x07fc7c04, 0x{command:08x}',
                        'mem32 0x07fc7c04, 1', 'mem32 0xe000edf0, 1']
        elif stage in ('wait', 'run-wait') and command is None and type(wait_ms) is int and 1 <= wait_ms <= 300000:
            commands = (['g'] if stage == 'run-wait' else []) + [f'WH {wait_ms}', 'mem32 0xe000edf0, 1']
        elif stage == 'sync' and command is None and wait_ms is None:
            # Caller must first prove S_HALT from an actual debug-register read.
            commands = ['halt', 'regs']
        else:
            raise ValueError('Unallocated helper boundary; no command submitted')
        return self._group(commands, timeout)

    def debug_control_group(self, stage, value=None, timeout=15):
        # Fixed CoreSight sequences only. TAR is independently checked by
        # the caller before the sole DRW write to DHCSR. No Flash command.
        if stage == 'select':
            commands = ['WriteDP 2, 0x00000000', 'ReadAP 0']
        elif stage == 'address':
            commands = ['WriteAP 1, 0xe000edf0', 'ReadAP 1']
        elif stage == 'control' and value in (0xA05F0001,0xA05F0003):
            commands = [f'WriteAP 3, 0x{value:08x}', 'ReadDP 3', 'ReadDP 1']
        else:
            raise ValueError('Unallocated CoreSight control sequence')
        return self._group(commands, timeout)

    def _group(self, commands, timeout):
        if self.failed or self.closed or timeout <= 0:
            raise RuntimeError('Terminal Commander group; no replay')
        self.counter += len(commands)
        record = dict(index=self.counter, commands=commands, complete=False, error=None)
        started, deadline = time.monotonic(), time.monotonic()+timeout
        try:
            self.process.stdin.write(('\r'.join(commands)+'\r').encode('ascii'))
            self.process.stdin.flush()
            replies = [self._receive(deadline-time.monotonic()) for _ in commands]
            record.update(complete=True, responseSHA256=hashlib.sha256(''.join(replies).encode()).hexdigest())
            return replies
        except BaseException as error:
            self.failed = True
            record['error'] = f'{type(error).__name__}: {error}'
            raise
        finally:
            record['elapsedSeconds'] = time.monotonic()-started
            self.records.write((json.dumps(record, sort_keys=True)+'\n').encode())
            self.records.flush()

    def close(self):
        if self.closed:
            return
        self.closed = True
        exit_code, error, terminated = None, None, False
        try:
            if not self.failed:
                self.process.stdin.write(b'qc\r')
                self.process.stdin.flush()
                exit_code = self.process.wait(timeout=5)
                self.process.dispose()
                self._receive(5, final=True)
                if exit_code != 0:
                    raise RuntimeError('Commander close returned an error')
            else:
                terminated = True
                self.process.kill()
                exit_code = self.process.wait(timeout=5)
        except BaseException as failure:
            self.failed = True
            error = f'{type(failure).__name__}: {failure}'
            if self.process.poll() is None:
                terminated = True
                self.process.kill()
                exit_code = self.process.wait(timeout=5)
        finally:
            self.process.dispose()
            self.thread.join(timeout=5)
            self.process.stdin.close()
            self.process.stdout.close()
            self.log.close()
            self.records.close()
            self.reader['write_new'](self.directory/'exit.json', (json.dumps(dict(
                nativeExitCode=exit_code, failed=self.failed, error=error, terminated=terminated,
                noAutomaticReplay=True, implicitStart=False,
                nativeLogSHA256=hashlib.sha256((self.directory/'native.log').read_bytes()).hexdigest()), sort_keys=True)+'\n').encode())
        if error:
            raise RuntimeError(error)
