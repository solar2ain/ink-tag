import { ensureBundledFont } from '../../data/bundledFonts'
import { creationFontAssets, defaultCreationTypography, loadCreationFont } from '../../data/creationFonts'
import { FeedbackNotice, useFeedbackMessage } from '../../components/FeedbackNotice'
import { useEffect, useRef, useState } from 'react'
import { Link } from 'react-router-dom'
import * as engine from 'leafer-editor'
import { EditorCore, cellFrame, locked, parseDocument, documentPageMode, canSendDocument, type Document, type PageMode, type Kind, type Point, type Selection } from '../../../../packages/inktag-editor-core/src/index'
import { templateDocument, labTemplates } from './templateLibrary'
import { nativeDocument, nativeRaster } from './nativeBridge'
import { resolveLabText } from './textBridge'
import { RichTextEditor } from './RichTextEditor'
import { CellInput, commitCellEdit } from './CellInput'
import { downloadBlob, downloadCanvasPng } from '../../download'
import { richTextRaster, clearTextRasterCache } from './richTextRenderer'
import { brandPreview, overlayBrandPreview } from './brandPreview'
import { mountScene } from './adapter'
import { CanvasImageCache } from './imageRaster'
import { DeviceEffectDialog } from './DeviceEffectDialog'
import { UnsavedCanvasGuard } from './UnsavedCanvasGuard'
import { WeatherControls, defaultWeatherSource } from './WeatherPanel'
import { demoWeather, type WeatherData } from '../../../../packages/inktag-editor-core/src/weather'
import { parseHostScene } from '../../../../packages/inktag-editor-core/src/hostScene'
import { useViewport } from './useViewport'
import { Inspector } from './Inspector'
import { LayerList } from './LayerList'
import { GuideHandles } from './GuideHandles'
import { DeviceControlPicker } from './DeviceControlPicker'
import { createDeviceControl } from './deviceControls'
import { createSlotCarousel } from './slotCarousel'
import { colorModes, inkColor, inksFor } from './palette'
import { ResizableEditorPanels } from '../../components/ResizableEditorPanels'
import { db, type PageKind } from '../../data/db'
import { documentKind } from '../../data/documentLibrary'
import { pageModeLabels, pageModeNotices, staticContentNotice } from '../../data/pageMode'
import { SaveDocumentDialog, type SaveDocumentChoice } from '../../components/SaveDocumentDialog'
import { SendCanvasDialog } from '../../components/SendCanvasDialog'
import { useDeviceStore } from '../../store/device'
import type { ImageSlotContentType } from '../../types'
import { ArrowLeft, Type, ImagePlus, Images, Table2, ListTodo, List, Undo2, Redo2, MousePointer2, Hand, Paintbrush, ZoomIn, ZoomOut, Scan, Maximize2, Save, Download, Send, Eye, CalendarDays, Clock3, QrCode, Minus, ChevronUp, ChevronDown, SquareDashedMousePointer, SlidersHorizontal } from 'lucide-react'
import '../../styles.css'
import '../../studio.css'
import './style.css'

const controls = [
  { kind: 'text', icon: Type, label: '文字' }, { kind: 'image', icon: ImagePlus, label: '图片框' },
  { kind: 'table', icon: Table2, label: '表格' }, { kind: 'todo', icon: ListTodo, label: '待办事项' }, { kind: 'list', icon: List, label: '列表' },
  { kind: 'qr', icon: QrCode, label: '二维码' }, { kind: 'line', icon: Minus, label: '分割线' },
] satisfies { kind: Exclude<Kind,'native' | 'stroke'>; icon: typeof Type; label: string }[]
type LabProps = { initialTemplateId?: string; initialWidth?: number; initialHeight?: number; initialColor?: Document['colorMode']; initialPageMode?: PageMode; savedPageId?: string; sourceTemplateId?: string; embedded?: boolean }
const strokeWidths = [1, 2, 4, 8, 12, 16]
function EraserToolIcon() {
  return <svg aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
    <path d="m9.5 4.5 10 10a2 2 0 0 1 0 2.8l-2.2 2.2H8.1L3.5 15a2 2 0 0 1 0-2.8l7.2-7.7a2 2 0 0 1 2.8 0Z" />
    <path d="m6.7 8.7 9.4 9.4M17.3 19.5H21" />
  </svg>
}
function templateName(id: string) { return labTemplates.find(item => item.id === id)?.name || (id === 'blank' ? '空白页面' : '我的页面') }
export function Lab({ initialTemplateId = 'calendar-native', initialWidth, initialHeight, initialColor, initialPageMode = 'static', savedPageId, sourceTemplateId, embedded = false }: LabProps) {
  const [core, setCore] = useState(() => {
    const document: Document = (initialTemplateId === 'blank' || savedPageId || sourceTemplateId) && initialWidth && initialHeight
      ? { version: 1, width: initialWidth, height: initialHeight, widgets: [], pageMode: initialPageMode, colorMode: 2, footerInset: 0 }
      : templateDocument(initialTemplateId, initialWidth, initialHeight)
    return new EditorCore(initialColor ? { ...document, colorMode: initialColor } : document)
  })
  const [, refresh] = useState(0)
  const savedSnapshot = useRef(JSON.stringify(core.doc))
  const draftKey = JSON.stringify([initialTemplateId, initialWidth, initialHeight, initialColor, initialPageMode, savedPageId, sourceTemplateId])
  const [notice, setNotice, noticeRevision] = useFeedbackMessage('')
  const [template, setTemplate] = useState(initialTemplateId)
  const [pageName, setPageName] = useState(() => templateName(initialTemplateId))
  const [pageKind, setPageKind] = useState<PageKind>(() => documentKind({ templateId: initialTemplateId }))
  const [saveDialog, setSaveDialog] = useState(false)
  const [restoreState, setRestoreState] = useState<'loading' | 'ready' | 'failed'>('loading')
  const restoring = restoreState === 'loading'
  const outputReady = restoreState === 'ready'
  const pageMode = documentPageMode(core.doc), dynamic = pageMode !== 'static'
  const deviceReady = outputReady && canSendDocument(core.doc)
  const deviceBlockReason = dynamic ? pageModeNotices[pageMode] : canSendDocument(core.doc) ? '' : staticContentNotice
  const ownTemplateId = useRef<string | undefined>(undefined)
  const [pageId] = useState(() => savedPageId || `canvas-${crypto.randomUUID()}`)
  const [mobilePanel, setMobilePanel] = useState<'controls' | 'properties' | null>(null)
  const lastMobilePanel = useRef<'controls' | 'properties'>('controls')
  const openMobilePanel = (panel = lastMobilePanel.current) => { lastMobilePanel.current = panel; setMobilePanel(panel) }
  const [sendDialog, setSendDialog] = useState(false)
  const [effectDialog, setEffectDialog] = useState(false)
  const [weatherData, setWeatherData] = useState<WeatherData>()
  const weatherRef = useRef<WeatherData|undefined>(undefined)
  const updateWeather = (data?:WeatherData) => {weatherRef.current=data;setWeatherData(data);scene.current?.render()}
  const [imageRequest,setImageRequest] = useState<{id:string;nonce:number}>()
  const screen = useDeviceStore(state => state.screen)
  const connection = useDeviceStore(state => state.connection)
  const activation = useDeviceStore(state => state.activation)
  const brandVisible = useDeviceStore(state => state.brandVisible)
  const [drawTool, setDrawTool] = useState<'brush' | 'eraser' | null>(null)
  const [brushColor, setBrushColor] = useState('#000000')
  const [brushWidth, setBrushWidth] = useState(2)
  const [eraserWidth, setEraserWidth] = useState(8)
  const drawCanvas = useRef<HTMLCanvasElement>(null)
  const drawing = useRef<{ id: number; points: Point[] } | null>(null)
  const host = useRef<HTMLDivElement>(null)
  const canvasRef = useRef<engine.Leafer | undefined>(undefined)
  const scene = useRef<ReturnType<typeof mountScene> | undefined>(undefined)
  const viewport = useViewport(core.doc.width, core.doc.height, {
    hit: (point, radius) => core.isBlank(point, radius),
    click: () => { core.deselect(); scene.current?.render(); refresh(n => n + 1) },
  })
  const redraw = () => { scene.current?.render(); refresh(n => n + 1) }
  const act = (fn: () => unknown) => { const message = fn(); if (typeof message === 'string') setNotice(message); redraw() }
  const drawableHeight = core.doc.height - (core.doc.footerInset ?? 18)
  const effectiveName = pageName.trim() || templateName(template)
  const colorLabel = colorModes.find(mode => mode.value === (core.doc.colorMode ?? 2))?.label.split(' · ')[0] || '三色'
  const strokeWidth = drawTool === 'eraser' ? eraserWidth : brushWidth
  const drawPoint = (event: React.PointerEvent<HTMLCanvasElement>): Point => {
    const bounds = event.currentTarget.getBoundingClientRect()
    return { x: Math.max(0, Math.min(core.doc.width - 1, Math.round((event.clientX - bounds.left) * core.doc.width / bounds.width))),
      y: Math.max(0, Math.min(drawableHeight - 1, Math.round((event.clientY - bounds.top) * drawableHeight / bounds.height))) }
  }
  const finishDrawing = (event: React.PointerEvent<HTMLCanvasElement>, commit: boolean) => {
    event.stopPropagation()
    const active = drawing.current
    if (!active || active.id !== event.pointerId) return
    drawing.current = null
    drawCanvas.current?.getContext('2d')?.clearRect(0, 0, core.doc.width, drawableHeight)
    if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId)
    if (commit) act(() => core.addStroke(active.points, drawTool === 'eraser' ? '#ffffff' : inkColor(brushColor, core.doc.colorMode), strokeWidth, drawTool === 'eraser'))
  }
  const addNative = (page: 'calendar' | 'clock') => act(() => {
    if (!dynamic) return staticContentNotice
    const source = nativeDocument(page,core.doc.width,core.doc.height).widgets.find(w => w.native?.role === page)!
    let id = `${page}-1`, i = 1; while (core.doc.widgets.some(w => w.id === id)) id = `${page}-${++i}`
    const widget = { ...source, id, height: Math.min(source.height,core.doc.height - (core.doc.footerInset ?? 18) - source.y) }
    core.change(doc => doc.widgets.push(widget)); core.select(id)
  })
  useEffect(() => {
    const canvas = new engine.Leafer({ view: host.current!, width: viewport.size.width, height: viewport.size.height,
      move: { disabled: true }, zoom: { disabled: true } })
    canvasRef.current = canvas; scene.current = mountScene(engine, canvas, core, () => refresh(n => n + 1), 1, nativeRaster, resolveLabText,richTextRaster,
      (width,height)=>brandPreview(useDeviceStore.getState(),width,height,core.doc.colorMode??2),false,
      new CanvasImageCache(()=>scene.current?.render(),error=>setNotice(error.message)),()=>weatherRef.current ?? {...demoWeather(),label:core.doc.weatherSource?.label??defaultWeatherSource.label})
    scene.current.viewport(viewport.view)
    return () => { scene.current?.destroy(); scene.current = undefined; canvas.destroy(); canvasRef.current = undefined }
  }, [core])
  useEffect(() => { scene.current?.render() }, [activation, brandVisible, connection])
  useEffect(() => { canvasRef.current?.resize(viewport.size) }, [viewport.size, core])
  useEffect(() => { scene.current?.viewport(viewport.view) }, [viewport.view, core])
  useEffect(() => {
    const { width, height } = viewport.size
    const zoom = Math.min(1, (width - 20) / core.doc.width, (height - 20) / core.doc.height)
    viewport.fit(Math.max(.1, zoom))
  }, [core, viewport.size.width, viewport.size.height])
  const needsCalligraphy = /Ma Shan Zheng|ma-shan-zheng/.test(JSON.stringify(core.doc))
  const pixelAssetsKey = (pageMode==='static' ? creationFontAssets(core.doc) : []).join(' ')
  useEffect(() => {
    let disposed = false
    const loaded = () => { if (!disposed) { clearTextRasterCache(); redraw() } }
    document.fonts.addEventListener('loadingdone', loaded)
    if (needsCalligraphy) void ensureBundledFont('Ma Shan Zheng').then(loaded, loaded)
    return () => { disposed = true; document.fonts.removeEventListener('loadingdone', loaded) }
  }, [needsCalligraphy])
  useEffect(() => {
    if (!pixelAssetsKey) return
    let active = true
    void Promise.all(creationFontAssets(core.doc).map(loadCreationFont)).then(() => {
      if (active) redraw()
    }, (error: Error) => {
      if (active) { redraw(); setNotice(error.message) }
    })
    return () => { active = false }
  }, [core, pixelAssetsKey])
  const s = core.selection, w = core.selected
  const cell = w && !locked(w) && !core.multiple && s?.row !== undefined && s.col !== undefined ? w.cells[s.row]?.[s.col] : undefined
  const frame = w && cell ? cellFrame(w, s!.row!, s!.col!) : undefined
  const commitCell = (target: Selection, value: string, next?: 'tab' | 'row') => act(() => commitCellEdit(core, target, value, next))
  const openSave = () => { if (outputReady) setSaveDialog(true) }
  const save = async (choice: SaveDocumentChoice) => {
    // A failed/missing source never authorizes replacing its existing page ID.
    if (!outputReady) return false
    try {
      const document = JSON.stringify(core.doc)
      let preview: string | undefined
      try { preview = (await exportCanvas(choice.target === 'template')).toDataURL('image/png') } catch { /* The editable document can still be saved. */ }
      if (document !== JSON.stringify(core.doc)) preview = undefined
      const record = { templateId: template, name: choice.name, kind: choice.kind,
        width: core.doc.width, height: core.doc.height, document, preview, updatedAt: Date.now() }
      if (choice.target === 'template') {
        const id = ownTemplateId.current ?? `template-${crypto.randomUUID()}`
        await db.templates.put({ ...record, id }); ownTemplateId.current = id
        setNotice('模板已保存到「创作 · 我的模板」（不上传设备）')
      } else {
        await db.pages.put({ ...record, id: pageId })
        setNotice(preview ? '页面已保存到本机资源库（不上传设备）' : '页面已保存，预览暂未生成（不上传设备）')
      }
      await db.canvasDrafts.delete(draftKey)
      savedSnapshot.current = document
      setPageName(choice.name); setPageKind(choice.kind)
      refresh(n => n + 1)
      return true
    } catch { setNotice('保存失败：浏览器存储空间不足，请减少图片大小。'); return false }
  }
  const exportCanvas = async (includeDeviceMark = false) => {
    const png = await scene.current?.exportPng(includeDeviceMark)
    if (!png) throw new Error('画板尚未准备完成')
    const bitmap = await createImageBitmap(png)
    try {
      if (bitmap.width !== core.doc.width || bitmap.height !== core.doc.height) throw new Error('导出尺寸与画板不一致')
      const target = document.createElement('canvas')
      target.width = bitmap.width; target.height = bitmap.height
      const context = target.getContext('2d')
      if (!context) throw new Error('无法读取导出画布')
      context.drawImage(bitmap, 0, 0)
      return target
    } finally { bitmap.close() }
  }
  const download = async () => {
    try {
      const target = await exportCanvas()
      await downloadCanvasPng(target, `${effectiveName}.png`)
      setNotice(dynamic ? '已导出预览 PNG，仅包含当前画面，不会自动更新。' : 'PNG 已按原始分辨率导出，设备标识占位不包含在图片中。')
    } catch (error) { setNotice(error instanceof Error ? error.message : String(error)) }
  }
  const exportHost = () => {
    try {
      const value=JSON.stringify(parseHostScene(JSON.stringify(core.doc)),null,2)
      if(new Blob([value]).size>8*1024*1024)throw new Error('原稿超过 8 MiB，请缩小图片后再导出')
      downloadBlob(new Blob([value],{type:'application/json'}),`${effectiveName}.inktag.json`)
      setNotice('原稿已导出。CLI：inktag host import <原稿文件> --config weather.toml；任务默认关闭。')
    } catch(error) {setNotice(error instanceof Error?error.message:'主机原稿导出失败')}
  }
  const restore = async (cancelled: () => boolean) => {
    try {
      const record = sourceTemplateId ? await db.templates.get(sourceTemplateId) : await db.pages.get(pageId)
      if (!record) throw new Error('saved document missing')
      const original = parseDocument(record.document)
      if (original.width !== record.width || original.height !== record.height || original.width !== core.doc.width || original.height !== core.doc.height) throw new Error('page geometry mismatch')
      const draft = await db.canvasDrafts.get(draftKey).catch(() => undefined)
      let source = record, doc = original
      if (draft && draft.updatedAt > record.updatedAt) {
        try {
          const candidate = parseDocument(draft.document)
          if (candidate.width === original.width && candidate.height === original.height) {source=draft;doc=candidate}
        } catch { /* An unreadable draft never blocks a valid saved page or rewrites either record. */ }
      }
      if (cancelled()) return
      setCore(new EditorCore(doc)); setTemplate(record.templateId)
      savedSnapshot.current = JSON.stringify(original)
      setPageName(source.name || templateName(record.templateId))
      setPageKind(documentKind(source))
      setNotice(source === draft ? '已恢复本机暂存草稿，尚未保存为页面' : sourceTemplateId ? '已从模板创建独立页面，原模板不会被修改' : '已打开本地页面')
      setRestoreState('ready')
    } catch {
      if (cancelled()) return
      setRestoreState('failed')
      setNotice('原稿未能读取或格式不兼容，保存、导出和发送已禁用。请返回资源库后重试，原稿不会被覆盖。')
    }
  }
  useEffect(() => {
    let disposed = false
    if (savedPageId || sourceTemplateId) void restore(() => disposed)
    else void db.canvasDrafts.get(draftKey).then(record => {
      if (!record || disposed) return
      if (savedSnapshot.current !== JSON.stringify(core.doc)) { setNotice('已开始编辑，未覆盖暂存草稿。'); return }
      const doc = parseDocument(record.document)
      if (doc.width !== core.doc.width || doc.height !== core.doc.height) return
      setCore(new EditorCore(doc)); setPageName(record.name); setPageKind(documentKind(record)); setNotice('已恢复本机暂存草稿，尚未保存为页面')
    }).catch(() => { if (!disposed) setNotice('草稿未能读取；原草稿保留，请勿关闭未保存的修改。') }).finally(() => { if(!disposed)setRestoreState('ready') })
    return () => { disposed = true }
  }, [savedPageId, sourceTemplateId])
  const dirty = outputReady && savedSnapshot.current !== JSON.stringify(core.doc)
  const saveDraft = async () => {
    const document = JSON.stringify(core.doc)
    await db.canvasDrafts.put({id:draftKey,templateId:template,name:effectiveName,kind:pageKind,width:core.doc.width,height:core.doc.height,document,updatedAt:Date.now()})
    return document === JSON.stringify(core.doc)
  }
  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      if (document.querySelector('[aria-modal="true"]')) return
      if ((event.target as HTMLElement).closest('input,textarea,select,[contenteditable]')) {
        if(!event.isComposing&&(event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='s'){event.preventDefault();(event.target as HTMLElement).blur();openSave()}
        return
      }
      const mod = event.ctrlKey || event.metaKey, k = event.key.toLowerCase()
      let action: (() => unknown) | undefined
      if (mod && k === 'z') action = () => event.shiftKey ? core.redo() : core.undo()
      else if (mod && k === 'y') action = () => core.redo()
      else if (mod && k === 's') action = openSave
      else if (mod && k === 'c') action = () => core.copy()
      else if (mod && k === 'v') action = () => core.paste()
      else if (mod && k === 'd') action = () => { core.copy(); core.paste() }
      else if (mod && k === 'a') action = () => core.selectAll()
      else if (mod && k === 'g') action = () => event.shiftKey ? core.ungroup() : core.group()
      else if (mod && event.shiftKey && k === 'l') action = () => core.setLocked(!core.selectionLocked)
      else if (event.key === 'Escape') action = () => core.deselect()
      else if (['Delete', 'Backspace'].includes(event.key)) action = () => core.remove()
      else if (!mod && k === 'h') action = () => { setDrawTool(null); viewport.setHand(true) }
      else if (!mod && k === 'v') action = () => { setDrawTool(null); viewport.setHand(false) }
      else if (event.key.startsWith('Arrow')) {
        const step = event.shiftKey ? 10 : 1
        action = () => core.nudge(event.key === 'ArrowLeft' ? -step : event.key === 'ArrowRight' ? step : 0,
          event.key === 'ArrowUp' ? -step : event.key === 'ArrowDown' ? step : 0)
      }
      if (action) { event.preventDefault(); act(action) }
    }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [core, effectiveName, restoreState])
  return <main className={`editor-page leafer-lab${embedded ? ' leafer-embedded' : ''}${mobilePanel ? ` mobile-panel-${mobilePanel}` : ''}`}>
    <header className="native-template-header">
      <Link to="/create" className="icon-button" aria-label="返回模板" title="返回模板"><ArrowLeft /></Link>
      <div className="native-template-title"><strong>{effectiveName}</strong><span>{core.doc.width} × {core.doc.height} · {colorLabel} · {pageModeLabels[pageMode]}</span></div>
      <div className="native-template-history"><button className="icon-button" aria-label="撤销" title="撤销" disabled={!core.canUndo} onClick={() => act(() => core.undo())}><Undo2 /></button><button className="icon-button" aria-label="重做" title="重做" disabled={!core.canRedo} onClick={() => act(() => core.redo())}><Redo2 /></button></div>
      <button className="button secondary" aria-label="保存作品" title={outputReady ? '保存为页面或模板' : restoring ? '正在读取原稿' : '原稿读取失败，请返回资源库重试'} disabled={!outputReady} onClick={openSave}><Save /><span>保存</span></button>
      <button className="button secondary" aria-label="下载 PNG" title="下载 PNG" disabled={!outputReady} onClick={() => void download()}><Download /><span>PNG</span></button>
      <button className="button secondary" aria-label="设备效果预览" title="设备效果预览" disabled={!outputReady} onClick={() => setEffectDialog(true)}><Eye /><span>设备效果</span></button>
      <button className="button primary" aria-label="发送到设备" title={deviceBlockReason || '发送当前画面，无需先保存为页面'} disabled={!deviceReady} onClick={() => setSendDialog(true)}><Send /><span>发送到设备</span></button>
    </header>
    {outputReady && deviceBlockReason && <p className="page-mode-notice" role="note">{deviceBlockReason}</p>}
    <nav className="canvas-view-toolbar lab-toolbar" aria-label="画板工具栏">
      <div className="lab-toolbar-row lab-toolbar-primary">
        <div className="tools lab-mode-tools"><button aria-label="选择" aria-pressed={!viewport.hand && !drawTool} onClick={() => { setDrawTool(null); viewport.setHand(false) }}><MousePointer2 /><span>选择</span></button><button aria-label="平移" aria-pressed={viewport.hand && !drawTool} onClick={() => { setDrawTool(null); viewport.setHand(true) }}><Hand /><span>平移</span></button><button aria-label="画笔" aria-pressed={drawTool === 'brush'} onClick={() => { viewport.setHand(false); setDrawTool('brush') }}><Paintbrush /><span>画笔</span></button><button aria-label="橡皮" aria-pressed={drawTool === 'eraser'} onClick={() => { viewport.setHand(false); setDrawTool('eraser') }}><EraserToolIcon /><span>橡皮</span></button></div>
        <div className="tools zoom-tools"><button aria-label="缩小视图" onClick={() => viewport.zoom(1 / 1.2)}><ZoomOut /></button><output>{Math.round(viewport.view.zoom * 100)}%</output><button aria-label="放大视图" onClick={() => viewport.zoom(1.2)}><ZoomIn /></button><button aria-label="原始大小" title="原始大小 1:1" onClick={() => viewport.fit(1)}><span className="zoom-wide-label">1:1</span><Scan className="zoom-compact-icon" /></button><button aria-label="适应画板" title="适应画板" onClick={() => viewport.fit()}><span className="zoom-wide-label">适应</span><Maximize2 className="zoom-compact-icon" /></button></div>
      </div>
      {pageMode==='host-dynamic'&&<div className="lab-toolbar-row host-task-toolbar"><button disabled={!outputReady} onClick={exportHost}>导出主机任务</button><span>原稿交给 CLI 定时渲染；不会启用任务或推送设备。</span></div>}
      {drawTool && <div className="lab-toolbar-row lab-toolbar-options">
        <div className="tools draw-options">
          {drawTool === 'brush' && <div className="draw-swatches" role="group" aria-label="画笔颜色">{inksFor(core.doc.colorMode).map(ink => <button key={ink.color} className="draw-swatch" aria-label={`画笔颜色 ${ink.name}`} title={ink.name} aria-pressed={inkColor(brushColor, core.doc.colorMode) === ink.color} onClick={() => setBrushColor(ink.color)}><i style={{ background: ink.color }} /></button>)}</div>}
          <div className="draw-sizes" role="group" aria-label={drawTool === 'eraser' ? '橡皮粗细' : '画笔粗细'}>{strokeWidths.map(width => <button key={width} className="draw-size" aria-label={`${drawTool === 'eraser' ? '橡皮' : '画笔'} ${width} 像素`} title={`${width} px`} aria-pressed={strokeWidth === width} onClick={() => drawTool === 'eraser' ? setEraserWidth(width) : setBrushWidth(width)}><i style={{ width: Math.min(15, Math.max(3, width + 1)), height: Math.min(15, Math.max(3, width + 1)) }} /><small>{width}</small></button>)}</div>
        </div>
      </div>}
    </nav>
    <ResizableEditorPanels className="editor-layout">
      <aside id="canvas-mobile-controls" className="editor-tools lab-tools"><h2>添加控件</h2><div className="control-library">{pageMode==='dynamic' && <><button onClick={() => { addNative('calendar'); setMobilePanel(null) }}><CalendarDays />日历</button><button onClick={() => { addNative('clock'); setMobilePanel(null) }}><Clock3 />时钟</button>
        <DeviceControlPicker add={id => {if (!dynamic) return; act(() => {const widget=createDeviceControl(id,core.doc);core.change(doc=>doc.widgets.push(widget));core.select(widget.id);return `已添加${widget.name}`});setMobilePanel(null)}} />
        <button onClick={() => {act(() => {const widget=createSlotCarousel(core.doc);core.change(doc=>doc.widgets.push(widget));core.select(widget.id);return '已添加图槽轮播（仅配置预览）'});setMobilePanel(null)}}><Images />图槽轮播</button></>}
        {controls.map(item => <button key={item.kind} onClick={() => {
          const typography = pageMode==='static' && ['text','list','todo','table'].includes(item.kind)
            ? defaultCreationTypography(core.doc.width, core.doc.height) : undefined
          act(() => core.add(item.kind, typography)); setMobilePanel(null)
        }}><item.icon />{item.label}</button>)}{pageMode==='host-dynamic'&&<WeatherControls core={core} act={act}/>}</div>
        <LayerList core={core} act={act} />
        <div className="locked">⌑ InkTag 标识 <small>{connection === 'connected' && activation.phase === 'active' && activation.edition === 'advanced' ? brandVisible === false ? 'Pro · 已隐藏' : 'Pro · 跟随设备设置' : 'base · 设备固定覆盖层'}</small></div><p>首次点击选中控件，再次点击编辑内容。拖动任意角调整大小，保持字号。双击图片框添加或更换图片。Shift 点击多选，空白处拖动框选。</p></aside>
      <div className="lab-canvas-workspace">
      <div ref={viewport.stage} className={`stage ${viewport.panning || viewport.dragging ? 'panning' : ''} ${viewport.overBlank ? 'blank-pan' : ''}`} {...viewport.handlers}>
        <div className="canvas-host" ref={host} aria-label="创作画板" onDoubleClick={()=>{
          const image=core.selected
          if(image?.kind==='image'&&!image.imageSlots&&!locked(image)&&!core.multiple){setImageRequest({id:image.id,nonce:Date.now()});setMobilePanel('properties')}
        }} />
        {!drawTool && !viewport.panning && <GuideHandles core={core} view={viewport.view} size={viewport.size} redraw={redraw} />}
        {w && !locked(w) && !core.multiple && !core.multiSelect && !w.native && ['table', 'todo', 'list'].includes(w.kind || '') && !viewport.panning && <div className="canvas-widget-actions" data-stage-control role="toolbar" aria-label={`${w.name}快捷操作`}
          style={{ left: Math.max(8, Math.min(viewport.size.width - 245, viewport.view.x + w.x * viewport.view.zoom)),
            top: Math.max(8, viewport.view.y + w.y * viewport.view.zoom - 40) }}
          onPointerDown={event => event.stopPropagation()} onPointerMove={event => event.stopPropagation()} onPointerUp={event => event.stopPropagation()}>
          <button onClick={() => act(() => { core.selection = { id: w.id, row: 0, col: 0 } })}>编辑格子</button>
          <button onClick={() => act(() => core.structure('row'))}>＋{w.kind === 'table' ? '行' : '项'}</button>
          <button disabled={w.cells.length <= 1} onClick={() => act(() => core.structure('row', true))}>−{w.kind === 'table' ? '行' : '项'}</button>
          {w.kind === 'table' && <><button onClick={() => act(() => core.structure('col'))}>＋列</button><button disabled={w.cells[0].length <= 1} onClick={() => act(() => core.structure('col', true))}>−列</button></>}
        </div>}
        {drawTool && <canvas ref={drawCanvas} className="draw-input-layer" aria-label={drawTool === 'eraser' ? '橡皮擦画布' : '自由画笔画布'}
          width={core.doc.width} height={drawableHeight}
          style={{ left: viewport.view.x, top: viewport.view.y, width: core.doc.width * viewport.view.zoom, height: drawableHeight * viewport.view.zoom }}
          onPointerDown={event => { if (event.button !== 0) return; event.stopPropagation(); const point = drawPoint(event)
            drawing.current = { id: event.pointerId, points: [point] }; event.currentTarget.setPointerCapture(event.pointerId)
            const ctx = event.currentTarget.getContext('2d')!; ctx.fillStyle = drawTool === 'eraser' ? '#ffffff' : inkColor(brushColor, core.doc.colorMode)
            ctx.beginPath(); ctx.arc(point.x, point.y, strokeWidth / 2, 0, Math.PI * 2); ctx.fill() }}
          onPointerMove={event => { event.stopPropagation(); const active = drawing.current; if (!active || active.id !== event.pointerId) return
            const point = drawPoint(event), previous = active.points.at(-1)!
            if (point.x === previous.x && point.y === previous.y || active.points.length >= 1024) return
            active.points.push(point); const ctx = event.currentTarget.getContext('2d')!; ctx.strokeStyle = drawTool === 'eraser' ? '#ffffff' : inkColor(brushColor, core.doc.colorMode)
            ctx.lineWidth = strokeWidth; ctx.lineCap = 'round'; ctx.lineJoin = 'round'; ctx.beginPath(); ctx.moveTo(previous.x, previous.y); ctx.lineTo(point.x, point.y); ctx.stroke() }}
          onPointerUp={event => finishDrawing(event, true)} onPointerCancel={event => finishDrawing(event, false)} />}
        {frame && (cell?.richText||w?.kind==='text') && !viewport.panning && <div className="canvas-text-popover" data-text-popover role="dialog" aria-label="画布文字编辑器"
          style={{left:Math.max(8,Math.min(viewport.size.width-320,viewport.view.x+frame.x*viewport.view.zoom)),top:Math.max(8,Math.min(viewport.size.height-310,viewport.view.y+(frame.y+frame.height)*viewport.view.zoom+8))}}
          onPointerDown={e=>e.stopPropagation()} onPointerMove={e=>e.stopPropagation()} onPointerUp={e=>e.stopPropagation()}>
          <header><strong>编辑文字</strong><button onClick={()=>act(()=>core.select(w!.id))}>完成</button></header>
          <RichTextEditor core={core} act={act} canvas/>
        </div>}
        {frame && !cell?.richText && !viewport.panning && <div className="cell-editor" style={{ left: viewport.view.x + frame.x * viewport.view.zoom, top: viewport.view.y + frame.y * viewport.view.zoom,
          width: frame.width * viewport.view.zoom, height: frame.height * viewport.view.zoom, fontSize: Math.min(28, (cell?.style?.fontSize || w!.style.fontSize) * viewport.view.zoom) }}>
          <CellInput key={`${s!.id}-${s!.row}-${s!.col}`} text={cell!.text} selection={{ ...s! }} commit={commitCell} close={() => act(() => core.select(w!.id))} />
        </div>}
      </div>
      <nav className="mobile-panel-dock" aria-label="编辑面板">
        {!mobilePanel && <button className="mobile-multi-select" aria-label="多选图层" aria-pressed={core.multiSelect} onClick={() => { setDrawTool(null); viewport.setHand(false); act(() => { core.multiSelect = !core.multiSelect; if (core.selection) core.selectMany(core.selectedWidgets.map(w => w.id)) }) }}><SquareDashedMousePointer /><span>多选</span></button>}
        {!mobilePanel && core.selectedWidgets.length > 0 && <button className="mobile-selection-properties" aria-label="编辑所选图层" onClick={() => openMobilePanel('properties')}><SlidersHorizontal /><span>{core.selectedWidgets.length} 个已选</span></button>}
        {mobilePanel && <div className="mobile-panel-switch" role="group" aria-label="面板内容">
          <button aria-pressed={mobilePanel === 'controls'} aria-controls="canvas-mobile-controls" onClick={() => openMobilePanel('controls')}>控件与图层</button>
          <button aria-pressed={mobilePanel === 'properties'} aria-controls="canvas-mobile-properties" onClick={() => openMobilePanel('properties')}>属性</button>
        </div>}
        <button key="toggle" className="mobile-panel-toggle" aria-label={mobilePanel ? '关闭编辑面板' : '打开编辑面板'} aria-expanded={Boolean(mobilePanel)}
          aria-controls={mobilePanel === 'properties' ? 'canvas-mobile-properties' : 'canvas-mobile-controls'} onClick={() => mobilePanel ? setMobilePanel(null) : openMobilePanel()}>
          {mobilePanel ? <ChevronDown /> : <><ChevronUp /><span>控件与属性</span></>}
        </button>
      </nav>
      </div>
      <Inspector core={core} act={act} notice={setNotice} imageRequest={imageRequest} weather={weatherData} onWeather={updateWeather}/>
    </ResizableEditorPanels>
    {notice && <FeedbackNotice noticeKey={noticeRevision}>{notice}</FeedbackNotice>}
    <footer><span>选择控件或使用画笔开始创作</span><span>灰色区域拖动平移 · 滚轮缩放 · 平移工具可拖动整个画板</span></footer>
    <UnsavedCanvasGuard dirty={dirty} save={async () => await save({target:'page',name:effectiveName,kind:pageKind}) && savedSnapshot.current === JSON.stringify(core.doc)}
      draft={saveDraft} discard={async () => {await db.canvasDrafts.delete(draftKey);return true}} />
    {saveDialog && <SaveDocumentDialog name={effectiveName} kind={pageKind} pageMode={pageMode} save={save} close={() => { setSaveDialog(false); requestAnimationFrame(() => host.current?.closest('.leafer-lab')?.querySelector<HTMLButtonElement>('button[aria-label="保存作品"]')?.focus()) }} />}
    {effectDialog && <DeviceEffectDialog prepareCanvas={() => exportCanvas()} overlay={frame=>overlayBrandPreview(frame,useDeviceStore.getState(),core.doc.colorMode??2)} color={core.doc.colorMode ?? 2} screen={connection === 'connected' ? screen : null} close={() => setEffectDialog(false)} />}
    {sendDialog && <SendCanvasDialog defaultName={effectiveName} prepareCanvas={() => exportCanvas()}
      contentType={['schedule', 'todo', 'business', 'wifi'].includes(pageKind) ? pageKind as ImageSlotContentType : 'page'}
      canSend={() => outputReady && canSendDocument(core.doc)} message={setNotice} close={() => setSendDialog(false)} />}
  </main>
}
