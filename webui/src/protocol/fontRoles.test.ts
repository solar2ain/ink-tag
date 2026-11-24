import { beforeEach, expect, it, vi } from 'vitest'
import { U8g2Font } from '../vendor/u8g2-js/index.js'
import { defaultGuiFonts, type GuiAssetPackage } from './guiAssets'
import { admitFontPackage, buildFontRoles, fontInfoLimit, parseFontInfo, parseFontPackage,
  parseNativeDigitFont, userFontRoles, nextFontGeneration } from './fontRoles'
import { InkTagClient } from './client'
import { CommandV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

function info(pkg?: GuiAssetPackage) {
  const b = new Uint8Array(24), view = new DataView(b.buffer)
  b.set([1, pkg ? 1 : 0, pkg ? 0 : 255, 1])
  view.setUint32(4, pkg?.generation ?? 0, true); view.setUint32(8, pkg?.totalSize ?? 0, true)
  view.setUint32(12, 32768, true); view.setUint32(16, pkg?.packageCrc32 ?? 0, true)
  view.setUint16(20, 2, true); b[22] = 1; b[23] = 3
  return b
}

function digits(height: number) {
  const shared = (globalThis as unknown as { InkTagFontRoles: { compressNativeDigits(g: unknown[]): number[] } }).InkTagFontRoles
  return Uint8Array.from(shared.compressNativeDigits(Array.from({ length: 11 }, (_, n) => {
    const width = n === 10 ? 4 : 10
    return { codePoint: 48 + n, width, height, x: 0, y: 0, dx: width,
      pixels: Array.from({ length: width * height }, (_, i) => Number(i % width === 0)) }
  })))
}

it.each([
  [1, 1, false], [7, 6, false], [0, 0, false],
  [0, 0x80000000, false], [0xffffffff, 0x7fffffff, false],
  [0, 0xffffffff, false], [7, 8, true], [0xfffffffe, 0, true], [0xffffffff, 0, true],
] as const)('uses firmware uint32 ordering from %i to %i', (installed, incoming, accepted) => {
  const current = buildFontRoles(defaultGuiFonts(), {}, installed)
  const pkg = buildFontRoles(defaultGuiFonts(), {}, incoming)
  const check = () => admitFontPackage(pkg.bytes, parseFontInfo(info(current)))
  if (accepted) expect(check()).toMatchObject({ generation: incoming })
  else expect(check).toThrow(/未开始写入/)
})

it('admits a first generation zero package but wraps the next serial after UINT32_MAX', () => {
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 0)
  expect(admitFontPackage(pkg.bytes, parseFontInfo(info())).generation).toBe(0)
  expect(nextFontGeneration(0xffffffff)).toBe(0)
  expect(nextFontGeneration(0)).toBe(1)
  for (const invalid of [-1, 0x100000000, NaN, 1.5]) {
    expect(() => nextFontGeneration(invalid)).toThrow()
    expect(() => fontInfoLimit({ ...parseFontInfo(info()), generation: invalid })).toThrow()
  }
})

it('builds the four user roles with only native IKFA v1', () => {
  expect(userFontRoles.map((role) => role.id)).toEqual(['numbers', 'latin', 'chinese', 'largeDigits'])
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 7)
  expect(pkg.schema).toBe(1)
  expect(pkg.entries.map((e) => e.id)).toEqual([0, 1, 2, 3])
  expect(parseFontPackage(pkg.bytes).packageCrc32).toBe(pkg.packageCrc32)
  for (const schema of [2, 3]) expect(() => buildFontRoles(defaultGuiFonts(), {}, 1, 32768, undefined, schema)).toThrow(/IKFA v1/)
})

it('completes printable ASCII and required symbols in both body faces', () => {
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 1)
  for (const e of pkg.entries.slice(0, 2)) {
    const font = U8g2Font.fromArray(pkg.bytes.slice(e.offset, e.offset + e.length))
    for (let cp = 32; cp <= 126; cp++) expect(font.hasGlyph(cp)).toBe(true)
    for (const cp of [176,177,183,215,8451]) expect(font.hasGlyph(cp)).toBe(true)
  }
})

it('preserves the selected number glyph metrics instead of fitting reference cells', () => {
  const base = defaultGuiFonts()
  const pkg = buildFontRoles(base, { numbers: { small: base[2], large: base[3] } }, 8)
  for (const e of pkg.entries) {
    const after = U8g2Font.fromArray(pkg.bytes.slice(e.offset, e.offset + e.length))
    const source = U8g2Font.fromArray(base[e.id % 2 + 2])
    for (const cp of [48,49,57]) expect(after.getGlyphProps(cp)).toEqual(source.getGlyphProps(cp))
  }
})

it.each(['chinese', 'latin'] as const)('refuses an incomplete selected %s source', (role) => {
  const base = defaultGuiFonts()
  expect(() => buildFontRoles(base, { [role]: { small: base[2], large: base[3] } }, 1)).toThrow(/missing/)
})

it('accepts arbitrary native digit sizes and refuses missing or invalid paired faces', () => {
  const big = [digits(19), digits(31)]
  big.forEach(parseNativeDigitFont)
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 3, 32768, big)
  expect(pkg.entries.map((e) => e.format)).toEqual([1,1,1,1,1,1])
  expect(admitFontPackage(pkg.bytes, parseFontInfo(info())).schema).toBe(1)
  expect(() => buildFontRoles(defaultGuiFonts(), {}, 3, 32768, big.slice(0, 1))).toThrow()
  expect(() => parseNativeDigitFont(new Uint8Array([73,84,68,71]))).toThrow()
})

it.each([6,14,30])('rejects obsolete or unknown schema mask %i', (mask) => {
  const raw = info(); raw[20] = mask
  if (mask === 6) { raw[22] = 1; raw[23] = 1 }
  expect(() => parseFontInfo(raw)).toThrow()
})

it('checks the exact native status tuple, active schema and single-bank bounds', () => {
  expect(fontInfoLimit(parseFontInfo(info()))).toBe(32768)
  for (const [offset, value] of [[0,2],[1,3],[1,2],[2,1],[3,0],[22,2],[23,1]]) {
    const raw = info(); raw[offset] = value
    expect(() => parseFontInfo(raw)).toThrow()
  }
  const bad = info(); new DataView(bad.buffer).setUint32(12, 32769, true)
  expect(() => parseFontInfo(bad)).toThrow()
})

beforeEach(() => {
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState((state) => ({ settingsCapabilities: { ...state.settingsCapabilities, guiAssetUpdate: true },
    activation: { ...state.activation, phase: 'active', edition: 'advanced' } }))
})

it.each([7, 6, 0x80000007])('rejects stale or ambiguous serial %i before any destructive command', async (incoming) => {
  const installed = buildFontRoles(defaultGuiFonts(), {}, 7)
  const pkg = buildFontRoles(defaultGuiFonts(), {}, incoming)
  const client = new InkTagClient(), chunk = vi.fn()
  const request = vi.fn(async (_command: number) => ({ payload: info(installed) }))
  Object.assign(client, { requestV2: request, writeV2TransferChunk: chunk })
  await expect(client.writeGuiAssetPackage(pkg, undefined, { confirmSingleUser: true })).rejects.toThrow(/未开始写入/)
  expect(request.mock.calls.map(([command]) => command)).toEqual([CommandV2.GET_GUI_ASSET_INFO])
  expect(chunk).not.toHaveBeenCalled()
  expect(useDeviceStore.getState().action).toBe('idle')
})

it.each([1,2,3])('rejects old ITFA%i locally before any request or BEGIN', async (schema) => {
  const raw = buildFontRoles(defaultGuiFonts(), {}, 1).bytes.slice(); raw.set([73,84,70,65]); raw[4] = schema
  const client = new InkTagClient(), request = vi.fn()
  Object.assign(client, { requestV2: request })
  await expect(client.writeGuiAssetPackage(raw, undefined, { confirmSingleUser: true })).rejects.toThrow()
  expect(request).not.toHaveBeenCalled()
})

it.each([1,2])('refuses an old INFOv%i device before BEGIN', async (version) => {
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 1)
  const client = new InkTagClient(), raw = new Uint8Array(20); raw[0] = version
  const request = vi.fn(async () => ({ payload: raw }))
  Object.assign(client, { requestV2: request })
  await expect(client.writeGuiAssetPackage(pkg, undefined, { confirmSingleUser: true })).rejects.toThrow()
  expect(request).toHaveBeenCalledExactlyOnceWith(CommandV2.GET_GUI_ASSET_INFO)
})

it('preserves complete transfer, confirmation, readback and unknown-COMMIT guards', async () => {
 for (const fault of ['success', 'unknown-commit', 'bad-readback', 'no-consent']) {
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 7)
  const client = new InkTagClient(), chunks: Uint8Array[] = []
  let committed = false
  const request = vi.fn(async (command: number, payload?: Uint8Array) => {
    if (command === CommandV2.GET_GUI_ASSET_INFO) {
      const b = info(committed ? pkg : undefined)
      if (committed && fault === 'bad-readback') b[1] = 3
      return { payload: b }
    }
    if (command === CommandV2.TRANSFER_BEGIN) expect(payload?.slice(0, 3)).toEqual(new Uint8Array([6,1,1]))
    if (command === CommandV2.TRANSFER_COMMIT) {
      committed = true
      if (fault === 'unknown-commit') throw new Error('unknown COMMIT ACK')
    }
    return { payload: new Uint8Array([6,0,64,0]) }
  })
  Object.assign(client, { requestV2: request, writeV2TransferChunk: vi.fn(async (_id, _type, _size, _offset, bytes: Uint8Array) => chunks.push(bytes)) })
  const result = client.writeGuiAssetPackage(pkg, undefined, { confirmSingleUser: fault !== 'no-consent' })
  if (fault === 'success') {
    await expect(result).resolves.toMatchObject({ version: 1, schema: 1, activeSize: pkg.totalSize })
    expect(Uint8Array.from(chunks.flatMap((b) => Array.from(b)))).toEqual(pkg.bytes)
  } else await expect(result).rejects.toThrow()
  expect(request.mock.calls.filter(([command]) => command === CommandV2.TRANSFER_COMMIT)).toHaveLength(fault === 'no-consent' ? 0 : 1)
  expect(request.mock.calls.some(([command]) => command === CommandV2.TRANSFER_ABORT)).toBe(false)
 }
})

it('admits only one font transaction and rejects uploads during another action', async () => {
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 7), client = new InkTagClient()
  let finish!: (value: { payload: Uint8Array }) => void
  const request = vi.fn(() => new Promise<{ payload: Uint8Array }>(resolve => { finish = resolve }))
  Object.assign(client, { requestV2: request })
  const first = client.writeGuiAssetPackage(pkg, undefined, { confirmSingleUser: true })
  expect(useDeviceStore.getState().action).toBe('font-update')
  await expect(client.writeGuiAssetPackage(pkg, undefined, { confirmSingleUser: true })).rejects.toThrow(/其他操作/)
  expect(request).toHaveBeenCalledTimes(1)
  finish({ payload: new Uint8Array() })
  await expect(first).rejects.toThrow()
  expect(useDeviceStore.getState().action).toBe('idle')
  useDeviceStore.getState().setAction('picture')
  await expect(client.writeGuiAssetPackage(pkg, undefined, { confirmSingleUser: true })).rejects.toThrow(/其他操作/)
  expect(useDeviceStore.getState().action).toBe('picture')
})

it('never continues or sends ABORT on a replacement connection', async () => {
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 7), client = new InkTagClient()
  const request = vi.fn(async (command: number) => {
    if (command === CommandV2.GET_GUI_ASSET_INFO) return { payload: info() }
    return { payload: new Uint8Array([6, 0, 64, 0]) }
  })
  const chunk = vi.fn(async () => {
    useDeviceStore.setState({ deviceId: 'replacement', action: 'picture' })
    Object.assign(client, { connectionGeneration: 1 })
  })
  Object.assign(client, { requestV2: request, writeV2TransferChunk: chunk })
  await expect(client.writeGuiAssetPackage(pkg, undefined, { confirmSingleUser: true })).rejects.toThrow(/连接已变化/)
  expect(chunk).toHaveBeenCalledTimes(1)
  expect(request.mock.calls.map(([command]) => command)).toEqual([CommandV2.GET_GUI_ASSET_INFO, CommandV2.TRANSFER_BEGIN])
  expect(useDeviceStore.getState().action).toBe('picture')
})
