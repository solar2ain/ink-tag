/** Bounded host timing only. No packet bytes, identifiers, timers or BLE calls. */
export type ImageTimingPhase = 'requestQueue' | 'writeQueue' | 'attWrite' | 'reply' | 'handler' | 'dispatchGap'
export type ImageTimingSink = (phase: ImageTimingPhase, elapsedMs: number, atMs: number) => void

export function imageDocumentVisibility() {
  return typeof document === 'undefined' ? 'unavailable' : document.visibilityState
}

/** Existing TLSR GET_STATUS v4 suffix, not a new command or generic layout. */
export function decodeTlsrNotifyCounters(payload: Uint8Array) {
  if (payload.length < 27 || payload[0] !== 1 || payload[16] !== 4) return null
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  return { lastStatus: payload[21], retries: view.getUint16(22, true),
    drops: view.getUint16(24, true), pending: payload[26] }
}

export function observeImageTiming(sink: ImageTimingSink | undefined, phase: ImageTimingPhase,
  elapsedMs: number, atMs = performance.now()) {
  try { sink?.(phase, elapsedMs, atMs) } catch { /* Never alter ACK/error/cleanup. */ }
}

export class ImageTransferTiming {
  private readonly stats = new Map<ImageTimingPhase, { count: number; total: number; max: number }>()
  private lastReplyAt: number | undefined

  readonly record: ImageTimingSink = (phase, ms, at) => {
    if (!Number.isFinite(ms) || ms < 0 || !Number.isFinite(at)) return
    if (phase === 'requestQueue' && this.lastReplyAt !== undefined) {
      this.add('dispatchGap', Math.max(0, at - ms - this.lastReplyAt))
      this.lastReplyAt = undefined
    }
    if (phase === 'reply') this.lastReplyAt = at
    this.add(phase, ms)
  }

  private add(phase: ImageTimingPhase, ms: number) {
    const value = this.stats.get(phase) ?? { count: 0, total: 0, max: 0 }
    value.count++; value.total += ms; value.max = Math.max(value.max, ms)
    this.stats.set(phase, value)
  }

  summary() {
    return (['requestQueue', 'writeQueue', 'attWrite', 'reply', 'handler', 'dispatchGap'] as const)
      .map((phase) => {
        const value = this.stats.get(phase)
        return `${phase}=${value ? `${(value.total / value.count).toFixed(1)}/${value.max.toFixed(1)}ms,n=${value.count}` : 'unavailable'}`
      }).join(' ')
  }
}
