import { db } from './db'
import { defaultGuiFonts, type GuiAssetInfo, type GuiAssetPackage } from '../protocol/guiAssets'
import { parseFontPackage, fontInfoLimit } from '../protocol/fontRoles'

export function packageFonts(pkg: GuiAssetPackage) {
  return [...pkg.entries].sort((left, right) => left.id - right.id)
    .map((entry) => pkg.bytes.slice(entry.offset, entry.offset + entry.length))
}

export async function rememberGuiAssetPackage(deviceId: string, pkg: GuiAssetPackage, chineseSource?: 'catalog') {
  await db.deviceGuiAssetPackages.put({
    id: deviceId, deviceId, generation: pkg.generation, crc32: pkg.packageCrc32,
    bytes: new Blob([pkg.bytes.slice().buffer], { type: 'application/octet-stream' }), chineseSource, updatedAt: Date.now(),
  })
}

export async function editableGuiFontPackage(deviceId: string, info: GuiAssetInfo) {
  const maximum = fontInfoLimit(info)
  if (info.activeBank == null) return {fonts:defaultGuiFonts(),chineseSource:'catalog' as const}
  const saved = await db.deviceGuiAssetPackages.get(deviceId)
  if (!saved || saved.crc32 !== info.activeCrc32 || saved.generation !== info.generation) {
    throw new Error('设备正在使用另一客户端写入的自定义字库。请先在“设备字体资源”中导入对应 .itfa 包，避免覆盖已有字形。')
  }
  const parsed = parseFontPackage(await saved.bytes.arrayBuffer(), maximum)
  if (parsed.schema !== info.schema || parsed.totalSize !== info.activeSize ||
      parsed.packageCrc32 !== info.activeCrc32 || parsed.generation !== info.generation) {
    throw new Error('本地字体包与设备当前字库不一致，请先导入对应 .itfa 包。')
  }
  return {fonts:packageFonts(parsed),chineseSource:saved.chineseSource}
}

export async function editableGuiFonts(deviceId: string, info: GuiAssetInfo) {
  return (await editableGuiFontPackage(deviceId,info)).fonts.slice(0,4)
}
