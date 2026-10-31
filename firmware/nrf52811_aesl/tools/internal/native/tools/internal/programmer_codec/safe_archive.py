"""Bounded, unambiguous OTA ZIP input. Pure offline; publisher checks stay in callers."""
from __future__ import annotations

import json
import math
from pathlib import Path
import stat
import struct
import zipfile

MAX_MEMBER_BYTES = 4 * 1024 * 1024
MAX_MANIFEST_BYTES = 64 * 1024
MAX_ARCHIVE_BYTES = 16 * 1024 * 1024
MAX_MEMBERS = 64
MAX_DIRECTORY_BYTES = 128 * 1024


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate OTA JSON field')
        result[key] = value
    return result


def _constant(_value):
    raise ValueError('non-finite number in OTA JSON')


class OtaArchive(zipfile.ZipFile):
    """Check metadata before decompression; read to a hard bound as well."""
    def __init__(self, path):
        archive_size = Path(path).stat().st_size
        if archive_size > MAX_ARCHIVE_BYTES:
            raise ValueError('OTA ZIP exceeds archive size limit')
        # ZipFile constructs every central-directory object before infolist().
        # Bound that metadata allocation too; current OTA packages are small,
        # single-disk non-ZIP64 archives, not general-purpose containers.
        with Path(path).open('rb') as raw:
            start=max(0,archive_size-65557);raw.seek(start);tail=raw.read(65557)
        eocd=tail.rfind(b'PK\x05\x06')
        if eocd<0 or eocd+22>len(tail):raise ValueError('OTA ZIP end record is missing')
        _,disk,directory_disk,disk_count,count,directory_size,directory_offset,comment_size=struct.unpack_from('<4s4H2IH',tail,eocd)
        if (disk or directory_disk or disk_count!=count or count>MAX_MEMBERS or
                directory_size>MAX_DIRECTORY_BYTES or directory_offset+directory_size!=start+eocd or
                eocd+22+comment_size!=len(tail)):
            raise ValueError('invalid OTA ZIP directory offset/size/count')
        super().__init__(path, 'r')
        try:
            entries = self.infolist()
            if len(entries) > MAX_MEMBERS or sum(i.file_size for i in entries) > MAX_ARCHIVE_BYTES:
                raise ValueError('OTA ZIP exceeds member count/total size limit')
            seen = set()
            for item in entries:
                if item.header_offset < 0 or item.header_offset + 30 > archive_size or item.compress_size > archive_size:
                    raise ValueError('OTA ZIP local header/data offset is outside archive')
                name = item.orig_filename
                components = name.rstrip('/').split('/')
                if (not name or '\\' in name or ':' in name or '\0' in name or
                        any(p in {'', '.', '..'} for p in components) or
                        name.casefold() in seen or stat.S_ISLNK(item.external_attr >> 16)):
                    raise ValueError('unsafe or duplicate OTA ZIP member')
                seen.add(name.casefold())
                limit = MAX_MANIFEST_BYTES if name == 'manifest.json' else MAX_MEMBER_BYTES
                if item.file_size > limit or item.flag_bits & 1:
                    raise ValueError('OTA ZIP member exceeds size limit or is encrypted')
        except BaseException:
            self.close()
            raise

    def read(self, name, pwd=None):
        item = name if isinstance(name, zipfile.ZipInfo) else self.getinfo(name)
        if item.is_dir():
            raise ValueError('OTA ZIP member is a directory')
        limit = MAX_MANIFEST_BYTES if item.filename == 'manifest.json' else MAX_MEMBER_BYTES
        with self.open(item, pwd=pwd) as stream:
            data = stream.read(limit + 1)
        if len(data) > limit or len(data) != item.file_size:
            raise ValueError('OTA ZIP member exceeds decoded size limit')
        return data

    def manifest(self):
        try:
            value = json.loads(self.read('manifest.json').decode('utf-8-sig'), object_pairs_hook=_object, parse_constant=_constant)
        except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
            raise ValueError('invalid OTA JSON manifest') from error
        if not isinstance(value, dict):
            raise ValueError('OTA JSON manifest must be an object')
        pending=[(value,1)]
        while pending:
            node,depth=pending.pop()
            if isinstance(node,float) and not math.isfinite(node):
                raise ValueError('non-finite number in OTA JSON')
            if isinstance(node,(dict,list)):
                if depth>32:raise ValueError('OTA JSON nesting exceeds limit')
                children=node.values() if isinstance(node,dict) else node
                pending.extend((child,depth+1) for child in children)
        root=value.get('manifest',value)
        if not isinstance(root,dict):raise ValueError('OTA manifest wrapper must be an object')
        for section in ('application','signature'):
            if section in root and not isinstance(root[section],dict):
                raise ValueError('OTA '+section+' must be an object')
        return value
