"""Browser policy and fail-closed admission for compiled public HTML (stdlib only)."""
from __future__ import annotations

import html
import base64
import hashlib
from html.parser import HTMLParser
import re
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

WEB_CSP = (
    "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; script-src-attr 'none'; "
    "style-src 'self' 'unsafe-inline'; img-src 'self' data: blob: https://tile.openstreetmap.org; "
    "font-src 'self' data:; connect-src 'self' data: blob: https://api.open-meteo.com "
    "https://solar2ain.github.io/ink-tag/updates/; object-src 'none'; frame-src 'none'; "
    "worker-src 'none'; base-uri 'none'; form-action 'none'"
)
MANUAL_CSP = (
    "default-src 'none'; script-src 'none'; script-src-attr 'none'; style-src 'self'; "
    "img-src 'self' https:; object-src 'none'; frame-src 'none'; base-uri 'none'; form-action 'none'"
)


def csp_meta(policy: str = MANUAL_CSP) -> str:
    return '<meta http-equiv="Content-Security-Policy" content="' + html.escape(policy, quote=True) + '">'


def safe_url(value: str, *, image: bool = False) -> bool:
    # Browsers strip ASCII whitespace/control characters from URL schemes.
    if any(ord(c) < 32 or ord(c) == 127 for c in value) or '\\' in value:
        return False
    try:
        parsed = urlsplit(value.strip())
    except ValueError:
        return False
    if parsed.scheme.lower() in {'http', 'https'}:
        return bool(parsed.netloc) and not parsed.username and not parsed.password
    if parsed.scheme.lower() in {'mailto', 'tel'}:
        return not image
    return not parsed.scheme and not parsed.netloc


class CompiledHTML(HTMLParser):
    """Admit only the HTML vocabulary actually needed by the app shell/manuals.

    This is a deployment gate, not a sanitizer for arbitrary browser markup.
    Foreign content, raw-text parsing modes and active embedded documents fail.
    """
    TAGS = frozenset(('html head body meta title div span main nav header footer section article aside '
        'h1 h2 h3 h4 h5 h6 p a img ul ol li dl dt dd pre code blockquote strong em b i s del '
        'table thead tbody tfoot tr th td hr br details summary link script').split())
    COMMON = {'id', 'class', 'title', 'lang', 'dir', 'role'}
    ATTRS = {
        'meta': {'charset', 'name', 'content', 'http-equiv'},
        'script': {'src', 'type', 'crossorigin'},
        'style': {'rel', 'crossorigin', 'type'},
        'link': {'href', 'rel', 'crossorigin', 'type'},
        'a': {'href', 'target', 'rel', 'name'},
        'img': {'src', 'alt', 'width', 'height', 'loading'},
        'ol': {'start'}, 'li': {'value'}, 'th': {'scope', 'colspan', 'rowspan', 'align'},
        'td': {'colspan', 'rowspan', 'align'}, 'details': {'open'},
    }

    def __init__(self, *, app: bool = False, standalone: bool = False):
        super().__init__(convert_charrefs=True)
        self.app = app
        self.standalone = standalone
        self.script_bodies: list[str] = []
        self.policy_values: list[str | None] = []
        self.errors: list[str] = []
        self.references: list[tuple[str, str]] = []
        self.policy_count = 0
        self.script = False
        self.head = False

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if tag not in self.TAGS and not (self.standalone and tag == 'style'):
            self.errors.append('Forbidden HTML element: ' + tag)
        if len(values) != len(attrs):
            self.errors.append('Duplicate HTML attribute: ' + tag)
        for name, value in attrs:
            allowed = self.COMMON | self.ATTRS.get(tag, set()) | ({'style'} if self.standalone else set())
            if name.startswith('on') or name not in allowed and not name.startswith('aria-'):
                self.errors.append('Forbidden HTML attribute: ' + tag + ':' + name)
            if name in {'href', 'src'} and (value is None or not safe_url(value, image=tag == 'img')):
                self.errors.append('Unsafe HTML URL: ' + tag + ':' + name)
        if tag == 'head':
            self.head = True
        is_policy = tag == 'meta' and (values.get('http-equiv') or '').lower() == 'content-security-policy'
        if is_policy:
            self.policy_count += 1
            self.policy_values.append(values.get('content'))
            expected = {MANUAL_CSP, WEB_CSP} if self.app else {MANUAL_CSP}
            if not self.head or not self.standalone and values.get('content') not in expected:
                self.errors.append('Missing/changed browser CSP')
        elif not self.policy_count and not (tag in {'html', 'head'} or tag == 'meta' and set(values) == {'charset'}):
            self.errors.append('Content before browser CSP: ' + tag)
        if tag == 'meta' and 'http-equiv' in values and not is_policy:
            self.errors.append('Forbidden HTTP-equiv (including refresh)')
        if tag == 'script':
            self.script = True
            if self.standalone:
                self.script_bodies.append('')
            src = values.get('src') or ''
            if self.standalone:
                if 'src' in values:
                    self.errors.append('External standalone script')
            elif not self.app or not re.fullmatch(r'(?:\./)?assets/[A-Za-z0-9_.-]+\.js', src) or values.get('type') != 'module':
                self.errors.append('Inline or unapproved script')
            if not self.standalone:
                self.references.append(('script', src))
        if tag == 'link':
            if self.standalone:
                self.errors.append('External standalone stylesheet/resource')
            rel = values.get('rel') or ''
            ref = values.get('href') or ''
            if rel not in {'stylesheet', 'modulepreload'} or not ref or urlsplit(ref).scheme or urlsplit(ref).netloc:
                self.errors.append('Unapproved resource link')
            self.references.append(('resource', ref))

    def handle_startendtag(self, tag, attrs):
        if tag in {'script', 'style'}:
            self.errors.append('Ambiguous self-closing raw-text element')
        self.handle_starttag(tag, attrs)
        self.handle_endtag(tag)

    def handle_endtag(self, tag):
        if tag not in self.TAGS and not (self.standalone and tag == 'style'):
            self.errors.append('Forbidden HTML closing element: ' + tag)
        if tag == 'script':
            self.script = False
        if tag == 'head':
            self.head = False

    def handle_data(self, data):
        if self.script and self.standalone:
            self.script_bodies[-1] += data
        elif self.script and data.strip():
            self.errors.append('Inline script body')
        if not self.policy_count and data.strip():
            self.errors.append('Text before browser CSP')

    def handle_decl(self, decl):
        if decl.lower() != 'doctype html':
            self.errors.append('Unapproved HTML declaration')

    def unknown_decl(self, data):
        self.errors.append('Unknown HTML declaration')

    def handle_pi(self, data):
        self.errors.append('HTML processing instruction')

    def finish(self):
        self.close()
        if self.policy_count != 1:
            self.errors.append('Exactly one early browser CSP required')
        if self.standalone:
            hashes = ["'sha256-" + base64.b64encode(hashlib.sha256(
                body.replace('\r\n', '\n').replace('\r', '\n').encode('utf-8')).digest()).decode('ascii') + "'"
                for body in self.script_bodies]
            expected = WEB_CSP.replace("script-src 'self' 'wasm-unsafe-eval'",
                "script-src 'self' 'wasm-unsafe-eval' " + ' '.join(hashes))
            if not hashes or self.policy_values != [expected]:
                self.errors.append('Standalone script hashes/CSP mismatch')
        return self.errors


def validate_standalone(document: str) -> list[str]:
    parser = CompiledHTML(standalone=True)
    try:
        parser.feed(document)
        return parser.finish()
    except ValueError:
        return ['Invalid standalone HTML']


def validate_svg(data: bytes) -> list[str]:
    # SVG can be navigated as its own document, so HTML checks alone are insufficient.
    if re.search(br'<!|<\?', data, re.I):
        return ['SVG declarations/processing instructions forbidden']
    allowed = set(('svg g path rect circle ellipse line polyline polygon title desc defs '
                   'linearGradient radialGradient stop clipPath mask symbol use').split())
    try:
        root = ET.fromstring(data)
        if root.tag != '{http://www.w3.org/2000/svg}svg':
            return ['Invalid SVG root/namespace']
        for element in root.iter():
            if not element.tag.startswith('{http://www.w3.org/2000/svg}') or element.tag.split('}', 1)[-1] not in allowed:
                return ['Active/unknown SVG element']
            for name, value in element.attrib.items():
                attr = name.split('}')[-1].lower()
                if attr.startswith('on') or attr in {'style', 'base'} or '\\' in value:
                    return ['Active SVG attribute']
                if attr == 'href' and not re.fullmatch(r'#[A-Za-z_][\w.-]*', value):
                    return ['External SVG reference']
                if re.search(r'url\s*\(', value, re.I) and not re.fullmatch(r'url\(#[A-Za-z_][\w.-]*\)', value):
                    return ['External SVG paint reference']
    except (ET.ParseError, ValueError):
        return ['Malformed SVG']
    return []
