import { useEffect, useId, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { X, ZoomIn, ZoomOut } from 'lucide-react'
import type { ScreenConfig } from '../../types'
import { deviceEffect, paperPreview } from './deviceEffect'
import { adaptFrameToSize } from '../../image/canvasFit'
import { canvasSizeForScreen } from '../../protocol/screen'
import type { RgbaFrame } from '../../../../packages/inktag-client-core/src/screen'

const zoomSteps = [.25, .35, .5, .65, .8, .9, 1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75, 3, 3.25, 3.5, 3.75, 4]

export function DeviceEffectDialog({ prepareCanvas, color, screen, close, overlay }: {
  prepareCanvas: () => Promise<HTMLCanvasElement>; color: number; screen: ScreenConfig | null; close: () => void
  overlay?: (frame: RgbaFrame) => RgbaFrame
}) {
  const id = useId(), dialog = useRef<HTMLDivElement>(null), canvas = useRef<HTMLCanvasElement>(null)
  const stage = useRef<HTMLDivElement>(null)
  const [result, setResult] = useState<{ width: number; height: number; profileMatched: boolean }>()
  const [error, setError] = useState(''), [zoom, setZoom] = useState<number | null>(null)
  const [available, setAvailable] = useState<{ width: number; height: number }>()
  const scale = zoom ?? (result && available
    ? Math.min(available.width / result.width, available.height / result.height, 4)
    : 1)
  const smaller = zoomSteps.filter(step => step < scale - .001).at(-1)
  const larger = zoomSteps.find(step => step > scale + .001)
  const deviceSize=screen&&canvasSizeForScreen(screen)
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null
    dialog.current?.querySelector<HTMLButtonElement>('button')?.focus()
    return () => { if (previous?.isConnected) previous.focus() }
  }, [])
  useEffect(() => {
    const observer = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect
      if (width > 0 && height > 0) setAvailable({ width, height })
    })
    if (stage.current) observer.observe(stage.current)
    return () => observer.disconnect()
  }, [])
  useEffect(() => {
    setError('');setResult(undefined)
    let disposed = false
    void (async () => {
      try {
        await document.fonts.ready
        const source = await prepareCanvas(), context = source.getContext('2d')
        if (!context) throw new Error('无法读取画板')
        const original=context.getImageData(0,0,source.width,source.height)
        const sized=deviceSize?adaptFrameToSize(original,deviceSize):original
        const input=overlay?overlay(sized):sized
        const { frame, profileMatched } = deviceEffect(input, color, screen)
        if (disposed) return
        const target = canvas.current!, output = target.getContext('2d')
        if (!output) throw new Error('浏览器无法生成预览')
        target.width = frame.width; target.height = frame.height
        output.putImageData(new ImageData(new Uint8ClampedArray(paperPreview(frame).data), frame.width, frame.height), 0, 0)
        setResult({ width: frame.width, height: frame.height, profileMatched })
      } catch (cause) { if (!disposed) setError(cause instanceof Error ? cause.message : '预览生成失败，请重试。') }
    })()
    return () => { disposed = true }
  }, [])
  return createPortal(<div className="modal-backdrop" onMouseDown={event => { if (event.target === event.currentTarget) close() }}>
    <div ref={dialog} role="dialog" aria-modal="true" aria-labelledby={id} className="confirmation-dialog device-effect-dialog"
      onKeyDown={event => {
        if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); close() }
        if (event.key === 'Tab') {
          const buttons = [...event.currentTarget.querySelectorAll<HTMLElement>('button:not(:disabled),select:not(:disabled),[tabindex="0"]')]
          if (event.shiftKey && document.activeElement === buttons[0]) { event.preventDefault(); buttons.at(-1)?.focus() }
          else if (!event.shiftKey && document.activeElement === buttons.at(-1)) { event.preventDefault(); buttons[0]?.focus() }
        }
      }}>
      <header><h2 id={id}>设备效果预览</h2><button className="icon-button" aria-label="关闭设备效果预览" onClick={close}><X /></button></header>
      <p>静态页面使用浏览器字体，文字随图片一起发送，不需要设备安装对应字库。</p>
      <p role="status">{error || (result ? result.profileMatched ? '已按当前设备屏幕配置处理并还原' : '按画板色板模拟，未匹配设备屏幕配置' : '正在生成预览…')}</p>
      <div className="device-effect-tools" role="group" aria-label="预览缩放">
        <button className="button secondary" disabled={!result} aria-pressed={zoom === null} onClick={() => setZoom(null)}>适应窗口</button>
        <button className="button secondary" disabled={!result} aria-pressed={zoom === 1} onClick={() => setZoom(1)}>1:1</button>
        <button className="icon-button" disabled={!result || smaller === undefined} aria-label="缩小效果预览" onClick={() => setZoom(smaller ?? scale)}><ZoomOut /></button>
        <span className="device-effect-scale" aria-label="当前预览缩放">{result ? `${Math.round(scale * 100)}%` : '—'}</span>
        <button className="icon-button" disabled={!result || larger === undefined} aria-label="放大效果预览" onClick={() => setZoom(larger ?? scale)}><ZoomIn /></button>
      </div>
      <div ref={stage} className="device-effect-stage" tabIndex={0} aria-label="设备效果图，可滚动检查">
        <canvas ref={canvas} aria-label="屏幕像素预览" hidden={!result} style={result ? { width: Math.floor(result.width * scale), height: Math.floor(result.height * scale) } : undefined} />
      </div>
      <p>仅本机预览，不连接或发送设备。实际墨色、纸色和环境光可能不同；设备标识以实际设置为准。动态页面这里只展示当前画面。</p>
    </div>
  </div>, document.body)
}
