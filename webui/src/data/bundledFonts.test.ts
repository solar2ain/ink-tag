import { afterEach, expect, it, vi } from 'vitest'

vi.mock('./optionalAssets', () => ({ loadOptionalAsset: vi.fn().mockResolvedValue('data:font/woff2;base64,YWJj') }))

class FakeFace { status = 'loaded'; async load() { return this } }

afterEach(() => { vi.unstubAllGlobals(); vi.resetModules() })

it('does not load a calligraphy asset for device or system fonts', async () => {
  vi.stubGlobal('FontFace', FakeFace)
  const { ensureBundledFont } = await import('./bundledFonts')
  await expect(ensureBundledFont('wqy9-lunar')).resolves.toBeUndefined()
})

it('waits for the actual bundled face and shares concurrent requests', async () => {
  const load = vi.fn().mockResolvedValue([{ status: 'loaded' }])
  vi.stubGlobal('document', { fonts: { load, add: vi.fn() } })
  vi.stubGlobal('FontFace', FakeFace)
  const { ensureBundledFont } = await import('./bundledFonts')
  await Promise.all([ensureBundledFont('ma-shan-zheng'), ensureBundledFont('"InkTag Ma Shan Zheng"')])
  expect(load).toHaveBeenCalledTimes(1)
})

it('does not silently treat an absent font as loaded; failed requests can retry', async () => {
  const load = vi.fn().mockResolvedValueOnce([]).mockResolvedValueOnce([{ status: 'loaded' }])
  vi.stubGlobal('document', { fonts: { load, add: vi.fn() } })
  vi.stubGlobal('FontFace', FakeFace)
  const { ensureBundledFont } = await import('./bundledFonts')
  await expect(ensureBundledFont('ma-shan-zheng')).rejects.toThrow('未使用系统字体代替')
  await expect(ensureBundledFont('ma-shan-zheng')).resolves.toBeUndefined()
})
