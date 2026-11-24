import { createHash } from 'node:crypto'
import { JSDOM } from 'jsdom'

export const webCsp = "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; script-src-attr 'none'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob: https://tile.openstreetmap.org; font-src 'self' data:; connect-src 'self' data: blob: https://api.open-meteo.com https://solar2ain.github.io/ink-tag/updates/ https://raw.githubusercontent.com/solar2ain/ink-tag/; object-src 'none'; frame-src https://api.map.baidu.com https://map.baidu.com https://uri.amap.com https://ditu.amap.com https://www.amap.com https://m.amap.com https://www.openstreetmap.org; worker-src 'none'; base-uri 'none'; form-action 'none'"
const policyTag = /<meta\b[^>]*http-equiv="Content-Security-Policy"[^>]*>/gi

export function standaloneCsp(html) {
  const document = new JSDOM(html).window.document
  const hashes = [...document.scripts].filter(script => !script.src).map(script =>
    `'sha256-${createHash('sha256').update(script.textContent).digest('base64')}'`)
  if (!hashes.length) throw new Error('Missing standalone script')
  return webCsp.replace("script-src 'self' 'wasm-unsafe-eval'", `script-src 'self' 'wasm-unsafe-eval' ${hashes.join(' ')}`)
}

export function sealStandalone(html) {
  if ([...html.matchAll(policyTag)].length !== 1) throw new Error('Exactly one CSP required before standalone sealing')
  return html.replace(policyTag, () => `<meta http-equiv="Content-Security-Policy" content="${standaloneCsp(html)}">`)
}

export function verifyHtml(html, standalone = false) {
  const document = new JSDOM(html).window.document
  const policies = [...document.querySelectorAll('meta[http-equiv]')]
    .filter(meta => meta.httpEquiv.toLowerCase() === 'content-security-policy')
  const expected = standalone ? standaloneCsp(html) : webCsp
  if (policies.length !== 1 || policies[0].content !== expected || policies[0].parentElement !== document.head) {
    throw new Error('Missing/changed browser CSP')
  }
  // Check source order as well: DOM parsing can move late head metadata.
  const offset = html.search(/<meta\b[^>]*http-equiv="Content-Security-Policy"/i)
  if (offset < 0 || /<(?:script|link|style|body|iframe|svg|img|base)\b/i.test(html.slice(0, offset))) throw new Error('Content before CSP')
  for (const element of document.querySelectorAll('*')) {
    const tag = element.localName
    if (['iframe', 'object', 'embed', 'base', 'form', 'input', 'button', 'svg', 'math', 'template', 'noscript', 'xmp', 'plaintext'].includes(tag)) throw new Error(`Active HTML: ${tag}`)
    for (const { name, value } of element.attributes) {
      if (/^on/i.test(name) || ['srcdoc', 'srcset', 'ping', 'action', 'formaction'].includes(name)) throw new Error(`Active HTML attribute: ${name}`)
      if (['href', 'src'].includes(name) && /^(?:javascript|vbscript|data|blob):|^[\s\S]*[\x00-\x20\\]/i.test(value)) throw new Error('Unsafe HTML URL')
    }
    if (tag === 'meta' && element.httpEquiv && element.httpEquiv.toLowerCase() !== 'content-security-policy') throw new Error('HTTP-equiv forbidden')
    if (tag === 'style' && !standalone) throw new Error('Inline style forbidden in static HTML')
    if (tag === 'script') {
      if (standalone ? element.hasAttribute('src') : !/^(?:\.\/)?assets\/[A-Za-z0-9_.-]+\.js$/.test(element.getAttribute('src') || '') || element.textContent.trim()) throw new Error('Unapproved script')
    }
  }
}
