import { useEffect, useRef, useState, type CSSProperties } from 'react'
import type { PageTemplate } from '../data/templates'
import { colorModes } from '../lab/leafer/palette'

export function fitTemplatePreview(naturalWidth: number, naturalHeight: number, availableWidth: number, availableHeight: number) {
  const scale = Math.min(1, availableWidth / naturalWidth, availableHeight / naturalHeight)
  return {
    width: Math.max(1, Math.round(naturalWidth * scale)),
    height: Math.max(1, Math.round(naturalHeight * scale)),
    scale,
  }
}

export function TemplatePreview({ template, compact = false, screenColor }: { template: PageTemplate; compact?: boolean; screenColor?: number }) {
  const { width, height } = template
  const host = useRef<HTMLDivElement>(null)
  const canvas = useRef<HTMLCanvasElement>(null)
  const initialBounds = compact ? { width: 156, height: 108 } : { width: 300, height: 256 }
  const [displaySize, setDisplaySize] = useState(() => fitTemplatePreview(width, height, initialBounds.width, initialBounds.height))
  const color = colorModes.find(mode => mode.value === screenColor)?.value

  useEffect(() => {
    const element = host.current
    if (!element) return
    const update = () => {
      const next = fitTemplatePreview(width, height, Math.max(1, element.clientWidth - 24), Math.max(1, element.clientHeight - 24))
      setDisplaySize((current) => current.width === next.width && current.height === next.height ? current : next)
    }
    update()
    if (typeof ResizeObserver === 'undefined') return
    const observer = new ResizeObserver(update)
    observer.observe(element)
    return () => observer.disconnect()
  }, [height, width])

  useEffect(() => {
    const element = canvas.current
    if (!element) return
    let cancelled = false
    void import('../lab/leafer/scenePreview').then(async ({ renderTemplatePreview }) => {
      if (cancelled) return
      const bitmap = await createImageBitmap(await renderTemplatePreview(template.id, width, height, color))
      if (!cancelled) {
        element.width = width; element.height = height
        element.getContext('2d')!.drawImage(bitmap, 0, 0)
      }
      bitmap.close()
    }).catch(error => { if (!cancelled) console.error('模板预览生成失败', error) })
    return () => { cancelled = true }
  }, [color, height, template.id, width])

  return <div
    ref={host}
    className={`template-preview ${template.layout} ${compact ? 'compact' : ''}`}
    style={{ '--accent': template.accent } as CSSProperties}
  >
    <div className="template-preview-media" style={{ width: displaySize.width, height: displaySize.height }}>
      <canvas
        ref={canvas}
        className="template-preview-canvas scene-template-preview-canvas"
        role="img"
        aria-label={`${template.name} ${template.width}×${template.height} 模板预览`}
      />
    </div>
  </div>
}
