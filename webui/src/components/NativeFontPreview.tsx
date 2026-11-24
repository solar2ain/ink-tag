import { useEffect, useRef, useState } from 'react'
import type { GuiAssetInfo, GuiAssetPackage } from '../protocol/guiAssets'
import { parseFontPackage } from '../protocol/fontRoles'
import { db } from '../data/db'
import type { ScreenConfig } from '../types'
import { previewScreen, renderFontPreview, type NativeFontFrame, type FontPreviewPage } from '../protocol/nativeFontPreview'

export function matchesInstalledFont(pkg: GuiAssetPackage | null | undefined, info: GuiAssetInfo) {
  return !!pkg && info.activeBank === 0 && pkg.schema === info.schema &&
    pkg.generation === info.generation && pkg.packageCrc32 === info.activeCrc32 && pkg.totalSize === info.activeSize
}

function PixelPreview({ frame }: { frame: NativeFontFrame }) {
  const ref = useRef<HTMLCanvasElement>(null)
  useEffect(() => {
    const context = ref.current?.getContext('2d')
    if (!context) return
    const image = context.createImageData(frame.width, frame.height)
    const palette = [[255, 255, 255], [23, 26, 24], [200, 45, 40]]
    frame.pixels.forEach((p, i) => {
      const rgb = palette[p] ?? palette[0]
      image.data.set([...rgb, 255], i * 4)
    })
    context.putImageData(image, 0, 0)
  }, [frame])
  return <canvas ref={ref} width={frame.width} height={frame.height}
    style={{ maxWidth: '100%', width: frame.width, height: 'auto', imageRendering: 'pixelated', background: 'white' }}
    aria-label="使用真实字库的 common 页面像素预览" />
}

export function NativeFontPreview({ pending, imported, info, deviceId, screen, pro }: {
  pending: GuiAssetPackage | null; imported: GuiAssetPackage | null
  info: GuiAssetInfo | null; deviceId: string; screen: ScreenConfig | null; pro: boolean
}) {
  const [source, setSource] = useState<'pending' | 'installed'>('pending')
  const [page, setPage] = useState<FontPreviewPage>('clock')
  const [frame, setFrame] = useState<NativeFontFrame | null>(null)
  const [message, setMessage] = useState('')
  useEffect(() => {
    let cancelled = false
    setFrame(null); setMessage('正在渲染实际字库…')
    void (async () => {
      if (!screen) throw Error('读取或选择屏幕配置后才能准确预览。')
      let pkg = pending
      if (source === 'installed') {
        if (!info) throw Error('尚未取得设备字库状态。')
        pkg = null
        if (info.activeBank !== null) {
          if (matchesInstalledFont(imported, info)) pkg = imported
          else {
            const saved = await db.deviceGuiAssetPackages.get(deviceId)
            if (saved && saved.generation === info.generation && saved.crc32 === info.activeCrc32)
              pkg = parseFontPackage(await saved.bytes.arrayBuffer(), info.maximumSize)
          }
          if (!matchesInstalledFont(pkg, info))
            throw Error('本地缺少与设备 generation、CRC 和大小匹配的实际字库，无法准确预览。请导入对应 ITFA 文件。')
        }
      } else if (!pkg) throw Error('请先导入或生成待上传字库。')
      const rendered = await renderFontPreview(previewScreen(screen, pro), page, pkg?.bytes)
      if (!cancelled) { setFrame(rendered); setMessage('') }
    })().catch((error: unknown) => {
      if (!cancelled) setMessage(error instanceof Error ? error.message : String(error))
    })
    return () => { cancelled = true }
  }, [source, page, pending, imported, info, deviceId, screen, pro])
  return <div className="native-font-preview">
    <div className="section-actions"><strong>实际字库预览</strong>
      <label>字库来源 <select value={source} onChange={(e) => setSource(e.target.value as typeof source)}>
        <option value="pending">待上传字库</option><option value="installed">设备已安装字库</option>
      </select></label>
      <label>页面 <select value={page} onChange={(e) => setPage(e.target.value as FontPreviewPage)}>
        <option value="clock">时钟</option><option value="calendar">日历</option><option value="device-info">设备信息</option>
      </select></label>
    </div>
    <p className="field-note">使用 common 渲染器及字体包真实字形，按原生 1:1 绘制。日期、设备信息为示例；预览不写入设备，也不模拟面板刷新效果。</p>
    {frame ? <PixelPreview frame={frame} /> : <p role="status" className="field-note">{message}</p>}
  </div>
}
