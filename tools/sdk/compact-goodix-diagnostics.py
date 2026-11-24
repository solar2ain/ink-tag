"""Fresh-link diagnostic data compaction; no signing, hardware or instruction edits."""
import argparse,json
from pathlib import Path
from goodix_diagnostic_literals import compact,verify

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',type=Path,required=True);p.add_argument('--bin',type=Path)
    p.add_argument('--compact',action='store_true');args=p.parse_args()
    raw=args.elf.read_bytes();catalog=args.elf.with_suffix('.diagnostics.json')
    if args.compact:
        final,record=compact(raw)
        temporary=args.elf.with_suffix('.diagnostics.tmp');temporary.write_bytes(final);temporary.replace(args.elf)
        catalog.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');raw=final
    record=json.loads(catalog.read_text(encoding='utf-8'))
    result=verify(raw,record,args.bin.read_bytes() if args.bin else None)
    print('Diagnostics verified:',result['sourceLiterals'],'source literals,',result['errorDescriptions'],
        'error descriptions,',result['functionLabels'],'function labels; instructions unchanged')

if __name__=='__main__':main()
