"""Compact diagnostic literals in a fresh linked GR ELF, before guards/signing.

Only NUL-terminated read-only data outside every function may change. Keep
addresses/lengths and a private reversible catalog; never patch instructions.
"""
import hashlib
import re
import struct
try:
    from goodix_base_elf import Elf
except ModuleNotFoundError:
    from tools.sdk.goodix_base_elf import Elf

SOURCE = re.compile(rb'(?:[A-Za-z]:)?[./\\A-Za-z_][A-Za-z0-9_./\\+\-]*\.(?:c|h|cpp|hpp|s|S|cc|cxx|hh|hxx|inc|inl|asm|lds|ld)\x00')

def sha(data):return hashlib.sha256(data).hexdigest()

def code_hash(elf):
    parts=[]
    for symbol in elf.symbols:
        if symbol['type']!=2 or not symbol['bytes']:continue
        address=symbol['address']&~1
        try:offset=elf.offset(address,symbol['bytes'])
        except ValueError:continue # ROM/library absolute symbols have no file bytes.
        parts.append((address,symbol['bytes'],elf.slice(offset,symbol['bytes'])))
    return sha(b''.join(struct.pack('<II',a,n)+b for a,n,b in sorted(set(parts))))

def readonly(elf,address,length):
    matches=[s for s in elf.loads if s[2]<=address and address+length<=s[2]+s[4]]
    if len(matches)!=1 or matches[0][6]&2:raise ValueError('Diagnostic is not read-only file-backed data')
    for symbol in elf.symbols:
        start=symbol['address']&~1
        if symbol['type']==2 and symbol['bytes'] and start<address+length and address<start+symbol['bytes']:
            raise ValueError('Refusing diagnostic rewrite overlapping machine instructions')
    return elf.offset(address,length)

def discover(elf):
    found={}
    for load in elf.loads:
        for match in SOURCE.finditer(elf.slice(load[1],load[4])):
            address=load[2]+match.start();literal=match.group()[:-1]
            readonly(elf,address,len(literal)+1)
            found[address]=(literal,'source')
    for symbol in elf.symbols:
        if symbol['type']!=1 or not symbol['name'].startswith(('__func__','__FUNCTION__','__PRETTY_FUNCTION__')):continue
        offset=readonly(elf,symbol['address'],symbol['bytes'])
        literal=elf.slice(offset,symbol['bytes'])
        end=literal.find(b'\0')
        if end<0 or any(literal[end:]):raise ValueError('Unknown diagnostic function label')
        found[symbol['address']]=(literal[:end],'function')
    # This exact native table contains descriptions, not protocol responses.
    # Keep its numeric error codes/pointers and fault control flow untouched.
    for symbol in elf.symbols:
        if re.sub(r'\.lto_priv\.\d+$','',symbol['name'])!='s_error_code_info':continue
        if symbol['type']!=1 or symbol['bytes']%8:raise ValueError('Unknown SDK diagnostic table')
        offset=readonly(elf,symbol['address'],symbol['bytes'])
        for i in range(0,symbol['bytes'],8):
            code,address=struct.unpack_from('<II',elf.raw,offset+i)
            position=elf.offset(address,1);end=elf.raw.find(b'\0',position,position+512)
            if end<0:raise ValueError('Unbounded SDK error description')
            literal=elf.raw[position:end];readonly(elf,address,len(literal)+1)
            found[address]=(literal,'error-'+format(code,'04x'))
    return found

def compact(raw):
    elf=Elf(raw);found=discover(elf);changes=[];out=bytearray(raw);tags={}
    if found:
        for symbol in elf.symbols:
            if symbol['name']=='inktag_code_digests':
                table=elf.slice(elf.offset(symbol['address'],symbol['bytes']),symbol['bytes'])
                if any(table):raise ValueError('Relink before compacting a finalized fingerprinted image')
    for address,(literal,kind) in sorted(found.items()):
        if len(literal)<3:raise ValueError('Diagnostic literal too short for a stable tag')
        tag=((('D' if kind=='source' else 'F')+sha(literal)[:min(12,len(literal)-1)])
             if kind in ('source','function') else 'E'+kind[6:]).encode()
        if len(tag)>len(literal):raise ValueError('Diagnostic tag would enlarge the image')
        if tag in tags and tags[tag]!=literal:raise ValueError('Diagnostic tag collision')
        tags[tag]=literal
        replacement=tag+bytes(len(literal)+1-len(tag))
        offset=readonly(elf,address,len(literal)+1)
        if any(offset<c['fileOffset']+c['bytes'] and c['fileOffset']<offset+len(replacement) for c in changes):
            raise ValueError('Overlapping diagnostic strings require review')
        out[offset:offset+len(replacement)]=replacement
        changes.append(dict(address=address,fileOffset=offset,bytes=len(replacement),kind=kind,
            original=literal.decode('ascii'),tag=tag.decode(),replacementSha256=sha(replacement)))
    final=bytes(out)
    if code_hash(Elf(final))!=code_hash(elf):raise ValueError('Diagnostic compaction changed instructions')
    return final,dict(schemaVersion=1,privateDiagnosticCatalog=True,changes=changes,
        machineCodeSha256=code_hash(elf),elfBytes=len(raw))

def verify(raw,record,binary=None):
    elf=Elf(raw)
    if len(raw)!=record['elfBytes'] or code_hash(elf)!=record['machineCodeSha256']:
        raise ValueError('Machine code/ELF extent differs from diagnostic catalog')
    for change in record['changes']:
        offset=readonly(elf,change['address'],change['bytes'])
        if offset!=change['fileOffset'] or sha(elf.slice(offset,change['bytes']))!=change['replacementSha256']:
            raise ValueError('Stale diagnostic catalog')
    indexed={c['address']:c for c in record['changes']}
    for address,(literal,kind) in discover(elf).items():
        change=indexed.get(address)
        if not change or change['kind']!=kind or literal!=change['tag'].encode('ascii'):
            raise ValueError('Uncompacted diagnostic label/description; relink from source')
    if any(SOURCE.search(elf.slice(s[1],s[4])) for s in elf.loads):
        raise ValueError('Source path/filename still present in loadable firmware')
    if binary is not None and binary!=elf.image()[1]:raise ValueError('Diagnostic ELF/BIN mismatch')
    return dict(sourceLiterals=sum(c['kind']=='source' for c in record['changes']),
        errorDescriptions=sum(c['kind'].startswith('error-') for c in record['changes']),
        functionLabels=sum(c['kind']=='function' for c in record['changes']),machineCodeUnchanged=True)

def makefile(text,repo):
    from pathlib import Path
    tool=(Path(repo)/'tools/sdk/compact-goodix-diagnostics.py').as_posix()
    module=(Path(repo)/'tools/sdk/goodix_diagnostic_literals.py').as_posix()
    parser=(Path(repo)/'tools/sdk/goodix_base_elf.py').as_posix()
    link=re.search(r'^\t[^\n]*\$\(V_LINK\)[^\n]*\n',text,re.M)
    if link is None:raise ValueError('Unknown native GR link recipe')
    command='python3 "'+tool+'" --elf '
    text=text[:link.end()]+'\t'+command+'$@ --compact\n'+text[link.end():]
    return text+('\n.DELETE_ON_ERROR:\n$(BUILD_LST)/$(MAKE_TARGET_NAME).elf: '+tool+' '+module+' '+parser+'\n'
        '.PHONY: inktag-diagnostics-verify\nall: inktag-diagnostics-verify\n'
        'inktag-diagnostics-verify: $(BUILD_LST)/$(MAKE_TARGET_NAME).elf $(BUILD)/$(MAKE_TARGET_NAME).bin\n'
        '\t'+command+'$(BUILD_LST)/$(MAKE_TARGET_NAME).elf --bin $(BUILD)/$(MAKE_TARGET_NAME).bin\n')
