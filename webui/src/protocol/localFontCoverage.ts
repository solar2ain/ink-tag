/** Verify required code points in the local SFNT cmap before Canvas can fallback. */
export function requireLocalFontGlyphs(buffer: ArrayBuffer, codePoints: number[]) {
  const v = new DataView(buffer)
  const need = (p: number, n: number) => {
    if (!Number.isSafeInteger(p) || p < 0 || n < 0 || p + n > v.byteLength) throw Error('字体目录越界')
  }
  need(0, 12)
  if (![0x00010000, 0x4f54544f, 0x74727565].includes(v.getUint32(0))) throw Error('请选择独立 TTF/OTF 字体')
  const tables = v.getUint16(4); if (tables > 128) throw Error('字体表数量超限'); need(12, tables * 16)
  let offset = 0, length = 0
  for (let i = 0; i < tables; i++) {
    const p = 12 + i * 16
    if (v.getUint32(p) === 0x636d6170) { offset = v.getUint32(p + 8); length = v.getUint32(p + 12); break }
  }
  if (!offset) throw Error('字体缺少 Unicode cmap')
  need(offset, length); need(offset, 4)
  const count = v.getUint16(offset + 2); if (count > 256) throw Error('字体 cmap 数量超限'); need(offset + 4, count * 8)
  const contains = (sub: number, cp: number) => {
    need(sub, 4)
    const format = v.getUint16(sub)
    if (format === 12) {
      need(sub, 16); const n = v.getUint32(sub + 12)
      if (n > 65536) throw Error('字体 cmap 分组数量超限')
      need(sub + 16, n * 12)
      for (let i = 0; i < n; i++) {
        const p = sub + 16 + i * 12, a = v.getUint32(p), b = v.getUint32(p + 4)
        if (a <= cp && cp <= b) return v.getUint32(p + 8) + cp - a !== 0
      }
    } else if (format === 4 && cp <= 0xffff) {
      need(sub, 8); const n = v.getUint16(sub + 6) / 2
      if (!Number.isInteger(n)) throw Error('字体 cmap 分段无效')
      need(sub + 14, 8 * n + 2)
      for (let i = 0; i < n; i++) {
        const end = v.getUint16(sub + 14 + i * 2), start = v.getUint16(sub + 16 + 2 * n + i * 2)
        if (start <= cp && cp <= end) {
          const delta = v.getInt16(sub + 16 + 4 * n + i * 2), p = sub + 16 + 6 * n + i * 2, range = v.getUint16(p)
          if (!range) return ((cp + delta) & 0xffff) !== 0
          const glyph = p + range + 2 * (cp - start); need(glyph, 2)
          const id = v.getUint16(glyph); return id !== 0 && ((id + delta) & 0xffff) !== 0
        }
      }
    }
    return false
  }
  for (const cp of codePoints) {
    let found = false
    for (let i = 0; i < count; i++) {
      const p = offset + 4 + i * 8, platform = v.getUint16(p), encoding = v.getUint16(p + 2)
      if (platform === 0 || (platform === 3 && [1, 10].includes(encoding))) {
        const sub = offset + v.getUint32(p + 4)
        if (sub < offset || sub >= offset + length) throw Error('字体 cmap 越界')
        found ||= contains(sub, cp)
      }
    }
    if (!found) throw Error(`本地字体缺少 U+${cp.toString(16).toUpperCase()}，不会借用系统字体补齐`)
  }
}
