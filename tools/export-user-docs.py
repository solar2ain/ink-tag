#!/usr/bin/env python3
"""Copy the explicit documentation snapshot; never publish or discover repositories."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from client_content import SOURCE_DOCUMENTS, LICENSING_DOCUMENTS, NOTICE_FILES


def export(source: Path, destination: Path, *, execute=False, expected_licenses=None):
    source, destination = source.resolve(), destination.resolve()
    protected = (*LICENSING_DOCUMENTS, 'LEGAL_REVIEW_REQUIRED.md', 'cli/LICENSE',
                 'cli/licenses/COMPILED-RESOURCES.txt', 'cli/licenses/EPDOPTIMIZE-LICENSE.txt',
                 'webui/THIRD_PARTY_NOTICES.txt', *('webui/licenses/'+n for n in NOTICE_FILES))
    names = (*SOURCE_DOCUMENTS, *protected)
    previous = expected_licenses or {}
    plan = []
    for name in dict.fromkeys(names):
        original, target = source/name, destination/name
        if not original.is_file() or original.is_symlink() or not original.resolve().is_relative_to(source):
            raise ValueError('Missing/unsafe explicit document: '+name)
        if target.is_symlink() or not target.resolve().is_relative_to(destination):
            raise ValueError('Unsafe documentation destination')
        body = original.read_bytes()
        if name in protected and target.is_file() and target.read_bytes()!=body:
            if previous.get(name)!=hashlib.sha256(target.read_bytes()).hexdigest():
                raise ValueError('Preserve changed independent license; explicit old hash required: '+name)
        plan.append((name,body))
    if execute:
        for name,body in plan:
            target=destination/name;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(body)
    return {name:hashlib.sha256(body).hexdigest() for name,body in plan}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--destination',type=Path,required=True)
    parser.add_argument('--expected-licenses',type=Path)
    parser.add_argument('--execute',action='store_true',help='Copy files locally; never push or publish.')
    args=parser.parse_args()
    expected=json.loads(args.expected_licenses.read_text('utf-8')) if args.expected_licenses else None
    result=export(args.source,args.destination,execute=args.execute,expected_licenses=expected)
    print(json.dumps({'copied':args.execute,'files':result},indent=2))

if __name__=='__main__':
    main()
