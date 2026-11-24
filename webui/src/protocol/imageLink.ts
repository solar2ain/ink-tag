/** Optional local OS preference. All GATT and product ACKs stay in client.ts. */
export type ImageLinkParameters = {
  intervalMs?: number; latency?: number; tx2M?: boolean; rx2M?: boolean
}
export type ImageLinkDiagnostic = {
  phase: 'acquired' | 'unavailable' | 'renewed' | 'inspected' | 'released'
  reason?: string; parameters?: ImageLinkParameters | null
}
export type ImageLinkLease = { close: () => Promise<void>; sample?: () => Promise<void> }
type DiagnosticSink = (value: ImageLinkDiagnostic) => void
type Capability = { csrfToken: string }
const base = '/api/v1/image-link/'
let capability: Promise<Capability | null> | undefined
let unavailable = false
let unavailableReason = 'capability_unavailable'

class OptionalLinkError extends Error {
  constructor(readonly reason: string) { super(reason) }
}

function parameters(value: any): ImageLinkParameters | null {
  if (!value || typeof value !== 'object') return null
  const result: ImageLinkParameters = {}
  if (typeof value.intervalMs === 'number' && Number.isFinite(value.intervalMs) &&
      value.intervalMs >= 7.5 && value.intervalMs <= 4000) result.intervalMs = value.intervalMs
  if (Number.isInteger(value.latency) && value.latency >= 0 && value.latency <= 499) result.latency = value.latency
  if (typeof value.tx2M === 'boolean') result.tx2M = value.tx2M
  if (typeof value.rx2M === 'boolean') result.rx2M = value.rx2M
  return Object.keys(result).length ? result : null
}

const reasons = new Set(['unsupported_host', 'shutting_down', 'dispatch_timeout', 'lease_missing',
  'native_close_unconfirmed', 'peer_owned', 'lease_limit', 'acquire_timeout', 'native_error',
  'connected_peer_unavailable', 'preference_refused'])
const reason = (value: any, fallback: string) => reasons.has(value) ? value as string : fallback
const errorReason = (error: unknown) => error instanceof OptionalLinkError ? error.reason : 'request_failed'
const report = (sink: DiagnosticSink | undefined, value: ImageLinkDiagnostic) => {
  try { sink?.(value) } catch { /* Diagnostics cannot change a transfer or cleanup. */ }
}

async function jsonRequest(path: string, body?: object, csrfToken?: string, timeout = 1000): Promise<any> {
  const controller = new AbortController()
  let timer: ReturnType<typeof setTimeout> | undefined
  try {
    const deadline = new Promise<never>((_, reject) => {
      timer = setTimeout(() => { controller.abort(); reject(new OptionalLinkError('request_timeout')) }, timeout)
    })
    const response = await Promise.race([fetch(base + path, {
      method: body ? 'POST' : 'GET', credentials: 'same-origin', cache: 'no-store',
      signal: controller.signal, keepalive: path === 'release',
      headers: body ? { 'Content-Type': 'application/json', 'X-InkTag-Image-Link-CSRF': csrfToken! } : undefined,
      body: body ? JSON.stringify(body) : undefined,
    }).then(async (reply) => {
      if (!reply.ok) throw new OptionalLinkError(`http_${reply.status}`)
      return reply.json()
    }), deadline])
    return response
  } finally {
    if (timer !== undefined) clearTimeout(timer)
  }
}

async function localCapability(): Promise<Capability | null> {
  if (unavailable || typeof window === 'undefined' || typeof fetch !== 'function') return null
  if (!['http:', 'https:'].includes(window.location.protocol) ||
      !['127.0.0.1', 'localhost', '[::1]', '::1'].includes(window.location.hostname)) {
    unavailableReason = 'nonlocal_origin'
    return null
  }
  capability ??= jsonRequest('capabilities').then((value) => {
    if (value?.available === true && /^[A-Za-z0-9_-]{32}$/.test(value.csrfToken ?? '')) {
      return { csrfToken: value.csrfToken }
    }
    unavailableReason = reason(value?.reason, 'capability_unavailable')
    return null
  }).catch((error) => { unavailableReason = errorReason(error); return null })
  return capability
}

export async function acquireImageLink(
  address: string, boardId: string, isCurrent: () => boolean,
  diagnostic?: DiagnosticSink,
): Promise<ImageLinkLease | null> {
  if (!isCurrent() || !/^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(address) ||
      address === '00:00:00:00:00:00') {
    report(diagnostic, { phase: 'unavailable', reason: 'unbound_peer' })
    return null
  }
  const supported = await localCapability()
  if (!supported || unavailable || !isCurrent()) {
    report(diagnostic, { phase: 'unavailable', reason: !isCurrent() ? 'stale_binding' : unavailableReason })
    return null
  }
  try {
    const value = await jsonRequest('acquire', { address, boardId }, supported.csrfToken, 3000)
    if (value?.available !== true || !/^[A-Za-z0-9_-]{32}$/.test(value.lease ?? '') ||
        !Number.isFinite(value.ttlMs) || value.ttlMs < 5000 || value.ttlMs > 30000) {
      unavailable = true
      unavailableReason = reason(value?.reason, 'invalid_acquire_reply')
      report(diagnostic, { phase: 'unavailable', reason: unavailableReason, parameters: parameters(value?.parameters) })
      return null
    }
    const token = value.lease as string
    let closed = false
    let closing: Promise<void> | undefined
    let timer: ReturnType<typeof setTimeout> | undefined
    const close = (): Promise<void> => {
      if (closing) return closing
      closed = true
      if (timer !== undefined) clearTimeout(timer)
      window.removeEventListener('pagehide', pageHide)
      closing = jsonRequest('release', { lease: token }, supported.csrfToken)
        .then((reply) => {
          if (reply?.ok !== true) { unavailable = true; unavailableReason = reason(reply?.reason, 'release_unconfirmed') }
          report(diagnostic, { phase: 'released', ...(reply?.ok !== true ? { reason: unavailableReason } : {}) })
        })
        .catch((error) => {
          unavailable = true; unavailableReason = errorReason(error)
          report(diagnostic, { phase: 'released', reason: unavailableReason })
        })
      return closing
    }
    const pageHide = () => { void close() }
    const renew = async () => {
      if (closed) return
      if (!isCurrent()) { await close(); return }
      try {
        const reply = await jsonRequest('renew', { lease: token }, supported.csrfToken)
        if (closed) return
        if (reply?.ok !== true) {
          unavailable = true; unavailableReason = reason(reply?.reason, 'renew_refused')
          report(diagnostic, { phase: 'unavailable', reason: unavailableReason })
          await close(); return
        }
        report(diagnostic, { phase: 'renewed', parameters: parameters(reply.parameters) })
      } catch (error) {
        if (closed) return
        unavailable = true; unavailableReason = errorReason(error)
        report(diagnostic, { phase: 'unavailable', reason: unavailableReason })
        await close()
        return
      }
      if (!closed) timer = setTimeout(() => { void renew() }, value.ttlMs / 3)
    }
    window.addEventListener('pagehide', pageHide, { once: true })
    timer = setTimeout(() => { void renew() }, value.ttlMs / 3)
    if (!isCurrent()) { await close(); return null }
    report(diagnostic, { phase: 'acquired', parameters: parameters(value.parameters) })
    return { close, sample: async () => {
      if (closed || !isCurrent()) return
      try {
        const reply = await jsonRequest('inspect', { lease: token }, supported.csrfToken)
        if (!closed && isCurrent()) report(diagnostic, reply?.ok === true
          ? { phase: 'inspected', parameters: parameters(reply.parameters) }
          : { phase: 'unavailable', reason: reason(reply?.reason, 'inspect_unavailable') })
      } catch (error) {
        if (!closed && isCurrent()) report(diagnostic, { phase: 'unavailable', reason: errorReason(error) })
      }
    } }
  } catch (error) {
    // No repeated native probe or write replay. An unknown acquire result is
    // still bounded by the server TTL; ordinary WebBluetooth remains usable.
    unavailable = true
    unavailableReason = errorReason(error)
    report(diagnostic, { phase: 'unavailable', reason: unavailableReason })
    return null
  }
}
