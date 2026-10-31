"""Read-only fixed-helper transport. No Flash write, reset or App-start command."""
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

BASE, DATA = 0x07FC7C00, 0x07FC8000
BAD = re.compile(r'(?im)^(?!.*J-Link Commander will now exit on Error).*?'
                 r'(Cannot|Could not|Unable to|Failed|ERROR:|Error while|Unspecified error|'
                 r'Unknown command|Illegal register|Syntax:|Syntax error|No ACK|reset via|Connect fallback)')


def verify_executable(executable, manifest):
    """File-only preflight, including the matching DLL; never starts a tool."""
    executable=Path(executable)
    entries=json.loads(Path(manifest).read_bytes())['segger']['files']
    for name in ('JLink.exe','JLink_x64.dll'):
        row=next(item for item in entries if item['file']==name)
        path=executable if name=='JLink.exe' else executable.with_name(name)
        if path.name.lower()!=name.lower() or path.stat().st_size!=row['bytes'] or \
                hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256'].lower():
            raise RuntimeError('Pinned Commander executable/DLL differs; no target accessed')
    return executable


def quote(path):
    value=str(Path(path).resolve())
    if not value.isascii() or any(char in value for char in '\r\n"'):
        raise ValueError('Commander requires an ASCII evidence path without quotes/newlines')
    return '"'+value+'"'


def write_new(path,data):
    with path.open('xb') as stream:stream.write(data)


def read(executable, serial, speed, helper, previous_sequence, address, length, directory, timeout, *, manifest):
    # A literal READ2-only operation; not a generic command/script runner.
    if not str(serial).isdigit() or not 1<=speed<=4000 or not 0<=address<0x40000 or \
            not 1<=length<=4096 or length>0x40000-address or not 0<=previous_sequence<0xFFFFFFFF or timeout<=0:
        raise ValueError('Invalid read-only Commander operation')
    executable=verify_executable(executable,manifest)
    directory=Path(directory)
    for path in (directory,*directory.parents):
        if path.exists() and (path.is_symlink() or getattr(path.lstat(),'st_file_attributes',0)&0x400):
            raise ValueError('Linked readback evidence directory refused')
    directory.mkdir(parents=True,exist_ok=False)
    sequence=previous_sequence+1
    header=struct.pack('<8I',sequence,address,length,0,0,0,0,0)
    expected=struct.pack('<9I',0x31484744,0,sequence,address,length,0,1,length,sequence)
    inputs={'helper.bin':helper,'idle-magic.bin':struct.pack('<II',0x31484744,0),
            'idle-result.bin':struct.pack('<I',1),'idle-sequence.bin':struct.pack('<I',previous_sequence),
            'header.bin':header,'expected.bin':expected}
    for name,data in inputs.items():write_new(directory/name,data)
    commands=['EoE 1','exec SetRestartOnClose = 0','exec SetSkipDebugDeInit = 1',
              'exec DisableFlashDL','exec DisableFlashBPs','connect','halt',
              'mem32 0xE000ED00, 1','mem8 0x50003200, 3',
              f'verifybin {quote(directory/"helper.bin")}, 0x07fc0000',
              f'verifybin {quote(directory/"idle-magic.bin")}, 0x07fc7c00',
              f'verifybin {quote(directory/"idle-result.bin")}, 0x07fc7c18',
              f'verifybin {quote(directory/"idle-sequence.bin")}, 0x07fc7c20',
              f'loadbin {quote(directory/"header.bin")}, 0x07fc7c08, noreset',
              f'verifybin {quote(directory/"header.bin")}, 0x07fc7c08',
              f'savebin {quote(directory/"header-readback.bin")}, 0x07fc7c08, 0x20',
              'w4 0x07fc7c04, 2','g',
              # Fixed helper executes SPI in software. Wait conservatively,
              # then halt once, instead of interrupting every few ms.
              'sleep '+str(max(20,(length+15)//16)), 'halt',
              f'verifybin {quote(directory/"expected.bin")}, 0x07fc7c00',
              f'savebin {quote(directory/"response.bin")}, 0x07fc7c00, 0x28',
              f'savebin {quote(directory/"data.bin")}, 0x07fc8000, 0x{length:x}',
              'mem32 0xE000EDF0, 1','qc']
    script=directory/'read.jlink';write_new(script,('\n'.join(commands)+'\n').encode('ascii'))
    argv=[str(executable),'-NoGui','1','-ExitOnError','1','-SelectEmuBySN',str(serial),
          '-Device','Cortex-M0','-If','SWD','-Speed',str(speed),'-AutoConnect','0','-CommanderScript',str(script)]
    log=directory/'native.log';started=time.monotonic()
    receipt=dict(argv=argv,scriptSHA256=hashlib.sha256(script.read_bytes()).hexdigest(),
                 command=2,sequence=sequence,address=address,bytes=length,nativeExit=None,timedOut=False,
                 flashMutation=False,resetScripted=False,implicitStart=False)
    with log.open('xb') as output:
        process=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        try:receipt['nativeExit']=process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            receipt['timedOut']=True;process.kill();process.wait()
    receipt.update(elapsedSeconds=time.monotonic()-started,nativeLogSHA256=hashlib.sha256(log.read_bytes()).hexdigest())
    write_new(directory/'receipt.json',(json.dumps(receipt,sort_keys=True)+'\n').encode())
    text=log.read_text(encoding='utf8',errors='replace')
    # EoE/exit0 alone is insufficient: 7.88j can print a halt failure and
    # continue. This child contains no Flash mutator; reject before returning
    # bytes or permitting the transaction's next operation. Never replay it.
    if receipt['timedOut'] or receipt['nativeExit']!=0 or BAD.search(text):
        raise RuntimeError(f'Commander read failed/unknown; preserve {directory}; no replay')
    serials=set(re.findall(r'(?im)^\s*S/N:\s*(\d+)\s*$',text))
    halted=re.findall(r'(?im)^\s*E000EDF0\s*=\s*([0-9a-f]+)',text)
    if 'J-Link Commander V7.88j' not in text or serials!={str(serial)} or \
            not re.search(r'(?im)^E000ED00\s*=\s*410CC200\s*$',text) or \
            not re.search(r'(?im)^50003200\s*=\s*35\s+38\s+35\s*$',text) or \
            not halted or not int(halted[-1],16)&0x20000 or 'Script processing completed.' not in text:
        raise RuntimeError('Commander identity/halt/terminal proof incomplete; no replay')
    if (directory/'header-readback.bin').read_bytes()!=header:
        raise RuntimeError('Independent read-command header differs; no replay')
    response=(directory/'response.bin').read_bytes();data=(directory/'data.bin').read_bytes()
    if len(response)!=40 or response[:36]!=expected or len(data)!=length:
        raise RuntimeError('Independent read-command result differs; no replay')
    return data,dict(transport='Commander read-only',sequence=sequence,result=1,outputBytes=length,
                     receipt=str(directory/'receipt.json'),receiptSHA256=hashlib.sha256((directory/'receipt.json').read_bytes()).hexdigest())
