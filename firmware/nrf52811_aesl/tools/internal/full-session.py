"""One bounded, exact-identity nRF52811 Full transaction; no BLE or key access.

Transport uses the maintained J-Link Commander 7.88j SWD recipe. Every native
invocation and original result is retained. No failed/unknown write is retried.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import ctypes
import codecs
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import struct
import subprocess
import sys
import time
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parent))
import nrf11_full as full
if os.name=='nt':ctypes.windll.kernel32.GetTickCount64.restype=ctypes.c_ulonglong

WORKER_SHA='d3e34c2be735e84c484198b95c9b10c92798a20c1abacfbb8af3e3a26b854acf'
# Exact nRF52811 physical RAM and POWER registers, SDK17.1 MDK nrf52811.h /
# nrf52811_bitfields.h. RESETREAS fields are write-one-to-clear. Other bits
# are reserved and are never written. This is host Full policy, not App code.
USER_RAM_START=0x20000000
USER_RAM_BYTES=24*1024
RESETREAS=0x40000400
RESETREAS_MASK=0x0005000f
RETENTION_REGISTERS=(('gpregret',0x4000051c),('gpregret2',0x40000520))
BAD=re.compile(r'(?i)cannot|could not|\bfailed\b|error while|\berror:|unknown (?:command|register)|invalid register|syntax error|timeout|timed out')

def emit(kind,**fields):
    # Presentation channel only. Native bytes and the result classifier never
    # consume these events or the terminal's cleaned/backspace-rendered text.
    print('INKTAG_FULL_EVENT '+json.dumps(dict(kind=kind,**fields)),flush=True)

class NativeProgress:
    def __init__(self,label,commands):
        self.started=time.monotonic();self.last_tick=self.started
        self.state=dict(label=label,action='Connecting / waiting',seconds=0)
        self.streams={};self.last=None;self.read=0;self.total=0
        for command in commands:
            match=re.fullmatch(r'SaveBin "[^"]+", (?:0x[0-9a-fA-F]+|\d+), (0x[0-9a-fA-F]+|\d+)',command)
            if match:self.total+=int(match[1],16)
    def progress(self,force=False):
        self.state['seconds']=int(time.monotonic()-self.started)
        current=tuple(sorted(self.state.items()))
        if force or current!=self.last:emit('progress',**self.state);self.last=current
    def line(self,raw,display,complete):
        if complete:
            emit('native',text=raw)
            if BAD.search(raw):emit('message',level='ERROR',text=raw)
        matches=list(re.finditer(r'(?i)(read|writ|program|eras|verif|compar)[^\r\n]*?(?<![-\d.])\b([0-9]{1,3}(?:\.\d+)?)\s*%',display))
        if matches:
            value=float(matches[-1][2])
            if 0<=value<=100:
                self.state=dict(label=self.state['label'],action=matches[-1][1],percent=value)
                self.progress()
        read=re.fullmatch(r'Reading (\d+) bytes from addr 0x[0-9a-fA-F]+ into file\.\.\.O\.K\.',display)
        if complete and read and self.total and int(read[1])<=self.total-self.read:
            self.read+=int(read[1])
            self.state=dict(label=self.state['label'],action='Native read transfer',done=self.read,total=self.total,percent=100*self.read/self.total)
            self.progress()
    def feed(self,stream,text,final=False):
        state=self.streams.setdefault(stream,dict(raw='',display='',cr=False))
        for char in text:
            if char in '\r\n':
                if not(char=='\n' and state['cr']) and state['raw']:self.line(state['raw'],state['display'],True)
                state.update(raw='',display='',cr=char=='\r')
            else:
                state['cr']=False;state['raw']+=char
                if char=='\b':state['display']=state['display'][:-1]
                else:
                    state['display']+=char
                    if char=='%':self.line(state['raw'],state['display'],False)
        if final and state['raw']:
            self.line(state['raw'],state['display'],True);state.update(raw='',display='')
    def tick(self):
        if time.monotonic()-self.last_tick>=1:
            self.last_tick=time.monotonic();self.progress(True)

def stamp():
    return dict(utc=datetime.now(timezone.utc).isoformat(),monotonicNs=time.monotonic_ns(),
                tickMs=ctypes.windll.kernel32.GetTickCount64() if os.name=='nt' else None)

def save(path,value):
    with Path(path).open('x',encoding='utf-8') as f:json.dump(value,f,indent=2);f.write('\n')

def read_exact(path,length):
    path=Path(path)
    data=path.read_bytes()
    if len(data)!=length:raise ValueError(f'Incomplete readback: {path.name}: {len(data)} != {length}')
    return data

def check_mailbox(raw,command,address,length,nonce,input_crc,output):
    if len(raw)!=48:raise ValueError('Incomplete RAM-worker mailbox; outcome unknown, no replay')
    values=struct.unpack('<12I',raw)
    expected=(0x3157524e,command,address,length,input_crc,nonce,nonce,0,0x454e4f44,0xa14013)
    if values[:10]!=expected or values[10]!=zlib.crc32(output)&0xffffffff or values[11]:
        raise ValueError(f'RAM-worker did not confirm exact operation: {values}; no replay')

def commander_commands(commands,checks=()):
    # Closing Commander defaults to Go. Set the DLL policy before target
    # connection so success, ExitOnError and process cleanup cannot add a
    # second resume after a worker BKPT or start App before final verification.
    return ['ExitOnError 1','Exec SetRestartOnClose = 0','connect','halt',
            *checks,*commands,'qc']

class Session:
    def __init__(self,args,root):
        self.args=args;self.root=root;self.counter=0
        self.worker=Path(__file__).parent.parent/'assets/full-writer.bin'
        worker=self.worker.read_bytes()
        if len(worker)!=1176 or full.sha(worker)!=WORKER_SHA:raise ValueError('RAM worker binding mismatch; no target access')
        self.worker=root/'worker.bin';self.worker.write_bytes(worker)
        self.native_checked=False;self.anchor=None
    def invoke(self,label,commands):
        emit('native',text='J-Link operation: '+label)
        if not self.native_checked:
            if os.name!='nt':raise ValueError('Native Full transport is available on Windows only')
            if Path(self.args.jlink).resolve()!= (Path(self.args.jlink).parent/'JLink.exe').resolve():
                raise ValueError('The executable must be the pinned JLink.exe; no target access')
            lock=json.loads((Path(__file__).parent.parent/'full-dependencies.lock.json').read_bytes())
            native=[]
            for row in lock['native']['files']:
                p=Path(self.args.jlink).parent/row['name']
                if not p.is_file() or full.sha(p.read_bytes())!=row['sha256']:raise ValueError('Pinned J-Link 7.88j file mismatch; no target access')
                native.append(dict(file=str(p),sha256=row['sha256']))
            save(self.root/'native-inputs.json',native);self.native_checked=True
        self.counter+=1
        stem=f'{self.counter:02d}-{label}'
        script=self.root/(stem+'.jlink')
        for value in [str(script),str(self.root)]:
            if len(os.path.abspath(value))>=250 or not value.isascii() or any(c in value for c in '\r\n"'):
                raise ValueError('J-Link 7.88j needs a short ASCII evidence path; choose -OutputDir')
        checks=[]
        if self.anchor is not None:
            for name,address,expected in self.anchor:
                p=self.root/name
                if p.read_bytes()!=expected:raise ValueError('Immutable identity anchor file changed; no native invocation')
                checks.append(f'VerifyBin "{p}", 0x{address:x}')
        script.write_text('\n'.join(commander_commands(commands,checks))+'\n',encoding='ascii')
        argv=[self.args.jlink,'-NoGui','1','-ExitOnError','1','-Device','nRF52811_xxAA','-If','SWD','-Speed',str(self.args.frequency//1000),
              '-SelectEmuBySN',self.args.serial,'-AutoConnect','0','-CommanderScript',str(script)]
        record=dict(argv=argv,start=stamp(),exitCode=None,exception=None,
                    closePolicy='SetRestartOnClose=0 before explicit target connect')
        stdout=self.root/(stem+'.stdout.log');stderr=self.root/(stem+'.stderr.log')
        try:
            with stdout.open('xb',buffering=0) as out,stderr.open('xb',buffering=0) as err:
                process=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                progress=NativeProgress(label,commands);progress.progress()
                streams=[dict(path=p,offset=0,closed=False,decoder=codecs.getincrementaldecoder('utf-8')('replace')) for p in (stdout,stderr)]
                def drain(final=False):
                    for stream in streams:
                        if stream['closed']:continue
                        with stream['path'].open('rb') as reader:
                            reader.seek(stream['offset']);chunk=reader.read();stream['offset']+=len(chunk)
                        text=stream['decoder'].decode(chunk,final);stream['closed']=final
                        progress.feed(stream['path'].name,text,final)
                deadline=time.monotonic()+self.args.timeout
                try:
                    while True:
                        remaining=deadline-time.monotonic()
                        if remaining<=0:raise subprocess.TimeoutExpired(argv,self.args.timeout)
                        try:
                            record['exitCode']=process.wait(timeout=min(.1,remaining));break
                        except subprocess.TimeoutExpired:
                            drain();progress.tick()
                    drain(True)
                except BaseException:
                    try:
                        process.kill();process.wait()
                    finally:
                        try:drain(True)
                        except (BrokenPipeError,OSError):pass
                    raise
        except BaseException as error:record['exception']=repr(error);raise
        finally:
            record['end']=stamp()
            for n,p in [('stdout',stdout),('stderr',stderr)]:
                if p.exists():record[n]=dict(file=p.name,bytes=p.stat().st_size,sha256=full.sha(p.read_bytes()))
            save(self.root/(stem+'.json'),record)
        text=stdout.read_text(encoding='utf-8',errors='replace')+'\n'+stderr.read_text(encoding='utf-8',errors='replace')
        volts=[float(v) for v in re.findall(r'(?i)(?:VTref|VTarget)\s*[=:]\s*(\d+(?:\.\d+)?)\s*V',text)]
        if record['exitCode']!=0 or BAD.search(text) or not volts or min(volts)<2.7 or max(volts)>3.6:
            raise ValueError(f'J-Link {label} failed/unknown or VTref outside 2.7–3.6V; no replay. Raw logs: {stdout}')
        return text
    def snapshot(self,label):
        ficr=self.root/(label+'-ficr.bin');uicr=self.root/(label+'-uicr.bin')
        self.invoke(label,[f'SaveBin "{ficr}", 0x10000000, 0x1000',f'SaveBin "{uicr}", 0x10001000, 0x1000'])
        f,u=read_exact(ficr,4096),read_exact(uicr,4096)
        return f,u,full.identity(f)
    def external(self,command,address,length,payload=None):
        token=secrets.randbits(32) or 1
        label=f'external-{command}-{address:05x}'
        prefix=f'{self.counter+1:02d}-{label}'
        mailbox=self.root/(prefix+'-input.bin');response=self.root/(prefix+'-mailbox.bin');output=self.root/(prefix+'-data.bin')
        crc=zlib.crc32(payload)&0xffffffff if payload is not None else 0
        mailbox.write_bytes(struct.pack('<12I',0x3157524e,command,address,length,crc,token,0,0,0,0,0,0))
        # Set the core's advertised Cortex-M register names while halted,
        # before any Go. ExitOnError aborts if the pinned Commander rejects a
        # register write. The blob's naked entry independently starts CPSID I.
        commands=['WReg PRIMASK, 0x1','WReg CONTROL, 0x0','WReg XPSR, 0x01000000',
            f'loadbin "{self.worker}", 0x20000000',f'loadbin "{mailbox}", 0x20001000']
        if payload is not None:
            if command!=2 or len(payload)!=4096:raise ValueError('Invalid external write payload')
            source=self.root/(prefix+'-source.bin');source.write_bytes(payload)
            commands.append(f'loadbin "{source}", 0x20002000')
        out_len=4096 if command in (1,2) else 118*4
        commands += ['WReg MSP, 0x20005F00','SetPC 0x20000000','RReg PRIMASK','RReg CONTROL','RReg XPSR','Go','WaitHalt 30000',
            f'SaveBin "{response}", 0x20001000, 0x30',f'SaveBin "{output}", 0x20002000, 0x{out_len:x}']
        self.invoke(label,commands)
        data=read_exact(output,out_len)
        check_mailbox(read_exact(response,48),command,address,length,token,crc,data)
        if payload is not None and data!=payload:raise ValueError('External write readback differs; no reset/replay')
        if command==3 and data!=bytes(out_len):raise ValueError('Image heads not invalidated; no reset/replay')
        return data
    def clear_volatile_user_data(self,expected_flash):
        # All worker operations/readbacks must be finished: clearing physical
        # RAM also destroys the worker and its stack. No Go until final Reset.
        if not self.anchor:raise ValueError('Exact live identity anchor required before volatile cleanup')
        registers=(('resetreas',RESETREAS,RESETREAS_MASK),
                   *((name,address,0xff) for name,address in RETENTION_REGISTERS))
        before={};commands=[]
        for name,address,mask in registers:
            path=self.root/(name+'-before-clear.bin')
            commands.append(f'SaveBin "{path}", 0x{address:x}, 0x4')
        self.invoke('read-volatile-user-state',commands)
        for name,address,mask in registers:
            value=struct.unpack('<I',read_exact(self.root/(name+'-before-clear.bin'),4))[0]
            if value & ~mask:raise ValueError('Invalid reset/retention read; not an empty state')
            before[name]=value
        zeros=self.root/'user-ram-zero.bin';readback=self.root/'user-ram-readback.bin'
        zeros.write_bytes(bytes(USER_RAM_BYTES))
        commands=[f'loadbin "{zeros}", 0x{USER_RAM_START:x}',
                  f'VerifyBin "{zeros}", 0x{USER_RAM_START:x}',
                  *[f'W4 0x{address:x} 0x0' for _,address in RETENTION_REGISTERS],
                  f'W4 0x{RESETREAS:x} 0x{RESETREAS_MASK:x}',
                  f'SaveBin "{readback}", 0x{USER_RAM_START:x}, 0x{USER_RAM_BYTES:x}']
        for name,address,mask in registers:
            commands.append(f'SaveBin "{self.root/(name+"-after-clear.bin")}", 0x{address:x}, 0x4')
        boot=self.root/'boot-metadata-after-clear.bin'
        commands.append(f'SaveBin "{boot}", 0x2e000, 0x2000')
        self.invoke('clear-volatile-user-state',commands)
        if read_exact(readback,USER_RAM_BYTES)!=bytes(USER_RAM_BYTES):
            raise ValueError('Volatile user RAM clear not verified; no reset/replay')
        for name,address,mask in registers:
            if read_exact(self.root/(name+'-after-clear.bin'),4)!=bytes(4):
                raise ValueError('Reset/retention clear not verified; no reset/replay')
        image=read_exact(expected_flash,0x30000)
        if read_exact(boot,8192)!=image[0x2e000:0x30000]:
            raise ValueError('Verified boot metadata changed during cleanup; no reset')
        result=dict(ramStart=USER_RAM_START,ramBytes=USER_RAM_BYTES,ramReadbackSha256=full.sha(bytes(USER_RAM_BYTES)),
                    resetReasonBefore=before['resetreas'],retentionBefore={name:before[name] for name,_ in RETENTION_REGISTERS},
                    previousResetFlagsCleared=True,retentionCleared=True,bootMetadataPreserved=True,
                    resetReasonAfterClear=0,
                    note='Final Reset may add a new reset reason; it is not previous reset history.')
        save(self.root/'volatile-user-data.json',result)
        return result

def run(args):
    if not args.serial.isdecimal() or args.frequency<1000 or args.frequency%1000 or not 1<=args.timeout<=86400:
        raise ValueError('Invalid exact probe/frequency/timeout')
    # All supplied files are bound before opening a probe; existing data is never overwritten.
    memory,files,review=full.verify_bundle(args.firmware);configuration=full.read_config(args.config)
    full.require_config_admission(review,configuration)
    if review['packageSha256']!=args.expected_package_sha or configuration['sha256']!=args.expected_config_sha:
        raise ValueError('Confirmed firmware/config changed; no target access')
    root=Path(args.output).resolve();root.mkdir(parents=True,exist_ok=False)
    firmware=root/'app-full.zip';config=root/('board'+Path(args.config).suffix)
    shutil.copyfile(args.firmware,firmware);shutil.copyfile(args.config,config)
    if full.sha(firmware.read_bytes())!=review['packageSha256'] or full.read_config(config)['sha256']!=configuration['sha256']:
        raise ValueError('Confirmed package/config changed; no target access')
    s=Session(args,root)
    save(root/'session-start.json',dict(start=stamp(),firmware=review,config=full.read_config(config),
        scope='Full clears native settings/GUI/image heads; preserves FICR/UICR, low-Flash holes and external reserved sectors; no backup/BLE/time sync'))
    try:
        emit('stage',name='COMPLETE FLASH / EXACT TARGET AND NECESSARY PRESERVATION')
        ficr,uicr,ident=s.snapshot('identity')
        emit('message',level='INFO',text=f"Detected nRF52811: 192 KiB Flash / 24 KiB RAM; device ID {ident['deviceId']}")
        s.anchor=[('identity-ficr.bin',0x10000000,ficr),('identity-uicr.bin',0x10001000,uicr)]
        spans={}
        for address,length in full.missing_low_ranges(memory):
            p=root/f'low-{address:05x}.bin'
            s.invoke('preserve-low',[f'SaveBin "{p}", 0x{address:x}, 0x{length:x}'])
            spans[(address,length)]=read_exact(p,length)
        reserved=b''.join(s.external(1,a,4096) for a in range(0x5000,0xa000,4096))
        (root/'reserved-before.bin').write_bytes(reserved)
        emit('stage',name='COMPLETE FLASH / PREPARE CURRENT DEFAULTS')
        plan=full.prepare(firmware,config,ficr,spans,root/'plan')
        emit('stage',name='COMPLETE FLASH / PRE-WRITE IDENTITY AND VOLTAGE')
        f,u,i=s.snapshot('before-write')
        if f!=ficr or u!=uicr or i!=ident:raise ValueError('Identity/UICR changed before programming')
        prepared=root/'plan/prepared.hex'
        emit('stage',name='COMPLETE FLASH / PROGRAM MAIN FLASH')
        text=s.invoke('program-mcu',[f'loadfile "{prepared}"'])
        if not re.search(r'(?i)Downloading file|Flash contents already match',text):raise ValueError('MCU programming was not confirmed; no replay')
        emit('stage',name='COMPLETE FLASH / WRITE SETTINGS AND GUI RESOURCES')
        journal=(root/'plan/settings.bin').read_bytes();assets=files['gui-assets.bin']
        # Retire the second generation before committing the new first page.
        for a,payload in [(4096,journal[4096:]),(0,journal[:4096]),*[(0x2000+n,assets[n:n+4096]) for n in range(0,12288,4096)]]:
            s.external(2,a,4096,payload)
        emit('stage',name='COMPLETE FLASH / INVALIDATE OLD IMAGE HEADERS')
        s.external(3,0xa000,0x76000)
        emit('stage',name='COMPLETE FLASH / INDEPENDENT WHOLE FLASH READBACK')
        actual=root/'flash-readback.bin'
        s.invoke('readback-mcu',[f'SaveBin "{actual}", 0x0, 0x30000'])
        if read_exact(actual,0x30000)!=(root/'plan/flash.bin').read_bytes():raise ValueError('Complete MCU readback differs')
        f,u,i=s.snapshot('after-write')
        if f!=ficr or u!=uicr or i!=ident:raise ValueError('FICR/UICR preservation failed; no reset')
        for base,expected in [(0,journal),(0x2000,assets),(0x5000,reserved)]:
            readback=b''.join(s.external(1,a,4096) for a in range(base,base+len(expected),4096))
            (root/f'external-readback-{base:05x}.bin').write_bytes(readback)
            if readback!=expected:raise ValueError('External independent readback differs; no reset')
        if s.external(4,0xa000,0x76000)!=bytes(472):raise ValueError('Independent image-head check failed; no reset')
        emit('stage',name='COMPLETE FLASH / CLEAR VOLATILE DIAGNOSTICS')
        cleared_user_state=s.clear_volatile_user_data(root/'plan/flash.bin')
        emit('stage',name='COMPLETE FLASH / GUARDED RESET')
        s.invoke('verified-reset',['Reset','Go'])
        save(root/'result.json',dict(status='verified-full-write',end=stamp(),identity=ident,firmware=review,
            noAutomaticBackup=True,noBle=True,noTimeWrite=True,oldEpochCompared=False,
            imagePixelsSecurelyErased=False,appRuntimeTested=False,
            clearedVolatileUserData=cleared_user_state))
        emit('message',level='SUCCESS',text='Congratulations! Complete programming and readback verified.')
        emit('message',level='INFO',text=f"Bluetooth: {ident['bleName']}; Recovery: {ident['bleName']}_OTA.")
        emit('message',level='INFO',text='First startup is configured to show the device-information page.')
    except BaseException as error:
        save(root/'failure.json',dict(end=stamp(),error=repr(error),replayed=False,automaticReset=False))
        raise

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for n in ('firmware','config','jlink','serial','output','expected-package-sha','expected-config-sha'):parser.add_argument('--'+n,required=True)
    parser.add_argument('--frequency',type=int,default=500000);parser.add_argument('--timeout',type=int,default=180)
    try:run(parser.parse_args())
    except Exception as error:emit('message',level='ERROR',text=str(error));sys.exit(1)
