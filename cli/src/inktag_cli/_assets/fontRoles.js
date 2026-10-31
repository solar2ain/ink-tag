/* Shared host-only font compiler for WebUI and Python MiniRacer. No BLE or IO. */
(() => {
  'use strict'
  const fail = (message) => { throw new Error(message) }
  const be16 = (b, p) => b[p] * 256 + b[p + 1]
  const le32 = (b, p) => (b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24) >>> 0
  const set16 = (b, p, n) => { b[p] = n & 255; b[p + 1] = n >>> 8 }
  const set32 = (b, p, n) => { for (let i = 0; i < 4; i++) b[p + i] = n >>> (8 * i) & 255 }
  const bytes = (input) => {
    if (!input || input.length > 2 * 1024 * 1024) fail('Font source size is invalid')
    const b = Array.from(input)
    if (b.some((n) => !Number.isInteger(n) || n < 0 || n > 255)) fail('Invalid font byte')
    return b
  }
  const crc = (b) => {
    let value = 0xffffffff
    for (const byte of b) {
      value ^= byte
      for (let i = 0; i < 8; i++) value = value & 1 ? value >>> 1 ^ 0xedb88320 : value >>> 1
    }
    return (value ^ 0xffffffff) >>> 0
  }
  function decodeFont(input, exactRuns = false) {
    const b = bytes(input), glyphs = new Map(), starts = new Set(); let pixelBudget = 0
    if (b.length < 27 || b.slice(2, 9).some((n) => n < 1 || n > 8) || b[4] > 7 || b[5] > 7 ||
        b[9] > 127 || b[10] > 127 || b[13] > 127 || b[15] > 127) fail('Invalid U8g2 header')
    const unicode = 23 + be16(b, 21)
    if (unicode < 25 || unicode + 4 > b.length) fail('Invalid Unicode directory')
    const parse = (position, prefix, cp) => {
      const length = b[position + prefix - 1], end = position + length
      if (length <= prefix || end > b.length || glyphs.has(cp)) fail('Invalid glyph record')
      starts.add(position)
      let bit = (position + prefix) * 8
      const read = (count) => {
        if (bit + count > end * 8) fail('Truncated glyph bitstream')
        let value = 0
        for (let i = 0; i < count; i++, bit++) value |= (b[bit >> 3] >> (bit & 7) & 1) << i
        return value
      }
      const signed = (count) => read(count) - 2 ** (count - 1)
      const width = read(b[4]), height = read(b[5]), x = signed(b[6]), y = signed(b[7]), dx = signed(b[8])
      if (width > b[9] || height > b[10] || dx < 0 || width && !height) fail('Unbounded glyph metrics')
      pixelBudget += width * height
      if (glyphs.size >= 16384 || pixelBudget > 8 * 1024 * 1024) fail('Font source exceeds the bounded host workspace')
      const pixels = Array(width * height).fill(0)
      let cursor = 0
      while (width && cursor < pixels.length) {
        const zero = read(b[2]), one = read(b[3])
        if (!zero && !one) fail('Non-progressing glyph RLE')
        let repeat
        do {
          cursor += zero
          for (let i = 0; i < one; i++, cursor++) if (cursor < pixels.length) pixels[cursor] = 1
          repeat = read(1)
          if (exactRuns && cursor > pixels.length) fail('Glyph RLE escapes its cell')
          if (cursor >= pixels.length && repeat) fail('Glyph RLE repeats beyond its cell')
        } while (repeat)
      }
      glyphs.set(cp, { codePoint: cp, width, height, x, y, dx, pixels })
      return end
    }
    let p = 23, previous = 0
    while (p + 1 < unicode && b[p + 1]) {
      const cp = b[p]
      if (!cp || cp <= previous) fail('Unsorted ASCII glyphs')
      previous = cp; p = parse(p, 2, cp)
    }
    if (p + 2 !== unicode || b[p] || b[p + 1]) fail('Invalid ASCII terminator')
    for (const offset of [be16(b, 17), be16(b, 19)]) if (offset && !starts.has(23 + offset)) fail('Invalid ASCII shortcut')
    const tableBytes = be16(b, unicode)
    if (tableBytes < 4 || tableBytes % 4 || unicode + tableBytes + 2 > b.length) fail('Invalid Unicode lookup length')
    const blocks = [], ends = []; let block = unicode
    for (p = unicode; p < unicode + tableBytes; p += 4) {
      block += be16(b, p); const last = be16(b, p + 2)
      if (block < unicode + tableBytes || block + 2 > b.length ||
          blocks.length && block <= blocks.at(-1) || ends.length && last <= ends.at(-1)) fail('Invalid Unicode lookup')
      blocks.push(block); ends.push(last)
    }
    if (ends.at(-1) !== 65535) fail('Unicode lookup lacks final sentinel')
    p = unicode + tableBytes; previous = 255
    const unicodeStarts = new Map()
    while (p + 1 < b.length && be16(b, p)) {
      const cp = be16(b, p)
      if (cp <= previous) fail('Unsorted Unicode glyphs')
      previous = cp; unicodeStarts.set(p, cp); p = parse(p, 3, cp)
    }
    if (p + 2 > b.length || b.slice(p).some((n) => n) || b.length - p > 3) fail('Invalid Unicode terminator')
    blocks.forEach((start, i) => {
      if (start !== p && !unicodeStarts.has(start)) fail('Unicode shortcut is not a glyph boundary')
      if (start !== p && unicodeStarts.get(start) > ends[i]) fail('Unicode shortcut range is invalid')
    })
    let bucket = 0
    for (const [position, cp] of unicodeStarts) {
      while (bucket + 1 < blocks.length && position >= blocks[bucket + 1]) bucket++
      if (cp > ends[bucket] || bucket && cp <= ends[bucket - 1]) fail('Unicode glyph is in the wrong lookup bucket')
    }
    if ((glyphs.size & 255) !== b[0]) fail('Glyph count differs from its header')
    return { glyphs, header: b.slice(0, 23) }
  }
  function encodeFont(glyphs, referenceHeader, runWidths) {
    const sorted = [...glyphs.values()].sort((a, b) => a.codePoint - b.codePoint)
    if (!sorted.length) fail('Empty font')
    const maximum = (name) => Math.max(...sorted.map((g) => g[name]))
    const unsignedBits = (n) => Math.max(1, Math.ceil(Math.log2(n + 1)))
    const signedBits = (name) => {
      const values = sorted.map((g) => g[name])
      for (let bits = 1; bits <= 8; bits++) if (Math.min(...values) >= -(2 ** (bits - 1)) && Math.max(...values) < 2 ** (bits - 1)) return bits
      fail('Signed glyph metrics do not fit')
    }
    const bits = [runWidths?.[0] || referenceHeader?.[2] || 4, runWidths?.[1] || referenceHeader?.[3] || 3,
      unsignedBits(maximum('width')), unsignedBits(maximum('height')), signedBits('x'), signedBits('y'), signedBits('dx')]
    const record = (g) => {
      const payload = []; let bit = 0
      const add = (count, value) => {
        if (!Number.isInteger(value) || value < 0 || value >= 2 ** count) fail('Glyph metrics do not fit')
        for (let i = 0; i < count; i++, bit++) {
          const index = bit >> 3
          if (payload.length <= index) payload.push(0)
          payload[index] |= (value >> i & 1) << (bit & 7)
        }
      }
      if (g.codePoint < 32 || g.codePoint > 65534 || g.pixels.length !== g.width * g.height) fail('Invalid glyph')
      add(bits[2], g.width); add(bits[3], g.height)
      add(bits[4], g.x + 2 ** (bits[4] - 1)); add(bits[5], g.y + 2 ** (bits[5] - 1)); add(bits[6], g.dx + 2 ** (bits[6] - 1))
      let pos = 0
      let last
      const emit = (zero, one) => {
        if (last && zero === last[0] && one === last[1]) add(1, 1)
        else { if (last) add(1, 0); add(bits[0], zero); add(bits[1], one) }
        last = [zero, one]
      }
      while (pos < g.pixels.length) {
        let zero = 0, one = 0
        while (pos < g.pixels.length && !g.pixels[pos]) { zero++; pos++ }
        while (pos < g.pixels.length && g.pixels[pos]) { one++; pos++ }
        const zeroMax = 2 ** bits[0] - 1, oneMax = 2 ** bits[1] - 1
        while (zero > zeroMax) { emit(zeroMax, 0); zero -= zeroMax }
        while (one > oneMax) { emit(zero, oneMax); zero = 0; one -= oneMax }
        if (zero || one) emit(zero, one)
      }
      if (last) add(1, 0)
      const prefix = g.codePoint <= 255 ? [g.codePoint, 2 + payload.length] : [g.codePoint >> 8, g.codePoint & 255, 3 + payload.length]
      if (prefix.length + payload.length > 255) fail('Glyph exceeds U8g2 record size')
      return [...prefix, ...payload]
    }
    const ascii = sorted.filter((g) => g.codePoint <= 255).flatMap(record)
    const unicode = sorted.filter((g) => g.codePoint > 255)
    const chunks = []
    for (let i = 0; i < unicode.length; i += 101) chunks.push(unicode.slice(i, i + 101))
    if (!chunks.length) chunks.push([])
    const blocks = chunks.map((chunk) => chunk.flatMap(record)), lookup = []
    chunks.forEach((chunk, i) => {
      const delta = i ? blocks[i - 1].length : chunks.length * 4
      const end = i === chunks.length - 1 ? 65535 : chunk.at(-1).codePoint
      if (delta > 65535) fail('Unicode block exceeds its lookup range')
      lookup.push(delta >> 8, delta & 255, end >> 8, end & 255)
    })
    const minX = Math.min(...sorted.map((g) => g.x)), minY = Math.min(...sorted.map((g) => g.y))
    const top = Math.max(...sorted.map((g) => g.y + g.height))
    const right = Math.max(...sorted.map((g) => g.x + g.width))
    if (!referenceHeader && (right - minX > 127 || top - minY > 127 || top < 0 || top > 127 || minY > 0)) fail('Font bounds exceed the U8g2 metric format')
    const header = [sorted.length & 255, 0, ...bits, referenceHeader ? maximum('width') : right - minX,
      referenceHeader ? maximum('height') : top - minY, minX & 255, minY & 255, top, minY & 255, top, minY & 255, 0, 0, 0, 0,
      (ascii.length + 2) >> 8, (ascii.length + 2) & 255]
    if (referenceHeader) for (let i = 9; i < 17; i++) header[i] = referenceHeader[i]
    const out = [...header, ...ascii, 0, 0, ...lookup, ...blocks.flat(), 0, 0]
    decodeFont(out)
    return out
  }
  function fitGlyph(source, reference) {
    const pixels = Array(reference.width * reference.height).fill(0)
    if (source.width && source.height && reference.width && reference.height) {
      const scale = Math.min(reference.width / source.width, reference.height / source.height)
      const width = Math.max(1, Math.floor(source.width * scale)), height = Math.max(1, Math.floor(source.height * scale))
      const left = Math.floor((reference.width - width) / 2), top = Math.floor((reference.height - height) / 2)
      for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
        /* Integrate source coverage over the final pixel before binarizing.
         * Integer overlap coordinates make browser/CLI results identical;
         * nearest-neighbour sampling can drop an entire thin row or column. */
        const x0 = x * source.width, x1 = (x + 1) * source.width
        const y0 = y * source.height, y1 = (y + 1) * source.height
        let coverage = 0
        for (let sy = Math.floor(y0 / height); sy < Math.ceil(y1 / height); sy++) {
          const overlapY = Math.min(y1, (sy + 1) * height) - Math.max(y0, sy * height)
          for (let sx = Math.floor(x0 / width); sx < Math.ceil(x1 / width); sx++) {
            const overlapX = Math.min(x1, (sx + 1) * width) - Math.max(x0, sx * width)
            const value = source.pixels[sy * source.width + sx]
            coverage += (source.coverage8 ? value : value * 255) * overlapX * overlapY
          }
        }
        pixels[(top + y) * reference.width + left + x] = coverage >= 96 * source.width * source.height ? 1 : 0
      }
    }
    return { ...reference, pixels }
  }
  const isNumber = (cp) => cp >= 48 && cp <= 57
  const isLatin = (cp) => cp >= 32 && cp <= 126 && !isNumber(cp) || [176, 215, 177, 183].includes(cp)
  const isChinese = (cp) => cp === 8451 || cp >= 0x3000
  const chineseRequired = [8451,19968,19969,19971,19975,19977,19985,19993,20013,20057,20061,20108,20116,20133,20146,20154,20241,20799,20803,20820,20826,20843,20845,20891,20908,20998,21021,21160,21171,21313,21320,21359,21608,22235,22269,22307,22764,22788,22799,22805,22812,22823,22825,22836,22899,22919,23376,23433,23477,23493,23506,23567,24049,24051,24072,24179,24180,24198,24218,24314,24319,24681,24773,24778,24858,24863,25098,25100,25260,25945,26085,26086,26126,26143,26149,26257,26376,26377,26399,26410,26641,26893,27491,27597,27700,28165,28385,29238,29275,29399,29482,29492,29677,30002,30003,30328,30333,31163,31179,31181,31435,31461,31471,32650,33098,33267,33410,33426,34382,34503,34544,34915,35806,35895,36763,36784,36824,37193,37325,38384,38451,38477,38500,38632,38634,38684,38706,38738,39532,40481,40736,40857]
  function requiredCharacters(role, additional = '') {
    if (typeof additional !== 'string') fail('Additional characters must be text')
    const result = role === 'numbers' ? Array.from({ length: 10 }, (_, n) => n + 48)
      : role === 'latin' ? [...Array.from({ length: 95 }, (_, n) => n + 32).filter(isLatin), 176,177,183,215]
        : role === 'chinese' ? chineseRequired : fail('Unknown font role')
    return [...new Set([...result, ...Array.from(additional, (c) => c.codePointAt(0))])]
  }
  function sourceFace(choice, size) {
    if (choice[size]) return decodeFont(choice[size])
    const input = choice[`${size}Glyphs`], glyphs = new Map(); let budget = 0
    if (!Array.isArray(input) || input.length > 16384) fail('Both role source sizes are required')
    for (const g of input) {
      const coverage8 = g && g.coverage !== undefined
      const pixels = coverage8 ? g.coverage : g && g.pixels
      if (!g || !Number.isInteger(g.codePoint) || g.codePoint < 32 || g.codePoint > 65534 || glyphs.has(g.codePoint) ||
          !['width','height','x','y','dx'].every((name) => Number.isInteger(g[name])) ||
          g.width < 0 || g.width > 127 || g.height < 0 || g.height > 127 ||
          g.x < -128 || g.x > 127 || g.y < -128 || g.y > 127 || g.dx < 0 || g.dx > 127 ||
          coverage8 && g.pixels !== undefined || !Array.isArray(pixels) || pixels.length !== g.width * g.height ||
          pixels.some((n) => !Number.isInteger(n) || n < 0 || n > (coverage8 ? 255 : 1))) fail('Invalid rasterized role source')
      budget += pixels.length
      if (budget > 8 * 1024 * 1024) fail('Raster source exceeds the bounded host workspace')
      glyphs.set(g.codePoint, { ...g, pixels, coverage8 })
    }
    return { glyphs }
  }
  function referenceCell(cp, target, small) {
    const h = target.header, original = target.glyphs.get(cp)
    if (original) return original
    const latin = isLatin(cp) && small.glyphs.get(cp)
    const ratio = h[13] / Math.max(1, small.header[13])
    const height = latin ? Math.min(h[10], Math.max(1, Math.ceil(latin.height * ratio))) : Math.max(1, h[10])
    const minimumY = h[12] > 127 ? h[12] - 256 : h[12]
    return { codePoint: cp, width: latin ? Math.min(h[9], Math.max(1, Math.ceil(latin.width * ratio))) : Math.max(1, h[9]),
      height, x: 0, y: latin ? Math.min(h[10] + minimumY - height, Math.max(minimumY, Math.round(latin.y * ratio))) : minimumY,
      dx: latin ? Math.min(h[9] + 1, Math.max(1, Math.ceil(latin.dx * ratio))) : Math.max(1, h[9]), pixels: [] }
  }
  function builtinSymbol(cp, header) {
    const width = Math.max(5, Math.min(header[9], header[13])), height = Math.max(7, Math.min(header[10], header[13]))
    const pixels = Array(width * height).fill(0), stroke = height > 10 ? 2 : 1
    const put = (x, y) => { if (x >= 0 && y >= 0 && x < width && y < height) pixels[y * width + x] = 1 }
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
      if (cp === 183 && Math.abs(x - (width - 1) / 2) < stroke && Math.abs(y - height / 2) < stroke) put(x, y)
      if (cp === 215 && (Math.abs(x - y * (width - 1) / (height - 1)) < stroke || Math.abs(x - (width - 1) + y * (width - 1) / (height - 1)) < stroke)) put(x, y)
      if (cp === 177 && ((y >= height - stroke && x > 0) ||
        (Math.abs(y - height / 3) < stroke && x > 0) || (Math.abs(x - width / 2) < stroke && y < height * 2 / 3))) put(x, y)
      if (cp === 176 && y < height * 0.6) {
        const radius = Math.max(1.5, width / 3), distance = Math.hypot(x - width / 2, y - radius)
        if (Math.abs(distance - radius) < stroke * 0.8) put(x, y)
      }
    }
    return { codePoint: cp, width, height, x: 0, y: 0, dx: width + 1, pixels }
  }
  function compose(baseInput, choices = {}) {
    if (baseInput.length !== 4) fail('Exactly four reference faces are required')
    if (Object.keys(choices).some((key) => !['numbers', 'latin', 'chinese'].includes(key))) fail('Unknown font role')
    const base = baseInput.map(bytes), decoded = base.map(decodeFont), out = base.map((b) => b.slice())
    const unionChinese = new Set([...decoded[0].glyphs.keys(), ...decoded[1].glyphs.keys()].filter(isChinese))
    /* Complete both body faces on the host. Built-in geometric Latin signs are
     * explicit InkTag defaults, never a fallback for a selected custom source.
     * Missing default WQY size variants derive from the other WQY body master. */
    for (let face = 0; face < 2; face++) {
      const target = decoded[face], glyphs = new Map(target.glyphs)
      if (!choices.latin) {
        for (let cp = 32; cp <= 126; cp++) if (isLatin(cp) && !glyphs.has(cp)) {
          const source = decoded[0].glyphs.get(cp)
          if (!source) fail(`Reference lacks Latin U+${cp.toString(16)}`)
          glyphs.set(cp, fitGlyph(source, referenceCell(cp, target, decoded[0])))
        }
        for (const cp of [176, 177, 183, 215]) if (!glyphs.has(cp)) glyphs.set(cp, builtinSymbol(cp, target.header))
      }
      if (!choices.chinese) for (const cp of unionChinese) if (!glyphs.has(cp)) {
        const source = decoded[1 - face].glyphs.get(cp)
        if (!source) fail('Missing reference Chinese source')
        glyphs.set(cp, fitGlyph(source, referenceCell(cp, target, decoded[0])))
      }
      target.glyphs = glyphs
      if (glyphs.size !== decodeFont(base[face]).glyphs.size) out[face] = encodeFont(glyphs, target.header)
    }
    for (const [role, classify] of [['numbers', isNumber], ['latin', isLatin], ['chinese', isChinese]]) {
      const choice = choices[role]
      if (!choice) continue
      const masters = [sourceFace(choice, 'small'), sourceFace(choice, 'large')]
      for (let face = 0; face < 4; face++) {
        const source = masters[face % 2], glyphs = new Map(decoded[face].glyphs)
        const required = new Set([...glyphs.keys()].filter(classify))
        if (face < 2) {
          if (role !== 'chinese') for (const cp of requiredCharacters(role)) required.add(cp)
          if (role === 'chinese') {
            for (const cp of unionChinese) required.add(cp)
            for (const cp of requiredCharacters(role, choice.characters || '')) required.add(cp)
          }
        }
        if (!required.size) continue
        for (const cp of required) {
          if (cp > 65534 || !classify(cp)) fail(`${role}: character is outside its role`)
          const candidate = source.glyphs.get(cp)
          if (!candidate) fail(`${role}: missing U+${cp.toString(16).toUpperCase()} in ${face % 2 ? 'large' : 'small'} source`)
          glyphs.set(cp, { ...candidate,
            pixels: candidate.pixels.map((n) => candidate.coverage8 ? Number(n >= 96) : n) })
        }
        decoded[face].glyphs = glyphs
        out[face] = encodeFont(glyphs)
      }
    }
    return out.map((font) => encodeFont(decodeFont(font).glyphs))
  }
  function validateNative(font, id) {
    const f = decodeFont(font, true), h = f.header, signed = (n) => n > 127 ? n - 256 : n
    const left = signed(h[11]), bottom = signed(h[12]), top = h[10] + bottom
    if (!h[9] || !h[10] || left + h[9] > 127 || top < 0 || top > 127 || bottom > 0 ||
        h[13] !== top || signed(h[14]) !== bottom || h[15] !== top || signed(h[16]) !== bottom) fail('Invalid native font bounds')
    for (const g of f.glyphs.values()) if (g.x < left || g.x + g.width > left + h[9] ||
      g.y < bottom || g.y + g.height > top || g.dx < 0 || g.dx > 127) fail('Glyph escapes native font bounds')
    const required = id >= 4 ? Array.from({ length: 11 }, (_, n) => n + 48)
      : id < 2 ? [...Array.from({ length: 95 }, (_, n) => n + 32), 176,177,183,215, ...chineseRequired] : requiredCharacters('numbers')
    for (const cp of required) if (!f.glyphs.has(cp)) fail(`Native face${id} lacks U+${cp.toString(16)}`)
    if (id >= 4) {
      if (f.glyphs.size !== 11 || left || bottom) fail('Invalid native digit face')
      for (const g of f.glyphs.values()) if (g.x || g.y || g.height !== h[10] || g.width !== g.dx ||
        g.width < 1 || (g.codePoint !== 58 && g.width !== h[9])) fail('Native digits must use fixed cells')
    }
    return f
  }
  /* Reuse U8g2 RLE. Choosing run widths is host work, not a new MCU decoder.
   * The original one-byte record limit is retained; no truncation fallback. */
  function compressNativeDigits(glyphInput) {
    const glyphs = glyphInput instanceof Map ? glyphInput : new Map(glyphInput.map((g) => [g.codePoint, g]))
    let best
    for (let z = 1; z <= 8; z++) for (let o = 1; o <= 8; o++) {
      try {
        const candidate = encodeFont(glyphs, undefined, [z, o])
        if (!best || candidate.length < best.length) best = candidate
      } catch { /* This parameter pair cannot represent every bounded glyph. */ }
    }
    if (!best) fail('Digit glyph exceeds the U8g2 record format; choose a simpler outline')
    validateNative(best, 4)
    return best
  }
  function buildPackage(fontsInput, generation, maximum = 32768, schema = 1) {
    const fonts = fontsInput.map(bytes)
    if (schema !== 1 || ![4, 6].includes(fonts.length) || !Number.isInteger(generation) || generation < 0 || generation > 0xffffffff ||
        !Number.isInteger(maximum) || maximum < 88 || maximum > 32768) fail('Invalid role package parameters')
    fonts.forEach((font, id) => validateNative(font, id))
    const header = 24 + 16 * fonts.length, total = header + fonts.reduce((n, font) => n + font.length, 0)
    if (total > maximum) fail(`Font package requires ${total} bytes; capacity is ${maximum}`)
    const out = Array(total).fill(0); out.splice(0, 4, 73, 75, 70, 65)
    set16(out, 4, schema); set16(out, 6, header); set32(out, 8, generation); set32(out, 12, total); out[20] = fonts.length
    let offset = header
    fonts.forEach((font, id) => {
      const p = 24 + id * 16
      out[p] = 1; out[p + 1] = id; out[p + 2] = 1
      set32(out, p + 4, offset); set32(out, p + 8, font.length); set32(out, p + 12, crc(font))
      for (let i = 0; i < font.length; i++) out[offset + i] = font[i]
      offset += font.length
    })
    set32(out, 16, crc(out))
    return out
  }
  function parsePackage(input, maximum = 32768) {
    const b = bytes(input)
    if (!Number.isInteger(maximum) || b.length < 88 || b.length > maximum || maximum > 32768 || maximum < 88 ||
        String.fromCharCode(...b.slice(0, 4)) !== 'IKFA' || b[4] !== 1 || b[5] ||
        ![4, 6].includes(b[20]) || b[21] || b[22] || b[23] ||
        b[6] + 256 * b[7] !== 24 + 16 * b[20] || le32(b, 12) !== b.length) fail('Invalid IKFA v1 header')
    const expected = le32(b, 16), cleared = b.slice(); cleared.fill(0, 16, 20)
    if (crc(cleared) !== expected) fail('Role package CRC mismatch')
    const entries = []; let end = 24 + 16 * b[20]
    for (let id = 0; id < b[20]; id++) {
      const p = 24 + 16 * id, offset = le32(b, p + 4), length = le32(b, p + 8), checksum = le32(b, p + 12)
      if (b[p] !== 1 || b[p + 1] !== id || b[p + 2] !== 1 || b[p + 3] ||
          offset !== end || !length || length > b.length - offset) fail('Invalid role package directory')
      const font = b.slice(offset, offset + length)
      if (crc(font) !== checksum) fail('Role face CRC mismatch')
      validateNative(font, id)
      entries.push({ id, offset, bytes: length, crc32: checksum, format: b[p + 2] }); end = offset + length
    }
    if (end !== b.length) fail('Trailing role package bytes')
    return { schema: b[4], generation: le32(b, 8), bytes: b.length, crc32: expected, faces: entries }
  }
  globalThis.InkTagFontRoles = Object.freeze({ decodeFont, encodeFont, compose, requiredCharacters, compressNativeDigits, validateNative, buildPackage, parsePackage })
})()
