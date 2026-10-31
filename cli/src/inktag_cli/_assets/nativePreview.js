/* Host-only adapter to the shipping C GUI. No BLE, device writes or layout math. */
(() => {
  'use strict'
  const decode = (s) => {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
    const out = []; let bits = 0, value = 0
    for (const c of s) {
      if (c === '=') break
      const n = chars.indexOf(c); if (n < 0) throw Error('Invalid renderer resource')
      value = value << 6 | n; bits += 6
      if (bits >= 8) { bits -= 8; out.push(value >>> bits & 255) }
    }
    return Uint8Array.from(out)
  }
  const bytes = () => decode(globalThis.InkTagNativePreviewWasm)
  function create(module) {
    const unsupported = () => { throw Error('Renderer attempted an unsupported host operation') }
    const instance = new WebAssembly.Instance(module || new WebAssembly.Module(bytes()), {
      wasi_snapshot_preview1: { proc_exit: unsupported, fd_close: unsupported,
        fd_write: unsupported, fd_seek: unsupported, environ_sizes_get: unsupported, environ_get: unsupported },
      env: { emscripten_notify_memory_growth: () => {} },
    })
    const e = instance.exports
    if (e._initialize) e._initialize()
    const allocate = (input) => {
      const p = e.malloc(input.byteLength)
      if (!p) throw Error('Renderer allocation failed')
      new Uint8Array(e.memory.buffer).set(input, p)
      return p
    }
    function configure(screen, page = 'clock') {
      const pages = { calendar: 1, clock: 2, 'device-info': 3 }
      if (!(page in pages)) throw Error('Unknown native page')
      if (screen.transform !== undefined && (!Number.isInteger(screen.transform) || screen.transform < 0 || screen.transform > 15))
        throw Error('Invalid screen transform')
      if (screen.transform !== undefined && (screen.rotation !== undefined || screen.mirrors !== undefined))
        throw Error('Choose transform or rotation/mirrors, not both')
      const v = [screen.width, screen.height, pages[page], screen.rotation ?? ((screen.transform ?? 0) & 3), screen.mirrors ?? ((screen.transform ?? 0) >> 2),
        screen.nativeUiScale ?? 1, screen.visibleX ?? 0, screen.visibleY ?? 0,
        screen.visibleWidth ?? screen.width, screen.visibleHeight ?? screen.height,
        screen.timestamp ?? 1792112400, screen.weekStart ?? 1, screen.color ?? 2,
        screen.pro ? 1 : 0, screen.brandHidden ? 1 : 0, screen.temperature ?? 24, screen.voltage ?? 3000, 0]
      if (v.some((n) => !Number.isSafeInteger(n))) throw Error('Invalid screen parameters')
      if (v.some((n, i) => i !== 10 && (n < -2147483648 || n > 2147483647)) || v[10] < 0 || v[10] > 4294967295)
        throw Error('Screen parameters exceed the C ABI')
      const a = new Int32Array(v), p = allocate(new Uint8Array(a.buffer))
      try { if (!e.nf_config(p)) throw Error('Unsupported screen geometry or transform') }
      finally { e.free(p) }
    }
    function region(kind) {
      const p = e.nf_region(kind)
      if (!p) return null
      const v = new Int32Array(e.memory.buffer, p, 12)
      return { x: v[0], y: v[1], width: v[2], height: v[3],
        physical: { x: v[4], y: v[5], width: v[6], height: v[7] },
        digits: v[8], logicalWidth: v[9], logicalHeight: v[10] }
    }
    function regions(screen) {
      configure(screen, 'clock'); const clock = region(1)
      configure(screen, 'calendar'); const calendar = region(2)
      if (!clock) throw Error('No clock digit area for this screen')
      return { clock, calendar }
    }
    function defaultSizes(screen) {
      const areas = regions(screen)
      // Same host raster cell convention as raster_large_digits; layout bounds
      // are queried from C, never recalculated here. U8g2 has signed 8-bit metrics.
      const fit = (area) => {
        for (let h = Math.min(127, area.height); h >= 1; h--) {
          const w = Math.max(1, Math.floor(h * 5 / 8))
          const padding = Math.min(Math.max(0, Math.floor((w - 1) / 2)), Math.max(1, Math.floor(h / 32)))
          const advance = w + padding, colon = Math.max(1, Math.floor(w / 2))
          if (area.digits * advance + (area.digits === 4 ? colon : 0) <= area.width) return h
        }
        throw Error('No native digit size fits the reserved area')
      }
      const large = fit(areas.clock)
      return { small: areas.calendar ? fit(areas.calendar) : Math.max(1, Math.floor(large / 2)), large, areas }
    }
    function render(screen, page, pkg, strings) {
      const b = Uint8Array.from(pkg || []), p = b.length ? allocate(b) : 0
      try { if (!e.nf_package(p, b.length)) throw Error('C renderer rejected the font package') }
      finally { if (p) e.free(p) }
      // Caller provides UTF-8 bytes to avoid browser-only TextEncoder in the CLI VM.
      for (let i = 0; i < 5; i++) {
        const s = Uint8Array.from(strings?.[i] || []), q = allocate(s.length ? s : new Uint8Array(1))
        try { if (!e.nf_string(i, q, s.length)) throw Error('Preview label too long') }
        finally { e.free(q) }
      }
      configure(screen, page)
      const frame = e.nf_render()
      if (!frame) throw Error('Native page cannot fit this font package or screen')
      return { width: screen.width, height: screen.height,
        pixels: Array.from(new Uint8Array(e.memory.buffer, frame, screen.width * screen.height)) }
    }
    return { regions, defaultSizes, render }
  }
  globalThis.InkTagNativePreview = Object.freeze({ bytes, create })
})()
