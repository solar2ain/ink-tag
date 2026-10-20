"""Explicit user-document and notice inventory; no source-tree discovery."""
from __future__ import annotations

from html.parser import HTMLParser
import html
import os
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

GUIDES = ('cli', 'devices', 'faq', 'flashing', 'getting-started', 'github-pages',
          'host-pages', 'ota', 'pro', 'releases', 'supported-devices', 'updates', 'webui')
SOURCE_DOCUMENTS = ('README.md', 'README.en.md', 'SECURITY.md', 'RELEASE_NOTES.md',
    'cli/README.md', 'cli/README.en.md',
    *(f'docs/{language}/{name}.md' for language in ('zh-CN', 'en') for name in GUIDES),
    *(f'firmware/{prefix}{name}' for prefix in ('', 'goodix/', 'onmicro/', 'telink/', 'nordic/', 'dialog/')
      for name in ('README.md', 'README.en.md')))
# These files are maintained separately from ordinary user guides. Exporters
# preserve them unless their exact old bytes have been acknowledged explicitly.
LICENSING_DOCUMENTS = ('LICENSING.md', 'THIRD_PARTY_NOTICES.md', 'release-materials.md',
    'cli/LICENSING.md', 'cli/THIRD_PARTY_NOTICES.md')
NOTICE_FILES = ('CLI-AGPL-3.0-only.txt', 'Dexie-Apache-2.0.txt', 'Fabric-MIT.txt',
    'Leafer-MIT.txt', 'Lucide-LICENSE.txt', 'MaShanZheng-OFL.txt', 'React-MIT.txt',
    'RgbQuant-MIT.txt', 'dexie-4.4.5-LICENSE.txt', 'dexie-4.4.5-NOTICE.txt',
    'epdoptimize-Apache-2.0.txt', 'leafer-ui-2.2.10-LICENSE.txt',
    'react-19.2.8-LICENSE.txt', 'react-dom-19.2.8-LICENSE.txt',
    'react-router-7.18.2-LICENSE.txt', 'LICENSING.txt', 'THIRD_PARTY_NOTICES.txt',
    'release-materials.txt')


def document_map() -> dict[str, str]:
    mapping = {}
    for name in (*SOURCE_DOCUMENTS, *LICENSING_DOCUMENTS):
        target = name[5:] if name.startswith('docs/') else name
        mapping[name] = 'manual/' + str(Path(target).with_suffix('.html')).replace('\\', '/')
    return mapping


def confined(root: Path, path: Path) -> Path:
    resolved = path.resolve()
    if not resolved.is_relative_to(root.resolve()) or path.is_symlink():
        raise ValueError('Document path leaves the public directory')
    return resolved


def rewrite_reference(repo: Path, source: Path, target: Path, reference: str) -> str:
    parts = urlsplit(html.unescape(reference))
    if parts.scheme or parts.netloc or not parts.path:
        return reference
    if '\\' in unquote(parts.path):
        raise ValueError('Backslash in document reference')
    original = confined(repo, source.parent / unquote(parts.path))
    relative = original.relative_to(repo.resolve()).as_posix()
    mapping = document_map()
    if relative in mapping:
        output = repo/'webui'/mapping[relative]
    elif relative == 'cli/LICENSE':
        output = repo/'webui/licenses/CLI-AGPL-3.0-only.txt'
    elif relative.startswith('webui/'):
        output = original
    elif relative == 'cli/licenses/EPDOPTIMIZE-LICENSE.txt':
        output = repo/'webui/licenses/epdoptimize-Apache-2.0.txt'
    elif relative == 'cli/licenses/COMPILED-RESOURCES.txt':
        output = repo/'webui/manual/release-materials.html'
    else:
        raise ValueError('Unlisted local document reference: '+relative)
    confined(repo/'webui', output)
    result = os.path.relpath(output, target.parent).replace('\\', '/')
    return result + ('?'+parts.query if parts.query else '') + ('#'+parts.fragment if parts.fragment else '')


def render_documents(repo: Path) -> dict[str, bytes]:
    import markdown
    from markdown.extensions.toc import slugify_unicode
    output = {}
    for name, target_name in document_map().items():
        source, target = repo/name, repo/'webui'/target_name
        body = markdown.markdown(source.read_text('utf-8'), extensions=['tables', 'fenced_code','toc'],
                                 extension_configs={'toc':{'slugify':slugify_unicode}})
        def rewrite(match):
            value = rewrite_reference(repo, source, target, match[2])
            return match[1]+'="'+html.escape(value, quote=True)+'"'
        body = re.sub(r'\b(href|src)="([^"]*)"', rewrite, body)
        language = 'en' if name.endswith('.en.md') or name.startswith('docs/en/') else 'zh-CN'
        css = os.path.relpath(repo/'webui/manual/style.css', target.parent).replace('\\', '/')
        home = os.path.relpath(repo/'webui/index.html', target.parent).replace('\\', '/')
        text = ('<!doctype html>\n<html lang="'+language+'"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<link rel="stylesheet" href="'+css+'"><title>InkTag manual</title></head>'
            '<body><nav><a href="'+home+'">InkTag WebUI</a></nav><main>'+body+'</main></body></html>\n')
        output[target_name] = text.encode('utf-8')
    return output


class References(HTMLParser):
    def __init__(self):
        super().__init__()
        self.references = []
        self.ids = set()
    def handle_starttag(self, tag, attrs):
        for name, value in attrs:
            if name=='id' or tag=='a' and name=='name':self.ids.add(value)
            if name in {'href', 'src'} and value:
                self.references.append((tag, name, value))


def validate_html_links(site: Path, pages=None) -> list[str]:
    site = site.resolve()
    problems = []
    for path in pages if pages is not None else site.rglob('*.html'):
        parser = References()
        parser.feed(path.read_text('utf-8'))
        for tag, attr, value in parser.references:
            parts = urlsplit(value)
            if parts.scheme in {'http', 'https', 'mailto', 'tel', 'data', 'blob'} or parts.netloc:
                continue
            if parts.scheme or '\\' in unquote(parts.path):
                problems.append('Unsafe HTML reference: '+path.relative_to(site).as_posix())
                continue
            target = path if not parts.path else (site/unquote(parts.path).lstrip('/')) if parts.path.startswith('/') else path.parent/unquote(parts.path)
            try:
                candidate = confined(site, target)
                if candidate.is_dir(): candidate = candidate/'index.html'
                if not candidate.is_file(): raise ValueError('missing')
                if parts.fragment and candidate.suffix=='.html' and candidate.is_relative_to(site/'manual'):
                    linked=References();linked.feed(candidate.read_text('utf-8'))
                    if unquote(parts.fragment) not in linked.ids:raise ValueError('missing anchor')
            except ValueError:
                problems.append('Missing/out-of-site '+attr+': '+path.relative_to(site).as_posix()+': '+value)
    return problems


def notice_bytes(repo: Path) -> dict[str, bytes]:
    # Never replace independently reviewed license text during packaging.
    return {'THIRD_PARTY_NOTICES.txt': (repo/'webui/THIRD_PARTY_NOTICES.txt').read_bytes(),
            **{'licenses/'+name: (repo/'webui/licenses'/name).read_bytes() for name in NOTICE_FILES}}


def user_web_files(repo: Path) -> dict[str, bytes]:
    site = repo/'webui'
    result = {}
    for path in site.rglob('*'):
        if not path.is_file(): continue
        name = path.relative_to(site).as_posix()
        if name.startswith(('manual/', 'licenses/')): continue
        if path.is_symlink(): raise ValueError('Links cannot be packaged')
        result[name] = path.read_bytes()
    result.update(notice_bytes(repo))
    result.update({name: (site/name).read_bytes() for name in document_map().values()})
    result['manual/style.css'] = (site/'manual/style.css').read_bytes()
    if (site/'manual/index.html').is_file(): result['manual/index.html'] = (site/'manual/index.html').read_bytes()
    for language in ('zh-CN','en'):
        result['manual/'+language+'/index.html']=(site/'manual'/language/'index.html').read_bytes()
    links = ''.join('<li><a href="'+html.escape(name, quote=True)+'">'+html.escape(name)+'</a></li>' for name in NOTICE_FILES)
    result['licenses/index.html'] = ('<!doctype html><html lang="zh-CN"><meta charset="utf-8">'
        '<title>InkTag licensing</title><h1>许可与第三方声明</h1><ul>'+links+'</ul></html>\n').encode()
    return result
