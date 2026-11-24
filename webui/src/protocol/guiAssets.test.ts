import { buildFontRoles, parseFontPackage, parseFontInfo } from './fontRoles'
import { beforeEach, expect, it, vi } from 'vitest'
import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, StatusV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'
import { defaultGuiFonts, GUI_ASSET_MAX_BYTES,
  guiFontFaces } from './guiAssets'

it('builds one native package with four ordered complete faces', () => {
  const built = buildFontRoles(defaultGuiFonts(), {}, 9)
  const parsed = parseFontPackage(built.bytes)
  expect(parsed.schema).toBe(1); expect(parsed.generation).toBe(9)
  expect(parsed.entries.map((e) => e.id)).toEqual(guiFontFaces.map((e) => e.id))
  expect(parsed.totalSize).toBeLessThanOrEqual(GUI_ASSET_MAX_BYTES)
  expect(() => parseFontPackage(built.bytes, built.totalSize - 1)).toThrow()
  expect(parseFontPackage(built.bytes, built.totalSize).totalSize).toBe(built.totalSize)
  expect(() => parseFontPackage(built.bytes, 32769)).toThrow()
})

it('refuses corrupt bytes, directory tampering and all old package schemas', () => {
  const pkg = buildFontRoles(defaultGuiFonts(), {}, 1)
  for (const offset of [8,24+16+1,pkg.totalSize-1]) {
    const bad = pkg.bytes.slice(); bad[offset] ^= 1
    expect(() => parseFontPackage(bad)).toThrow()
  }
  for (const schema of [0,2,3,255]) {
    const bad = pkg.bytes.slice(); bad[4] = schema
    expect(() => parseFontPackage(bad)).toThrow()
  }
})

it.each([1,2])('refuses old INFOv%i instead of inferring a native-capable device', (version) => {
  const raw = new Uint8Array(20); raw.set([version,1,255,1])
  expect(() => parseFontInfo(raw)).toThrow()
})

beforeEach(() => useDeviceStore.getState().resetDevice())

it('keeps unsupported information reads without fallback or a second request', async () => {
  const client = new InkTagClient(), refusal = new ProtocolV2Error(StatusV2.UNSUPPORTED)
  const request = vi.fn().mockRejectedValue(refusal)
  Object.assign(client, { requestV2: request })
  await expect(client.readGuiAssetInfo()).rejects.toBe(refusal)
  expect(request).toHaveBeenCalledExactlyOnceWith(CommandV2.GET_GUI_ASSET_INFO)
})

it('does not turn a read capability into update authority or Pro entitlement', async () => {
  const client = new InkTagClient(), request = vi.fn()
  Object.assign(client, { requestV2: request })
  await expect(client.writeGuiAssetPackage(new Uint8Array())).rejects.toThrow('不支持字体资源更新')
  useDeviceStore.setState((state) => ({ settingsCapabilities: { ...state.settingsCapabilities, guiAssetUpdate: true } }))
  await expect(client.writeGuiAssetPackage(new Uint8Array())).rejects.toThrow('Pro')
  expect(request).not.toHaveBeenCalled()
})
