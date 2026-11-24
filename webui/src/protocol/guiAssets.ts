import { deviceFontData } from '../device-gui/fonts.generated'

export const GUI_ASSET_SCHEMA = 1
export const GUI_ASSET_MAX_BYTES = 32 * 1024
export const GUI_ASSET_ABSOLUTE_MAX_BYTES = GUI_ASSET_MAX_BYTES
export const GUI_ASSET_HEADER_BYTES = 24
export const GUI_ASSET_ENTRY_BYTES = 16
export const SINGLE_USER_FONT_WARNING = '本设备只有一个用户字库区。更新失败可能丢失原自定义字体，设备将使用内置默认字体。是否继续？'

export const guiFontFaces = [
  { id: 0, key: 'wqy9-lunar', label: '小号正文' },
  { id: 1, key: 'wqy12-lunar', label: '大号正文' },
  { id: 2, key: 'helv-bold-14-num', label: '小号数字' },
  { id: 3, key: 'helv-bold-18-num', label: '大号数字' },
] as const

export interface GuiAssetEntry {
  kind: 1
  id: number
  format: 1 | 2
  offset: number
  length: number
  crc32: number
}

export interface GuiAssetPackage {
  schema: 1
  generation: number
  totalSize: number
  packageCrc32: number
  entries: GuiAssetEntry[]
  bytes: Uint8Array
}

export interface GuiAssetInfo {
  version: number
  schema: number
  activeBank: number | null
  generation: number
  activeSize: number
  maximumSize: number
  activeCrc32: number
  storageMode?: number
  supportedSchemas?: number
  roleVersion?: number
  flags?: number
}

export function defaultGuiFonts() {
  return guiFontFaces.map((face) => Uint8Array.from(atob(deviceFontData[face.key].base64), (c) => c.charCodeAt(0)))
}
