"""Offline pairing of compiled WebUI and CLI; never build/download at startup."""
from __future__ import annotations

import hashlib
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re

from .generated_commands_v2 import PROTOCOL_V2_VERSION


class _Index(HTMLParser):
    def __init__(self):
        super().__init__(); self.meta = {}; self.scripts = []

    def handle_starttag(self, tag, attrs):
        row = dict(attrs)
        if tag == 'meta' and row.get('name', '').startswith('inktag-'):
            key = row['name']
            if key in self.meta: raise ValueError('Duplicate WebUI build metadata')
            self.meta[key] = row.get('content', '')
        if tag == 'script' and row.get('type') == 'module' and row.get('src'):
            self.scripts.append(row['src'])


def source_build_id(repo: Path) -> str | None:
    """Same declarative inputs as Vite; source-only check, absent in user packages."""
    root = repo / 'webui'; spec = root / 'tools/client-build-inputs.json'
    if not spec.is_file(): return None
    inputs = json.loads(spec.read_text(encoding='utf-8')); digest = hashlib.sha256()
    source = root / 'src'
    excluded = {(root / name).resolve() for name in inputs.get('excludedDirectories', [])}
    text_extensions = {'.ts', '.tsx', '.js', '.mjs', '.mts', '.json', '.html', '.css', '.txt'}
    def input_bytes(path):
        raw = path.read_bytes()
        return raw.replace(b'\r\n', b'\n') if inputs.get('normalizeTextLineEndings') and path.suffix.lower() in text_extensions else raw
    def tree(directory):
        for path in sorted(directory.iterdir(), key=lambda p: p.name):
            if path.resolve() in excluded: continue
            if path.is_dir(): tree(path)
            else:
                digest.update(os.path.relpath(path, source).replace('\\', '/').encode('utf-8'))
                digest.update(input_bytes(path))
    for directory in inputs['directories']: tree(root / directory)
    for item in inputs['files']:
        digest.update(item['name'].encode('utf-8')); digest.update(input_bytes(root / item['path']))
    return 'web-' + digest.hexdigest()[:20]


_VERSION = re.compile(r'Object\.freeze\(\{version:[^,]{1,32},major:\d+,minor:\d+,patch:\d+,protocolVersion:(0x[0-9a-fA-F]+|\d+),otaVersion:')


def inspect_webui_build(root: Path, *, repo_root: Path | None = None,
                        require_metadata: bool = False, entry: str = 'index.html') -> dict:
    root = root.resolve(); html = (root / entry).read_text(encoding='utf-8')
    parser = _Index(); parser.feed(html)
    bodies = [html] if not parser.scripts else []
    for name in parser.scripts:
        # Only ordinary packaged module entries, never execute JS or fetch URLs.
        if not name.startswith(('./assets/', 'assets/')) or any(c in name for c in '?%#\\'):
            raise ValueError('WebUI module is not a local compiled asset')
        original = root / name
        path = original.resolve()
        if not path.is_relative_to(root) or original.is_symlink() or path.stat().st_size > 32*1024*1024:
            raise ValueError('Invalid WebUI module asset')
        bodies.append(path.read_text(encoding='utf-8'))
    protocols = {int(m.group(1), 0) for body in bodies for m in _VERSION.finditer(body)}
    compiled_protocols = protocols.copy()
    declared = parser.meta.get('inktag-protocol-version')
    if declared is not None:
        if not declared.isdecimal(): raise ValueError('Invalid WebUI protocol metadata')
        protocols.add(int(declared))
    if protocols and protocols != {PROTOCOL_V2_VERSION}:
        actual = '/'.join(f'0x{x:02x}' for x in sorted(protocols))
        raise ValueError(f'本地 WebUI 与 CLI 不匹配（WebUI {actual}，CLI 0x{PROTOCOL_V2_VERSION:02x}）。'
                         '请更新本地客户端或重新构建 WebUI；无需因此重刷设备。')
    build = parser.meta.get('inktag-client-build')
    if build is not None and (not re.fullmatch(r'web-[a-f0-9]{20}', build) or
                              not any(build in body for body in bodies)):
        raise ValueError('WebUI 页面与脚本来自不同构建；请更新完整客户端包。')
    expected = source_build_id(repo_root) if repo_root is not None else None
    if expected is not None and build != expected:
        raise ValueError('WebUI 源码已更新，静态页面尚未同步。请在 webui 目录执行 npm run build；'
                         '普通用户请更新完整客户端包，无需重刷设备。')
    if require_metadata and (declared is None or build is None or compiled_protocols != {PROTOCOL_V2_VERSION}):
        raise ValueError('WebUI 构建信息缺失；请重新构建后再打包。')
    return dict(buildId=build, protocolVersion=next(iter(protocols)) if protocols else None,
                metadataVerified=declared is not None and build is not None and compiled_protocols == {PROTOCOL_V2_VERSION})
