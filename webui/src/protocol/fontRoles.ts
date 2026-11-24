import '../../../cli/src/inktag_cli/_assets/fontRoles.js'
import type { GuiAssetPackage, GuiAssetInfo } from './guiAssets'
import { RELEASE_FONT_VERSION, RELEASE_FONT_INFO_VERSION, RELEASE_FONT_SCHEMA_MASK, RELEASE_FONT_ROLE_VERSION, RELEASE_FONT_FLAGS } from './generatedReleaseV1'

export type FontRole = 'numbers' | 'latin' | 'chinese'
export interface FontRoleChoice { small: Uint8Array; large: Uint8Array; characters?: string }
export type FontRoleChoices = Partial<Record<FontRole, FontRoleChoice>>
interface RolePackage {
  schema: 1; generation: number; bytes: number; crc32: number
  faces: Array<{ id: number; format: 1; offset: number; bytes: number; crc32: number }>
}
interface SharedCompiler {
  compose(base: number[][], choices: Record<string, { small: number[]; large: number[]; characters?: string }>): number[][]
  buildPackage(fonts: number[][], generation: number, maximum: number, schema?: number): number[]
  parsePackage(bytes: number[], maximum: number): RolePackage
  validateNative(bytes: number[], id: number): unknown
}
const compiler = (globalThis as unknown as { InkTagFontRoles: SharedCompiler }).InkTagFontRoles

export const userFontRoles = [
  { id: 'numbers', label: '普通数字', note: '日历、年份等普通数字，分为小号和大号。' },
  { id: 'latin', label: '英文字母', note: '包含空格、半角标点和 ° × ± · 等符号。' },
  { id: 'chinese', label: '中文汉字', note: '包含中文标点；补字必须来自所选字形源。' },
  { id: 'largeDigits', label: '时钟及日历大字', note: '默认内置七段数码字；可选固定单元格0–9及冒号。' },
] as const

export function fontInfoLimit(info: GuiAssetInfo) {
  if (info.version !== RELEASE_FONT_INFO_VERSION || info.supportedSchemas !== RELEASE_FONT_SCHEMA_MASK || info.roleVersion !== RELEASE_FONT_ROLE_VERSION || info.flags !== RELEASE_FONT_FLAGS ||
      ![0, RELEASE_FONT_VERSION].includes(info.schema) || info.storageMode !== 1 || !Number.isSafeInteger(info.maximumSize) ||
      info.maximumSize < 88 || info.maximumSize > 32768 ||
      !Number.isInteger(info.generation) || info.generation < 0 || info.generation > 0xffffffff ||
      !Number.isSafeInteger(info.activeSize) || info.activeSize < 0) throw new Error('不支持的四类字库契约')
  if (info.activeBank === null) {
    if (info.schema || info.generation || info.activeSize || info.activeCrc32) throw new Error('内置默认字体状态不一致')
  } else if (info.activeBank !== 0 || info.schema === 0 || info.activeSize < 88 || info.activeSize > info.maximumSize) {
    throw new Error('四类字体状态无效')
  }
  return info.maximumSize
}

export function parseFontInfo(payload: Uint8Array): GuiAssetInfo {
  if (payload.length !== 24 || payload[0] !== RELEASE_FONT_INFO_VERSION) throw new Error('当前字库需要24字节 INFO v1 响应')
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const info: GuiAssetInfo = { version: RELEASE_FONT_INFO_VERSION, schema: payload[1], activeBank: payload[2] === 255 ? null : payload[2],
    storageMode: payload[3], generation: view.getUint32(4, true), activeSize: view.getUint32(8, true),
    maximumSize: view.getUint32(12, true), activeCrc32: view.getUint32(16, true),
    supportedSchemas: view.getUint16(20, true), roleVersion: payload[22], flags: payload[23] }
  fontInfoLimit(info)
  return info
}

export function parseFontPackage(input: ArrayBuffer | Uint8Array, maximum = 32768): GuiAssetPackage {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input)
  const parsed = compiler.parsePackage(Array.from(bytes), maximum)
  return { schema: parsed.schema, generation: parsed.generation, totalSize: parsed.bytes, packageCrc32: parsed.crc32,
    entries: parsed.faces.map((e) => ({ kind: 1, id: e.id, format: e.format, offset: e.offset, length: e.bytes, crc32: e.crc32 })), bytes }
}

export function admitFontPackage(bytes: Uint8Array, info: GuiAssetInfo) {
  const pkg = parseFontPackage(bytes, fontInfoLimit(info))
  const delta = (pkg.generation - info.generation) >>> 0
  if (info.activeBank !== null && (delta === 0 || delta >= 0x80000000)) {
    throw new Error('字库包不是更新版本，未开始写入。请重新生成字库包。')
  }
  return pkg
}

export function nextFontGeneration(current: number) {
  if (!Number.isInteger(current) || current < 0 || current > 0xffffffff) throw new Error('设备字库版本无效，请重新读取设备状态。')
  return (current + 1) >>> 0
}

export function parseNativeDigitFont(bytes: Uint8Array) {
  return compiler.validateNative(Array.from(bytes), 4)
}

export function buildFontRoles(base: Uint8Array[], choices: FontRoleChoices, generation: number,
  maximum = 32768, largeDigits?: Uint8Array[], schema = RELEASE_FONT_VERSION) {
  if (schema !== RELEASE_FONT_VERSION) throw new Error('仅支持 IKFA v1 用户字库')
  const selected = Object.fromEntries(Object.entries(choices).map(([role, c]) =>
    [role, { small: Array.from(c.small), large: Array.from(c.large), characters: c.characters }]))
  const faces = compiler.compose(base.map((b) => Array.from(b)), selected)
  if (largeDigits?.length) {
    if (largeDigits.length !== 2) throw new Error('时钟大字需要小号和大号两套字形')
    faces.push(...largeDigits.map((b) => Array.from(b)))
  }
  return parseFontPackage(Uint8Array.from(compiler.buildPackage(faces, generation, maximum, schema)), maximum)
}
