import { FeedbackNotice, useFeedbackMessage } from '../components/FeedbackNotice'
import {
  AlertTriangle, Crop, Download, Expand, Eye, ImagePlus, Nfc, RefreshCcw, RotateCcw, RotateCw, Send, Trash2, Upload, ZoomIn, ZoomOut,
} from 'lucide-react'
import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import { useLocation } from 'react-router-dom'
import { DeviceImageSlots } from '../components/DeviceImageSlots'
import { SendCanvasDialog } from '../components/SendCanvasDialog'
import { NfcOperationDialog } from '../components/NfcOperationDialog'
import { NfcScreenControl } from '../components/NfcScreenControl'
import { webSerial } from '../nfc/pn532'
import type { SerialScreen } from '../nfc/serialTag'
import { useNfcOperation } from '../nfc/useNfcOperation'
import { PageHeader } from '../components/PageHeader'
import { imageEditorDraft } from '../image/editorDraft'
import { clampImagePan, clampImageScale, imageDrawSize, zoomPanAtPoint, type ImageFitMode } from '../image/crop'
import { LatestProcessor } from '../image/latestProcessor'
import { canRunManualImageOperation } from '../image/manualOperation'
import { algorithmGroups, optimizeCanvas, processingPresets, usesDirectStrength, type ImageAlgorithm, type OptimizeOptions } from '../image/optimize'
import { imageCanvasSize } from '../image/orientation'
import { imageCompareLabel, resolveImagePreviewLayer, type ImagePreviewMode } from '../image/previewMode'
import { supportsWarmColors } from '../image/warmColors'
import { paletteForColor } from '../image/processing'
import { canvasSizeForScreen, colorCode, resolutions } from '../protocol/screen'
import { useDeviceStore } from '../store/device'
import { createEditionBadgeBitmap, editionBadgePlacement } from '../device-gui/editionBadgeBitmap'
import { isProActivation } from '../access/imageQuota'

const simulatedResolutions = [...resolutions.reduce((items, [size, width, height]) => {
  const id = `${width}x${height}`
  const existing = items.get(id)
  if (existing) existing.sizes.push(size)
  else items.set(id, { id, width, height, sizes: [size] })
  return items
}, new Map<string, { id: string; width: number; height: number; sizes: string[] }>()).values()]

const simulatedColors = [
  [1, '2 色（黑白）'],
  [2, '3 色（黑白红）'],
  [3, '4 色（黑白红黄）'],
  [10, 'KWRYGB（黑白红黄绿蓝）'],
] as const

type RenderResult = { adjusted: HTMLCanvasElement; preview: HTMLCanvasElement; device: HTMLCanvasElement }
const mobilePreviewMargin = 28

function copyCanvas(source: HTMLCanvasElement, destination: HTMLCanvasElement) {
  destination.width = source.width
  destination.height = source.height
  destination.getContext('2d')!.drawImage(source, 0, 0)
}

export function ImagePage() {
  const state = useDeviceStore()
  const location = useLocation()
  const connected = state.connection === 'connected'
  const showEmptySlideshowNotice = new URLSearchParams(location.search).get('notice') === 'slideshow-empty'
  const fileInput = useRef<HTMLInputElement>(null)
  const originalCanvas = useRef<HTMLCanvasElement>(null)
  const coverageCanvas = useRef<HTMLCanvasElement | null>(null)
  const adjustedCanvas = useRef<HTMLCanvasElement>(null)
  const processedCanvas = useRef<HTMLCanvasElement>(null)
  const deviceCanvas = useRef<HTMLCanvasElement>(null)
  const editionPreviewCanvas = useRef<HTMLCanvasElement>(null)
  const viewportElement = useRef<HTMLDivElement>(null)
  const [fitZoom, setFitZoom] = useState<number>()
  const frameElement = useRef<HTMLDivElement>(null)
  const sourceUrl = useRef(imageEditorDraft.sourceUrl)
  const pendingSource = useRef({generation:0,url:''})
  const [loading, setLoading] = useState(false)
  const drag = useRef<{ mode: 'image' | 'workspace'; x: number; y: number; moved: boolean } | null>(null)
  const [source, setSource] = useState(imageEditorDraft.source)
  const [fileName, setFileName] = useState(imageEditorDraft.fileName)
  const [targetResolution, setTargetResolution] = useState(imageEditorDraft.targetResolution)
  const [simulatedColor, setSimulatedColor] = useState(imageEditorDraft.simulatedColor)
  const [nfcScreen, setNfcScreen] = useState<SerialScreen | null>(imageEditorDraft.nfcScreen)
  const [algorithm, setAlgorithm] = useState<ImageAlgorithm>(() => algorithmGroups.some(group => group.options.some(([id]) => id === imageEditorDraft.algorithm)) ? imageEditorDraft.algorithm : 'auto')
  const [preset, setPreset] = useState<(typeof processingPresets)[number][0]>(imageEditorDraft.preset)
  const [contrast, setContrast] = useState(imageEditorDraft.contrast)
  const [brightness, setBrightness] = useState(imageEditorDraft.brightness)
  const [strength, setStrength] = useState(imageEditorDraft.strength)
  const [heavyInk, setHeavyInk] = useState(imageEditorDraft.heavyInk)
  const [warmColors, setWarmColors] = useState(imageEditorDraft.warmColors)
  const [rotationOffset, setRotationOffset] = useState(imageEditorDraft.rotationOffset)
  const [canvasTurned, setCanvasTurned] = useState(imageEditorDraft.canvasTurned)
  const [fitMode, setFitMode] = useState<ImageFitMode>(imageEditorDraft.fitMode)
  const [imageScale, setImageScale] = useState(imageEditorDraft.imageScale)
  const [pan, setPan] = useState(imageEditorDraft.pan)
  const [workspacePan, setWorkspacePan] = useState(imageEditorDraft.workspacePan)
  const [previewZoom, setPreviewZoom] = useState(imageEditorDraft.previewZoom)
  const [previewMode, setPreviewMode] = useState<ImagePreviewMode>(imageEditorDraft.previewMode)
  const [compareHeld, setCompareHeld] = useState(false)
  const [cropPreview, setCropPreview] = useState(false)
  const imageNfc = useNfcOperation()
  const [sendDialog, setSendDialog] = useState(false)
  const [processing, setProcessing] = useState(false)
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  const processor = useRef<LatestProcessor<OptimizeOptions, RenderResult> | null>(null)

  if (!processor.current) {
    processor.current = new LatestProcessor(async (options) => {
      const current = originalCanvas.current
      if (!current) throw new Error('图片预览尚未准备好')
      const sourceSnapshot = document.createElement('canvas')
      copyCanvas(current, sourceSnapshot)
      const coverageSnapshot = coverageCanvas.current ? document.createElement('canvas') : undefined
      if (coverageSnapshot && coverageCanvas.current) copyCanvas(coverageCanvas.current, coverageSnapshot)
      const adjusted = document.createElement('canvas')
      const preview = document.createElement('canvas')
      const device = document.createElement('canvas')
      await optimizeCanvas(sourceSnapshot, preview, device, options, adjusted, coverageSnapshot)
      return { adjusted, preview, device }
    }, {
      onBusy: () => setProcessing(true),
      onResult: (result) => {
        if (adjustedCanvas.current) copyCanvas(result.adjusted, adjustedCanvas.current)
        if (processedCanvas.current) copyCanvas(result.preview, processedCanvas.current)
        if (deviceCanvas.current) copyCanvas(result.device, deviceCanvas.current)
        setProcessing(false)
        if (!drag.current) setCropPreview(false)
      },
      onError: (error) => {
        setProcessing(false)
        setCropPreview(false)
        setMessage(error instanceof Error ? error.message : String(error))
      },
    })
  }

  const nfcResolution = nfcScreen ? `${nfcScreen.width}x${nfcScreen.height}` : undefined
  const simulated = simulatedResolutions.find((item) => item.id === targetResolution)
    ?? (nfcScreen && targetResolution === nfcResolution ? nfcScreen : simulatedResolutions[0])
  const bluetoothScreen = useMemo(() => connected && state.screen
    ? { ...canvasSizeForScreen(state.screen), color: state.screen.color }
    : null, [connected, state.screen])
  const fixedScreen = nfcScreen ? null : bluetoothScreen
  const screenTarget = useMemo(() => fixedScreen ?? { width: simulated.width, height: simulated.height, color: simulatedColor },
    [fixedScreen, simulated, simulatedColor])
  const selectedResolution = `${screenTarget.width}x${screenTarget.height}`
  const extraScreen = nfcScreen ?? bluetoothScreen
  const extraResolution = extraScreen ? `${extraScreen.width}x${extraScreen.height}` : undefined
  const target = useMemo(() => {
    const size = source && !nfcScreen ? imageCanvasSize(source.naturalWidth,source.naturalHeight,screenTarget) : screenTarget
    return {...(canvasTurned ? {width:size.height,height:size.width} : size),color:screenTarget.color}
  }, [source,screenTarget,canvasTurned,nfcScreen])
  const palette = paletteForColor(target.color)
  const edition = connected && isProActivation(state.activation) ? 'pro' : 'base'
  const showBadge = edition === 'base' || state.brandVisible !== false
  // Editing is always in the upright reading frame, even offline. Negotiation
  // and the old-firmware fallback belong to the send flow, not the composition.
  const badgeTarget = target
  const badge = editionBadgePlacement(target.width,target.height,screenTarget.width,screenTarget.height,edition,true)
  const rotation = (rotationOffset + 360) % 360
  const previousTarget = useRef({width:screenTarget.width,height:screenTarget.height})
  useEffect(() => {
    if (previousTarget.current.width === screenTarget.width && previousTarget.current.height === screenTarget.height) return
    previousTarget.current = {width:screenTarget.width,height:screenTarget.height}
    // A previous panel's manual correction must not rotate/crop the new panel twice.
    processor.current?.cancel()
    setProcessing(Boolean(source))
    setCanvasTurned(false);setRotationOffset(0);setImageScale(1);setPan({x:0,y:0});setWorkspacePan({x:0,y:0})
    setCropPreview(Boolean(source))
  }, [screenTarget.width,screenTarget.height,source])
  const displayZoom = previewZoom * (fitZoom ?? 1)
  const sceneMargin = fitZoom === undefined ? Math.max(160, Math.min(target.width, target.height) * displayZoom * .7) : mobilePreviewMargin
  useLayoutEffect(() => {
    const viewport = viewportElement.current
    if (!viewport || typeof ResizeObserver === 'undefined') return
    // The scene can grow scrollbars. Fit to the viewport box independently of that overflow.
    const update = () => setFitZoom(document.documentElement.classList.contains('mobile-layout')
      ? Math.min(1, Math.max(1, viewport.offsetWidth - mobilePreviewMargin) / target.width,
        Math.max(1, viewport.offsetHeight - mobilePreviewMargin) / target.height)
      : undefined)
    update()
    const observer = new ResizeObserver(update)
    observer.observe(viewport, { box: 'border-box' })
    return () => observer.disconnect()
  }, [source, target.width, target.height])
  const directStrength = usesDirectStrength(algorithm)
  const drawSize = source ? imageDrawSize(source.naturalWidth, source.naturalHeight, target.width, target.height, rotation, fitMode, imageScale) : null
  const previewLayer = resolveImagePreviewLayer(previewMode, compareHeld, cropPreview)
  const compareLabel = imageCompareLabel()

  const drawSource = useCallback(() => {
    const canvas = originalCanvas.current
    if (!canvas || !source) return null
    canvas.width = target.width
    canvas.height = target.height
    const context = canvas.getContext('2d', { willReadFrequently: true })!
    context.fillStyle = '#fff'
    context.fillRect(0, 0, target.width, target.height)
    const size = imageDrawSize(source.naturalWidth, source.naturalHeight, target.width, target.height, rotation, fitMode, imageScale)
    context.save()
    context.translate(target.width / 2 + pan.x, target.height / 2 + pan.y)
    context.rotate(rotation * Math.PI / 180)
    context.drawImage(source, -size.width / 2, -size.height / 2, size.width, size.height)
    context.restore()
    const coverage = coverageCanvas.current || document.createElement('canvas')
    coverageCanvas.current = coverage
    coverage.width = target.width
    coverage.height = target.height
    const coverageContext = coverage.getContext('2d', { willReadFrequently: true })!
    coverageContext.clearRect(0, 0, target.width, target.height)
    coverageContext.save()
    coverageContext.translate(target.width / 2 + pan.x, target.height / 2 + pan.y)
    coverageContext.rotate(rotation * Math.PI / 180)
    coverageContext.drawImage(source, -size.width / 2, -size.height / 2, size.width, size.height)
    coverageContext.restore()
    return context.getImageData(0, 0, target.width, target.height)
  }, [fitMode, imageScale, pan.x, pan.y, rotation, source, target.height, target.width])

  useEffect(() => {
    if (!source) return
    const frame = window.requestAnimationFrame(() => {
      try {
        if (!drawSource()) return
        processor.current?.schedule({ algorithm, preset, brightness, contrast, heavyInk, warmColors, color: target.color, strength })
      } catch(error) {
        setProcessing(false)
        setMessage(error instanceof Error?error.message:'无法生成图片预览，请重新选择图片。')
      }
    })
    return () => window.cancelAnimationFrame(frame)
  }, [algorithm, brightness, contrast, drawSource, heavyInk, preset, source, strength, target.color, target.height, target.width, warmColors])

  useEffect(() => {
    Object.assign(imageEditorDraft, {
      source, sourceUrl: sourceUrl.current, fileName, targetResolution, simulatedColor, nfcScreen,
      algorithm, preset, brightness, contrast, strength, heavyInk, warmColors, rotationOffset, canvasTurned, fitMode, imageScale,
      pan: { ...pan }, workspacePan: { ...workspacePan }, previewZoom, previewMode,
    })
  }, [algorithm, brightness, canvasTurned, contrast, fileName, fitMode, heavyInk, imageScale, nfcScreen, pan, preset, previewMode, previewZoom, rotationOffset, simulatedColor, source, strength, targetResolution, warmColors, workspacePan])

  useEffect(() => {
    const down = (event: KeyboardEvent) => {
      if (event.code === 'Space' && !/INPUT|SELECT|TEXTAREA/.test((event.target as HTMLElement)?.tagName || '')) {
        event.preventDefault()
        setCompareHeld(true)
      }
    }
    const up = (event: KeyboardEvent) => { if (event.code === 'Space') setCompareHeld(false) }
    window.addEventListener('keydown', down)
    window.addEventListener('keyup', up)
    return () => { window.removeEventListener('keydown', down); window.removeEventListener('keyup', up) }
  }, [])

  useEffect(() => {
    processor.current?.activate()
    return () => {
      processor.current?.dispose()
      ++pendingSource.current.generation
      if(pendingSource.current.url)URL.revokeObjectURL(pendingSource.current.url)
      pendingSource.current.url=''
    }
  }, [])

  useEffect(() => {
    const canvas = editionPreviewCanvas.current
    if (!canvas) return
    const bitmap = createEditionBadgeBitmap(edition, badgeTarget.color, badgeTarget.width, badgeTarget.height)
    copyCanvas(bitmap, canvas)
  }, [source, edition, showBadge, badgeTarget.color, badgeTarget.height, badgeTarget.width])

  const loadFile = (file?: File) => {
    if (!file) return
    if(pendingSource.current.url)URL.revokeObjectURL(pendingSource.current.url)
    const generation=++pendingSource.current.generation
    const url = URL.createObjectURL(file)
    pendingSource.current.url=url
    setLoading(true)
    const image = new Image()
    image.onload = () => {
      if(generation!==pendingSource.current.generation)return
      pendingSource.current.url=''
      if(sourceUrl.current)URL.revokeObjectURL(sourceUrl.current)
      sourceUrl.current=url
      setLoading(false)
      processor.current?.cancel()
      // The first processing request starts on the next frame; do not enable
      // Send between decoding this source and producing its device pixels.
      setProcessing(true)
      setSource(image)
      setFileName(file.name || '图片')
      setRotationOffset(0)
      setCanvasTurned(false)
      setFitMode('crop')
      setImageScale(1)
      setPan({ x: 0, y: 0 })
      setWorkspacePan({ x: 0, y: 0 })
      setMessage('')
    }
    image.onerror = () => {
      if(generation!==pendingSource.current.generation)return
      URL.revokeObjectURL(url);pendingSource.current.url='';setLoading(false)
      setMessage('无法读取这张图片，请选择浏览器支持的图片格式。')
    }
    image.src = url
  }

  const clearImage = () => {
    ++pendingSource.current.generation
    if(pendingSource.current.url)URL.revokeObjectURL(pendingSource.current.url)
    pendingSource.current.url=''
    if(sourceUrl.current)URL.revokeObjectURL(sourceUrl.current)
    sourceUrl.current=''
    processor.current?.cancel()
    coverageCanvas.current=null;drag.current=null
    setLoading(false);setSource(null);setFileName('图片');setProcessing(false)
    setCanvasTurned(false);setRotationOffset(0);setFitMode('crop');setImageScale(1)
    setPan({x:0,y:0});setWorkspacePan({x:0,y:0})
    setCompareHeld(false);setCropPreview(false);setSendDialog(false);setMessage('')
    // Only this browser's in-memory image draft. Never reload or touch BLE.
    Object.assign(imageEditorDraft,{source:null,sourceUrl:'',fileName:'图片'})
  }

  const rotate = (direction: -1 | 1) => {
    processor.current?.cancel()
    setProcessing(true)
    setRotationOffset((value) => (value + direction * 90 + 360) % 360)
    setPan({ x: 0, y: 0 })
  }

  const rotateCanvas = () => {
    processor.current?.cancel()
    setProcessing(true)
    setCanvasTurned(value => !value)
    // Swap the frame, not the source pixels or their rotation. Refit the image
    // and re-anchor the badge to the new reading frame's bottom-right corner.
    setImageScale(1);setPan({x:0,y:0});setWorkspacePan({x:0,y:0})
    setCropPreview(true)
  }

  const limitPan = useCallback((value: { x: number; y: number }, scale: number) => {
    if (!source) return value
    const size = imageDrawSize(source.naturalWidth, source.naturalHeight, target.width, target.height, rotation, fitMode, scale)
    return clampImagePan(value, size, target, rotation)
  }, [fitMode, rotation, source, target])

  const zoomImage = (nextValue: number, point = { x: 0, y: 0 }) => {
    const nextScale = clampImageScale(nextValue)
    setPan((value) => limitPan(zoomPanAtPoint(value, imageScale, nextScale, point), nextScale))
    setImageScale(nextScale)
    setCropPreview(true)
  }

  useEffect(() => {
    const frame = frameElement.current
    if (!frame || !source) return
    const handleWheel = (event: WheelEvent) => {
      event.preventDefault()
      event.stopPropagation()
      const rect = frame.getBoundingClientRect()
      if (!rect.width || !rect.height) return
      const point = {
        x: (event.clientX - rect.left) * target.width / rect.width - target.width / 2,
        y: (event.clientY - rect.top) * target.height / rect.height - target.height / 2,
      }
      const nextScale = clampImageScale(imageScale * Math.exp(-event.deltaY * .0012))
      setPan((value) => limitPan(zoomPanAtPoint(value, imageScale, nextScale, point), nextScale))
      setImageScale(nextScale)
      setCropPreview(true)
    }
    frame.addEventListener('wheel', handleWheel, { passive: false })
    return () => frame.removeEventListener('wheel', handleWheel)
  }, [imageScale, limitPan, source, target.height, target.width])

  const pointerMove = (event: React.PointerEvent) => {
    if (!drag.current) return
    if (drag.current.mode === 'workspace') {
      const dx = event.clientX - drag.current.x
      const dy = event.clientY - drag.current.y
      drag.current = { ...drag.current, x: event.clientX, y: event.clientY, moved: true }
      setWorkspacePan((value) => ({ x: value.x + dx, y: value.y + dy }))
      return
    }
    const rect = frameElement.current?.getBoundingClientRect()
    if (!rect?.width || !rect.height) return
    const dx = (event.clientX - drag.current.x) * target.width / rect.width
    const dy = (event.clientY - drag.current.y) * target.height / rect.height
    drag.current = { ...drag.current, x: event.clientX, y: event.clientY, moved: true }
    setPan((value) => limitPan({ x: value.x + dx, y: value.y + dy }, imageScale))
  }

  const finishDrag = (event: React.PointerEvent) => {
    const moved = drag.current?.moved
    drag.current = null
    if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId)
    if (!moved || !processor.current?.busy) setCropPreview(false)
  }

  const sendNfcImage = async () => {
    const canvas = deviceCanvas.current
    if (!canvas || !source || processing || loading) return
    const frame = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height)
    const result = await imageNfc.run('NFC 传图', { kind: 'image', frame, color: target.color })
    if (result?.screen) selectNfcScreen(result.screen)
  }

  const selectNfcScreen = (screen: SerialScreen) => {
    if (source && (target.width !== screen.width || target.height !== screen.height || target.color !== screen.color)) {
      processor.current?.cancel()
      setProcessing(true)
    }
    setNfcScreen(screen)
    setTargetResolution(`${screen.width}x${screen.height}`)
    setSimulatedColor(screen.color)
    setCanvasTurned(false)
  }

  const download = () => {
    if (!processedCanvas.current) return
    const anchor = document.createElement('a')
    anchor.download = `${fileName.replace(/\.[^.]+$/, '') || 'inktag'}-${target.width}x${target.height}.png`
    anchor.href = processedCanvas.current.toDataURL('image/png')
    anchor.click()
  }

  const exportButton = <button className="button secondary" disabled={!source || processing} onClick={download}><Download />导出效果</button>

  return (
    <div className="page image-page">
      <PageHeader title="图片" actions={<>
        <input ref={fileInput} hidden type="file" accept="image/*" onChange={(event) => { const file = event.target.files?.[0]; event.currentTarget.value = ''; loadFile(file) }} />
        <button className="button secondary image-page-action" title="选择图片" aria-label="选择图片" onClick={() => fileInput.current?.click()}><ImagePlus /><span>选择图片</span><span className="mobile-action-label" aria-hidden="true">选图</span></button>
        <button className="button secondary image-page-action" title="清除图片" aria-label="清除图片" disabled={(!source&&!loading)||!canRunManualImageOperation(state.action)} onClick={clearImage}><Trash2 /><span>清除图片</span><span className="mobile-action-label" aria-hidden="true">清除</span></button>
        <button className="button primary image-page-action" title="蓝牙传图" disabled={!source || !connected || Boolean(nfcScreen) || !canRunManualImageOperation(state.action) || processing || loading} onClick={() => setSendDialog(true)}><Send /><span>蓝牙传图</span></button>
        {webSerial() && <button className="button secondary image-page-action image-page-nfc-action" title="NFC 传图" aria-label="NFC 传图" disabled={!source || processing || loading} onClick={sendNfcImage}><Nfc /><span>NFC 传图</span></button>}
      </>} />
      {imageNfc.operation && <NfcOperationDialog operation={imageNfc.operation} close={imageNfc.close} />}
      {showEmptySlideshowNotice && <aside className="notice warning image-route-notice"><AlertTriangle /><div><strong>设备中还没有可轮播图片</strong><span>在下面选择一张图片，发送时选择加入或覆盖设备图槽。</span></div></aside>}

      <div className="image-workbench">
        <aside className="image-settings">
          <section className="image-target">
            <NfcScreenControl disabled={!canRunManualImageOperation(state.action)} onScreen={selectNfcScreen} />
            <label>分辨率<select value={selectedResolution} disabled={Boolean(fixedScreen)} onChange={(event) => {
              if (event.target.value === 'bluetooth') setNfcScreen(null)
              else setTargetResolution(event.target.value)
            }}>
              {simulatedResolutions.map((item) => <option key={item.id} value={item.id}>{item.width} × {item.height} · {item.sizes.join(' / ')} 寸</option>)}
              {extraScreen && !simulatedResolutions.some(item => item.id === extraResolution) &&
                <option value={extraResolution}>{extraScreen.width} × {extraScreen.height}</option>}
              {nfcScreen && bluetoothScreen && <option value="bluetooth">当前蓝牙设备</option>}
            </select></label>
            <label>颜色<select value={screenTarget.color} disabled={Boolean(fixedScreen)} onChange={(event) => setSimulatedColor(Number(event.target.value))}>
              {simulatedColors.map(([id, label]) => <option key={id} value={id}>{label}</option>)}
              {extraScreen && !simulatedColors.some(([id]) => id === extraScreen.color) &&
                <option value={extraScreen.color}>{colorCode(extraScreen.color)}</option>}
            </select></label>
            <div className="palette-row" title="实屏近似预览色板">{palette.map((item) => <i key={item.name} style={{ background: `rgb(${item.perceived.join(',')})` }} title={item.name} />)}</div>
            {target.color === 10 && <p className="crop-note">E6 六色图片会按当前屏幕的传输格式自动编码并发送。</p>}
          </section>
          <section className="image-crop-settings"><h2>裁剪与构图</h2><div className="image-fit-control" role="group" aria-label="图片适配方式"><button className={fitMode === 'crop' ? 'active' : ''} disabled={!source} onClick={() => { setFitMode('crop'); setImageScale(1); setPan({ x: 0, y: 0 }); setCropPreview(true) }}><Crop />裁剪</button><button className={fitMode === 'stretch' ? 'active' : ''} disabled={!source} onClick={() => { setFitMode('stretch'); setImageScale(1); setPan({ x: 0, y: 0 }); setCropPreview(true) }}><Expand />拉伸</button></div><div className="crop-actions"><button className="icon-button" disabled={!source} title="向左旋转" onClick={() => rotate(-1)}><RotateCcw /></button><button className="icon-button" disabled={!source} title="向右旋转" onClick={() => rotate(1)}><RotateCw /></button><button className="icon-button" disabled={!source} title="缩小图片" onClick={() => zoomImage(imageScale / 1.15)}><ZoomOut /></button><button className="icon-button" disabled={!source} title="放大图片" onClick={() => zoomImage(imageScale * 1.15)}><ZoomIn /></button><button className="icon-button" disabled={!source} title="还原图片与画布位置" onClick={() => { setImageScale(1); setPan({ x: 0, y: 0 }); setWorkspacePan({ x: 0, y: 0 }); setRotationOffset(0); setCropPreview(true) }}><RefreshCcw /></button></div><label>图片缩放 <output>{Math.round(imageScale * 100)}%</output><input type="range" min="0.25" max="6" step="0.01" value={imageScale} disabled={!source} onChange={(event) => zoomImage(Number(event.target.value))} /></label><p className="crop-note">框内拖动图片、滚轮缩放；框外拖动工作区。</p></section>
          <section className="image-dither-settings">
            <h2>抖动处理</h2>
            <label>图像预处理<select value={preset} onChange={(event) => setPreset(event.target.value as (typeof processingPresets)[number][0])}>{processingPresets.map(([id, label]) => <option key={id} value={id}>{label}</option>)}</select></label>
            <label>抖动算法<select value={algorithm} onChange={(event) => { const next = event.target.value as ImageAlgorithm; setAlgorithm(next); if (!usesDirectStrength(next)) setStrength((value) => Math.min(value, 1)) }}>{algorithmGroups.map((group) => <optgroup key={group.label} label={group.label}>{group.options.map(([id, label]) => <option key={id} value={id}>{label}</option>)}</optgroup>)}</select></label>
            <label>亮度 <output>{brightness > 0 ? '+' : ''}{brightness}%</output><input type="range" aria-label="图片亮度" min="-100" max="100" step="1" value={brightness} onChange={(event) => setBrightness(Number(event.target.value))} /></label>
            <label>对比度 <output>{contrast.toFixed(1)}</output><input type="range" min="0.5" max="2" step="0.1" value={contrast} onChange={(event) => setContrast(Number(event.target.value))} /></label>
            <label>抖动强度 <output>{directStrength ? strength.toFixed(1) : `${Math.round(strength * 100)}%`}</output><input type="range" min="0" max={directStrength ? 5 : 1} step={directStrength ? .1 : .05} value={strength} disabled={algorithm === 'none'} onChange={(event) => setStrength(Number(event.target.value))} /></label>
            <div className="image-ink-modes">
              <label className="setting-check"><input type="checkbox" checked={heavyInk} onChange={(event) => setHeavyInk(event.target.checked)} />重色模式</label>
              {supportsWarmColors(target.color) && <label className="setting-check"><input type="checkbox" checked={warmColors} onChange={(event) => {
                setProcessing(Boolean(source))
                setWarmColors(event.target.checked)
              }} />暖色模式</label>}
            </div>
            {supportsWarmColors(target.color) && <p className="crop-note">将饱和蓝绿色转为屏幕可显示的暖色，保留肤色和中性色。</p>}
          </section>
        </aside>

        <section className="image-preview-panel">
          <div className="image-preview-toolbar">
            <div className="image-preview-heading">
              <span title={fileName}>{fileName}</span><small>{target.width} × {target.height}</small>
              {exportButton}
            </div>
            <div className="image-preview-actions"><button className="icon-button" title="缩小预览" onClick={() => setPreviewZoom((value) => Math.max(.25, value - .25))}><ZoomOut /></button><output>{Math.round(previewZoom * 100)}%</output><button className="icon-button" title="放大预览" onClick={() => setPreviewZoom((value) => Math.min(4, value + .25))}><ZoomIn /></button><button className="icon-button" title="旋转画布" aria-label="旋转画布" disabled={!source || target.width === target.height} onClick={rotateCanvas}><RotateCw /></button><div className="image-preview-tabs" role="tablist" aria-label="预览画面"><button role="tab" aria-selected={previewMode === 'adjusted'} className={previewMode === 'adjusted' ? 'active' : ''} onClick={() => { setPreviewMode('adjusted'); setCompareHeld(false) }}>处理后</button><button role="tab" aria-selected={previewMode === 'dithered'} className={previewMode === 'dithered' ? 'active' : ''} onClick={() => { setPreviewMode('dithered'); setCompareHeld(false) }}>抖动后</button></div><button className="button secondary compare-button" title={compareLabel} disabled={!source} onPointerDown={() => setCompareHeld(true)} onPointerUp={() => setCompareHeld(false)} onPointerCancel={() => setCompareHeld(false)} onPointerLeave={() => setCompareHeld(false)}><Eye />{compareLabel}</button></div>
          </div>
          {source ? <div ref={viewportElement} className="image-viewport" onPointerDown={(event) => { if (event.button !== 0) return; const mode = frameElement.current?.contains(event.target as Node) ? 'image' : 'workspace'; drag.current = { mode, x: event.clientX, y: event.clientY, moved: false }; if (mode === 'image') setCropPreview(true); event.currentTarget.setPointerCapture(event.pointerId) }} onPointerMove={pointerMove} onPointerUp={finishDrag} onPointerCancel={finishDrag}>
            <div className="image-scene" style={{ width: target.width * displayZoom + sceneMargin, height: target.height * displayZoom + sceneMargin }}>
              <div className="image-crop-bleed" aria-hidden="true"><img src={source.src} alt="" style={{ left: `calc(50% + ${workspacePan.x + pan.x * displayZoom}px)`, top: `calc(50% + ${workspacePan.y + pan.y * displayZoom}px)`, width: (drawSize?.width || 0) * previewZoom, height: (drawSize?.height || 0) * previewZoom, transform: `translate(-50%, -50%) rotate(${rotation}deg)` }} /></div>
              <div ref={frameElement} className={`image-device-frame view-${previewLayer}`} style={{ left: `calc(50% + ${workspacePan.x}px)`, top: `calc(50% + ${workspacePan.y}px)`, width: target.width * displayZoom, height: target.height * displayZoom }}>
                <canvas ref={processedCanvas} className="dithered-layer" aria-label="墨水屏抖动效果" />
                <canvas ref={adjustedCanvas} className="adjusted-layer" aria-label="预处理后的图片" />
                <canvas ref={originalCanvas} className="original-layer" aria-label="裁切后的原始图片" />
                <canvas ref={deviceCanvas} className="device-data-layer" aria-hidden="true" />
                {showBadge && <canvas
                  ref={editionPreviewCanvas}
                  className="edition-preview-layer"
                  aria-label="InkTag 标识预览"
                  style={{
                      left: `${badge.x * displayZoom}px`, top: `${badge.y * displayZoom}px`,
                      right: 'auto', bottom: 'auto',
                      width: `${badge.width * displayZoom}px`, height: `${badge.height * displayZoom}px`,
                      transform:`rotate(${badge.rotation}deg)`,transformOrigin:'0 0',
                  }}
                />}
                {processing && <span className="processing-label">处理中</span>}
              </div>
            </div>
          </div> : <button className="image-drop-zone" onClick={() => fileInput.current?.click()} onDragOver={(event) => event.preventDefault()} onDrop={(event) => { event.preventDefault(); loadFile(event.dataTransfer.files[0]) }}><Upload /><strong>选择或拖入图片</strong><span>PNG、JPEG、WebP 等浏览器支持的格式</span></button>}
          {source && <footer><span>{`拖动图片调整构图，滚轮围绕指针缩放；按住空格${compareLabel.slice(2)}。`}</span><div>{exportButton}</div></footer>}
        </section>
      </div>
      {message && <FeedbackNotice noticeKey={messageRevision}>{message}</FeedbackNotice>}
      <DeviceImageSlots />
      {sendDialog && <SendCanvasDialog title="发送图片" defaultName={fileName} prepareCanvas={() => deviceCanvas.current}
        readingCanvasSize={target}
        canSend={() => Boolean(source && !processing && !loading)} message={setMessage} close={() => setSendDialog(false)} />}
    </div>
  )
}
