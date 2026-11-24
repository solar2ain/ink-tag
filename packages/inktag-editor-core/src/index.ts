/** Experimental fixed-pixel editor model. No browser, canvas engine or wx dependency. */
import { alignmentOffset, boundsOf, selectionUnits, snapFrame, type Alignment, type Guide, type SnapLine } from './selectionGeometry'
import { weatherFields, validWeatherSource, type WeatherBinding, type WeatherSource } from './weather'
export { boundsOf, selectionUnits, type Alignment, type Guide, type SnapLine } from './selectionGeometry'
export type Frame = { x: number; y: number; width: number; height: number }
export type DeviceFontId = 'wqy9-lunar' | 'wqy12-lunar' | 'helv-bold-14-num' | 'helv-bold-18-num'
export type Style = { fontSize: number; color: string; bold: boolean; align?: 'left' | 'center' | 'right'; fontFamily?: string; fontId?: DeviceFontId; background?: string; borderColor?: string }
export type TextRun = { text: string; style?: Partial<Style>; gap?: number; baselineShift?: number;
  /** Exact imported pixel-font settings, independent of the source page. */
  fontId?: 'wqy9-lunar' | 'wqy12-lunar' | 'helv-bold-14-num' | 'helv-bold-18-num'; scale?: number; raster?: 'nearest' | 'legacy' | 'coverage' | 'vertical-coverage' | 'strokes' }
export type RichText = { runs: TextRun[]; padding?: number; verticalAlign?: 'top' | 'middle' | 'bottom'; preset?: { id: string; signature: string } }
export type Cell = { text: string; checked?: boolean; style?: Partial<Style>; richText?: RichText }
export type Kind = 'text' | 'table' | 'todo' | 'list' | 'image' | 'qr' | 'line' | 'stroke' | 'native'
export type Stroke = { points: Point[]; width: number }
/** Semantic source reference, never a stored page screenshot or engine object. */
export type NativeControl = {
  page: 'calendar' | 'clock' | 'device-info'; key: string; role: 'text' | 'calendar' | 'clock' | 'graphic'; source: Frame
  sample?: { timestamp?: number; weekStart?: 0 | 1; temperature?: number; voltage?: number; bluetoothName?: string; deviceName?: string; boardName?: string; batteryPercent?: number }
  settings?: Record<string, number | boolean>; fontId?: string; text?: string
}
/** Scene-only references. Zero-based IDs match existing device image records. */
export type ImageSlotCarousel = { slotIds: number[]; intervalSeconds: number }
export const canvasImageAlgorithms = ['source','auto','floydSteinberg','atkinson','bayer','none'] as const
export type CanvasImageAlgorithm = typeof canvasImageAlgorithms[number]
export type CanvasImageAdjustments = {contrast:number;strength:number;heavyInk:boolean}
export type Widget = Frame & { id: string; name: string; kind?: Kind; locked?: boolean; groupId?: string; cells: Cell[][]; style: Style; image?: string; imageFit?: 'fit' | 'cover'; imageDither?: CanvasImageAlgorithm; imageAdjustments?: CanvasImageAdjustments; imageSlots?: ImageSlotCarousel; stroke?: Stroke; native?: NativeControl; sample?: NativeControl['sample']; hostWeather?: WeatherBinding; countdownTarget?: { name: string; targetTimestamp: number } }
export const pageModes = ['static','dynamic','host-dynamic'] as const
export type PageMode = typeof pageModes[number]
export type Document = { version: 1; width: number; height: number; widgets: Widget[]; guides?: Guide[]; pageMode?: PageMode; colorMode?: 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11; footerInset?: number; weatherSource?: WeatherSource }
export type Point = { x: number; y: number }
export type ResizeCorner = 'nw' | 'ne' | 'sw' | 'se'
export function resizeCorners(w: Frame) {
  return ([['nw', w.x, w.y], ['ne', w.x + w.width, w.y], ['sw', w.x, w.y + w.height], ['se', w.x + w.width, w.y + w.height]] as const)
    .map(([corner, x, y]) => ({ corner, x, y }))
}
function resizeCornerAt(w: Frame, p: Point, radius: number) {
  return resizeCorners(w).filter(h => Math.abs(p.x-h.x) <= radius && Math.abs(p.y-h.y) <= radius)
    .sort((a,b) => Math.hypot(p.x-a.x,p.y-a.y)-Math.hypot(p.x-b.x,p.y-b.y))[0]?.corner
}
export type Selection = { id: string; row?: number; col?: number }
export const clone = <T>(value: T): T => JSON.parse(JSON.stringify(value)) as T
const clamp = (n: number, low: number, high: number) => Math.max(low, Math.min(high, n))
export const contains = (f: Frame, p: Point) => p.x >= f.x && p.y >= f.y && p.x < f.x + f.width && p.y < f.y + f.height
export const kindOf = (w: Widget): Kind => w.kind || (w.cells.length > 1 ? 'table' : 'text')
export const deviceMark = (w: Widget) => Boolean(w.native?.key.startsWith('edition:'))
export const locked = (w: Widget) => Boolean(w.locked) || deviceMark(w)
/** Device-owned values require a runtime, even when inserted as rich-text tokens. */
export const hasDynamicContent = (doc: Document) => doc.widgets.some(w =>
  Boolean(w.hostWeather) || Boolean(w.imageSlots) || (Boolean(w.native) && !deviceMark(w)) || w.cells.some(row => row.some(cell =>
    /\{\{[^{}]+\}\}/.test(cell.richText ? cell.richText.runs.map(run => run.text).join('') : cell.text))))
/** Older saved scenes are classified from their content without rewriting them. */
export const documentPageMode = (doc: Document): PageMode => doc.pageMode ?? (hasDynamicContent(doc) ? 'dynamic' : 'static')
export const canSendDocument = (doc: Document) => documentPageMode(doc) === 'static' && !hasDynamicContent(doc)
export const resizable = (w: Widget) => !locked(w) && kindOf(w) !== 'stroke' && (!w.native || ['calendar','clock'].includes(w.native.role))
const bottomInset = (doc: Document) => doc.footerInset ?? 18
/** Validate stored drafts before constructing scene objects; never follow external image URLs. */
export function parseDocument(json: string): Document {
  const doc = JSON.parse(json) as Document
  const finite = (n: unknown): n is number => typeof n === 'number' && Number.isFinite(n)
  const validSample = (s: NativeControl['sample']) => s === undefined || (s !== null && typeof s === 'object' && !Array.isArray(s)
    && (s.timestamp === undefined || (finite(s.timestamp) && s.timestamp >= 0 && s.timestamp <= 4133980800))
    && (s.weekStart === undefined || s.weekStart === 0 || s.weekStart === 1)
    && Object.entries(s).every(([key,value]) => value === undefined || (key === 'timestamp' ? finite(value) : typeof value === 'string' ? value.length <= 256 : finite(value) && Math.abs(value) <= 100000)))
  const validStyle = (s: Partial<Style> | undefined) => s === undefined || (s !== null && typeof s === 'object'
    && (s.fontSize === undefined || (finite(s.fontSize) && s.fontSize >= 6 && s.fontSize <= 240))
    && (s.color === undefined || /^#[0-9a-f]{6}$/i.test(s.color))
    && (s.background === undefined || s.background === 'transparent' || /^#[0-9a-f]{6}$/i.test(s.background))
    && (s.borderColor === undefined || /^#[0-9a-f]{6}$/i.test(s.borderColor))
    && (s.fontFamily === undefined || (typeof s.fontFamily === 'string' && s.fontFamily.length <= 180 && !/[;{}<>]/.test(s.fontFamily)))
    && (s.fontId === undefined || ['wqy9-lunar','wqy12-lunar','helv-bold-14-num','helv-bold-18-num'].includes(s.fontId))
    && (s.align === undefined || ['left','center','right'].includes(s.align)))
  const validRichText = (r: RichText | undefined) => r === undefined || (r !== null && typeof r === 'object'
    && Array.isArray(r.runs) && r.runs.length <= 256
    && (r.padding === undefined || (finite(r.padding) && r.padding >= 0 && r.padding <= 128))
    && (r.verticalAlign === undefined || ['top','middle','bottom'].includes(r.verticalAlign))
    && (r.preset === undefined || (r.preset && typeof r.preset.id === 'string' && r.preset.id.length <= 64 && typeof r.preset.signature === 'string' && r.preset.signature.length <= 64000))
    && r.runs.every(run => run && typeof run.text === 'string' && run.text.length <= 10000 && validStyle(run.style)
      && (run.fontId === undefined || ['wqy9-lunar','wqy12-lunar','helv-bold-14-num','helv-bold-18-num'].includes(run.fontId))
      && (run.scale === undefined || (finite(run.scale) && run.scale >= 64 && run.scale <= 2048))
      && (run.raster === undefined || ['nearest','legacy','coverage','vertical-coverage','strokes'].includes(run.raster))
      && [run.gap,run.baselineShift].every(n => n === undefined || (finite(n) && Math.abs(n) <= 4096))))
  if (!doc || doc.version !== 1 || !finite(doc.width) || !finite(doc.height)
    || (doc.pageMode !== undefined && !pageModes.includes(doc.pageMode))
    || (doc.weatherSource !== undefined && !validWeatherSource(doc.weatherSource))
    || Object.hasOwn(doc, 'calendarSource')
    || (doc.colorMode !== undefined && ![1,2,3,4,5,6,7,8,9,10,11].includes(doc.colorMode))
    || (doc.footerInset !== undefined && (!finite(doc.footerInset) || doc.footerInset < 0 || doc.footerInset > 32))
    || doc.width < 32 || doc.width > 4096 || doc.height < 48 || doc.height > 4096
    || !Array.isArray(doc.widgets) || doc.widgets.length > 200) throw new Error('Invalid document')
  if (doc.guides !== undefined && (!Array.isArray(doc.guides) || doc.guides.length > 100 ||
    new Set(doc.guides.map(g => g?.id)).size !== doc.guides.length || doc.guides.some(g => !g || typeof g.id !== 'string' || !g.id.length || g.id.length > 100 ||
      !['x','y'].includes(g.axis) || !Number.isInteger(g.position) || g.position < 0 || g.position > (g.axis === 'x' ? doc.width : doc.height - bottomInset(doc))))) throw new Error('Invalid guides')
  const ids = new Set<string>()
  for (const w of doc.widgets) {
    if (!w || typeof w.id !== 'string' || ids.has(w.id) || typeof w.name !== 'string'
      || (w.locked !== undefined && typeof w.locked !== 'boolean')
      || (w.groupId !== undefined && (typeof w.groupId !== 'string' || !w.groupId.length || w.groupId.length > 100 || deviceMark(w)))
      || ![w.x,w.y,w.width,w.height].every(finite) || w.x < 0 || w.y < 0 || w.width <= 0 || w.height <= 0
      || w.x + w.width > doc.width || w.y + w.height > doc.height - bottomInset(doc)
      || !w.style || !finite(w.style.fontSize) || !validStyle(w.style)
      || !validSample(w.sample)
      || (w.imageDither !== undefined && (w.kind !== 'image' || !canvasImageAlgorithms.includes(w.imageDither)))
      || (w.imageAdjustments !== undefined && (w.kind!=='image'||!w.imageAdjustments||!finite(w.imageAdjustments.contrast)||w.imageAdjustments.contrast<.5||w.imageAdjustments.contrast>2||!finite(w.imageAdjustments.strength)||w.imageAdjustments.strength<0||w.imageAdjustments.strength>5||typeof w.imageAdjustments.heavyInk!=='boolean'))
      || (w.hostWeather !== undefined && (!w.hostWeather || w.kind !== 'text' || !weatherFields.includes(w.hostWeather.field) || doc.pageMode !== 'host-dynamic'))
      || Object.hasOwn(w, 'hostCalendar')
      || (w.imageSlots !== undefined && (w.kind !== 'image' || !w.imageSlots || !Array.isArray(w.imageSlots.slotIds) || w.imageSlots.slotIds.length > 255
        || w.imageSlots.slotIds.some(id => !Number.isInteger(id) || id < 0 || id > 254)
        || !Number.isInteger(w.imageSlots.intervalSeconds) || w.imageSlots.intervalSeconds < 30 || w.imageSlots.intervalSeconds > 86400))
      || (w.kind !== undefined && !['text','table','todo','list','image','qr','line','stroke','native'].includes(w.kind))
      || (w.kind === 'stroke' && (!w.stroke || !Number.isInteger(w.stroke.width) || w.stroke.width < 1 || w.stroke.width > 16
        || !Array.isArray(w.stroke.points) || w.stroke.points.length < 1 || w.stroke.points.length > 1024
        || w.stroke.points.some(p => !p || !finite(p.x) || !finite(p.y) || p.x < 0 || p.y < 0 || p.x >= w.width || p.y >= w.height)))
      || !Array.isArray(w.cells) || !w.cells.length || w.cells.length > 30 || !Array.isArray(w.cells[0]) || !w.cells[0].length || w.cells[0].length > 30
      || w.cells.some(row => !Array.isArray(row) || row.length !== w.cells[0].length || row.some(c => !c || typeof c.text !== 'string' || c.text.length > 10000 || !validStyle(c.style) || !validRichText(c.richText)))
      || (w.countdownTarget !== undefined && (!w.countdownTarget || typeof w.countdownTarget.name !== 'string' || w.countdownTarget.name.length > 64 || !finite(w.countdownTarget.targetTimestamp) || w.countdownTarget.targetTimestamp < 0 || w.countdownTarget.targetTimestamp > 0xffffffff))
      || (w.image !== undefined && (typeof w.image !== 'string' || !/^data:image\/(png|jpeg|webp);base64,/.test(w.image)))) throw new Error('Invalid widget')
    ids.add(w.id)
    if (w.kind === 'native' && (!w.native || !['calendar','clock','device-info'].includes(w.native.page)
      || typeof w.native.key !== 'string' || w.native.key.length > 100
      || !['text','calendar','clock','graphic'].includes(w.native.role)
      || !w.native.source || ![w.native.source.x,w.native.source.y,w.native.source.width,w.native.source.height].every(value => finite(value) && Math.abs(value) <= 8192)
      || !validSample(w.native.sample)
      || (w.native.fontId !== undefined && !['wqy9-lunar','wqy12-lunar','helv-bold-14-num','helv-bold-18-num'].includes(w.native.fontId))
      || (w.native.text !== undefined && (typeof w.native.text !== 'string' || w.native.text.length > 10000))
      || (w.native.sample?.timestamp !== undefined && (!finite(w.native.sample.timestamp) || w.native.sample.timestamp < 0 || w.native.sample.timestamp > 4133980800))
      || (w.native.settings && Object.entries(w.native.settings).some(([key, value]) => !/^[a-zA-Z]+$/.test(key) || !(typeof value === 'boolean' || finite(value) && Math.abs(value) <= 4096))))) throw new Error('Invalid native control')
  }
  return doc
}
export function cellFrame(widget: Widget, row: number, col: number): Frame {
  const cols = widget.cells[0].length, rows = widget.cells.length
  const x = Math.round(col * widget.width / cols), y = Math.round(row * widget.height / rows)
  return { x: widget.x + x, y: widget.y + y,
    width: Math.round((col + 1) * widget.width / cols) - x,
    height: Math.round((row + 1) * widget.height / rows) - y }
}
export function minimumSize(widget: Widget) {
  if (kindOf(widget) === 'stroke') return { width: 1, height: 1 }
  if (widget.native) return widget.native.role === 'calendar' ? { width: 98, height: 76 } : { width: 1, height: 1 }
  if (kindOf(widget) === 'line') return { width: 1, height: 1 }
  if (['image','qr'].includes(kindOf(widget))) return { width: 16, height: 16 }
  // Glyphs never scale with a control. Keep at least one glyph + inset per cell;
  // longer content clips in the renderer and stays intact in the document.
  const size = Math.max(widget.style.fontSize, ...widget.cells.flat().map(c => c.style?.fontSize || 0))
  return { width: (size + 4) * widget.cells[0].length, height: (size + 4) * widget.cells.length }
}
export function drawPlan(doc: Document) {
  return doc.widgets.flatMap(widget => widget.cells.flatMap((row, r) => row.map((cell, c) => ({
    ...cellFrame(widget, r, c), widgetId: widget.id, row: r, col: c,
    lastRow: r === widget.cells.length - 1, lastCol: c === row.length - 1, text: cell.text,
    richText: cell.richText ?? ((cell.style?.fontId || widget.style.fontId) ? {runs:[{text:cell.text}]} : undefined), ...widget.style, ...cell.style,
    color: cell.style?.color ?? (kindOf(widget) === 'table' && r === 0 ? '#ffffff' : widget.style.color),
    background: cell.style?.background ?? widget.style.background ?? (kindOf(widget) === 'table' && r === 0 ? '#b32025' : kindOf(widget) === 'text' ? 'transparent' : '#ffffff'),
    kind: kindOf(widget), image: widget.image, imageFit: widget.imageFit || 'fit', imageSlots: widget.imageSlots, checked: cell.checked,
    border: kindOf(widget) === 'table',
    header: kindOf(widget) === 'table' && r === 0,
  }))))
}

/** Contiguous one-pixel runs of a stroke, shared by browser and miniapp renderers. */
export function strokePixelRuns(widget: Widget): { x: number; y: number; width: number }[] {
  const stroke = widget.stroke
  if (!stroke?.points.length) return []
  const rows = new Map<number, Set<number>>()
  const radius = stroke.width / 2
  // Canvas strokes with an even width centered on an integer pixel span
  // [center - width / 2, center + width / 2). Match that raster on commit.
  const offset = stroke.width % 2 ? 0 : -.5
  const stamp = (cx: number, cy: number) => {
    for (let y = Math.max(0, Math.ceil(cy + offset - radius)); y <= Math.min(widget.height - 1, Math.floor(cy + offset + radius)); y++) {
      const row = rows.get(y) || new Set<number>(); rows.set(y, row)
      for (let x = Math.max(0, Math.ceil(cx + offset - radius)); x <= Math.min(widget.width - 1, Math.floor(cx + offset + radius)); x++) {
        if ((x - cx - offset) ** 2 + (y - cy - offset) ** 2 <= radius ** 2) row.add(x)
      }
    }
  }
  stroke.points.forEach((point, index) => {
    const previous = stroke.points[Math.max(0, index - 1)]
    const steps = Math.max(1, Math.ceil(Math.hypot(point.x - previous.x, point.y - previous.y)))
    for (let step = 0; step <= steps; step++) stamp(Math.round(previous.x + (point.x - previous.x) * step / steps),
      Math.round(previous.y + (point.y - previous.y) * step / steps))
  })
  const runs: { x: number; y: number; width: number }[] = []
  for (const [y, xs] of rows) {
    const sorted = [...xs].sort((a, b) => a - b)
    for (let i = 0; i < sorted.length;) {
      const x = sorted[i++]; let end = x
      while (i < sorted.length && sorted[i] === end + 1) end = sorted[i++]
      runs.push({ x, y, width: end - x + 1 })
    }
  }
  return runs
}

/** Zoom around an anchor without moving the underlying document point. */
export function zoomAt(view: Point & { zoom: number }, point: Point, zoom: number) {
  zoom = clamp(zoom, .25, 8)
  return { zoom, x: point.x - (point.x - view.x) * zoom / view.zoom,
    y: point.y - (point.y - view.y) * zoom / view.zoom }
}

export class EditorCore {
  doc: Document
  private currentSelection?: Selection
  private extraSelection: string[] = []
  get selection() { return this.currentSelection }
  set selection(value: Selection | undefined) { this.currentSelection = value; this.extraSelection = [] }
  multiSelect = false
  snapEnabled = true
  showGuides = true
  snapLines: SnapLine[] = []
  guidePreview?: { id: string; position: number }
  marquee?: Frame
  private past: Document[] = []
  private future: Document[] = []
  private clipboard?: Widget[]
  private gesture?: { start: Point; before: Document; widgets: Widget[]; mode: 'move' | 'resize'; corner?: ResizeCorner; moved: boolean }
  private boxGesture?: { start: Point; ids: string[]; moved: boolean }
  constructor(doc: Document) { this.doc = clone(doc) }
  get selected() { return this.doc.widgets.find(w => w.id === this.selection?.id) }
  get selectedWidgets() { const ids = new Set([this.selection?.id, ...this.extraSelection]); return this.doc.widgets.filter(w => ids.has(w.id)) }
  get selectionBounds() { return boundsOf(this.selectedWidgets) }
  get multiple() { return this.selectedWidgets.length > 1 }
  get selectionLocked() { return this.selectedWidgets.some(locked) }
  get units() { return selectionUnits(this.selectedWidgets) }
  get canUndo() { return this.past.length > 0 }
  get canRedo() { return this.future.length > 0 }
  isBlank(p: Point, handleRadius = 4) {
    const selected = this.selected
    // A resize handle extends beyond the widget and must not start viewport panning.
    if (!this.multiple && selected && resizable(selected) && resizeCornerAt(selected, p, handleRadius)) return false
    return !this.doc.widgets.some(w => contains(w, p))
  }
  private nextId(prefix = 'widget') { let n = 1; while (this.doc.widgets.some(w => w.id === `${prefix}-${n}`)) n++; return `${prefix}-${n}` }
  add(kind: Exclude<Kind, 'native' | 'stroke'>, typography?: Pick<Style, 'fontFamily' | 'fontSize'>) {
    const names = { text: '文字', table: '表格', todo: '待办事项', list: '列表', image: '图片框', qr: '二维码', line: '分割线' }
    const fontSize = typography?.fontSize ?? (this.doc.width < 300 ? 9 : 14)
    const cells: Cell[][] = kind === 'table' ? [['项目','内容','备注'],['示例','点击编辑',''],['','','']].map(row => row.map(text => ({ text })))
      : kind === 'todo' || kind === 'list' ? ['整理今日计划','阅读二十分钟','散步，放松一下'].map(text => [{ text }]) : [[{ text: kind === 'image' ? '双击或在右侧添加图片' : '输入文字' }]]
    const width = Math.min(this.doc.width - 16, kind === 'image' ? 140 : 240)
    const height = Math.min(this.doc.height - 34, kind === 'text' ? fontSize * 2 + 8 : kind === 'image' ? 100 : (fontSize + 16) * cells.length)
    const w: Widget = { id: this.nextId(), kind, name: names[kind], x: Math.round((this.doc.width - width) / 2),
      y: Math.round((this.doc.height - 18 - height) / 2), width, height, cells,
      style: { fontSize, color: kind === 'text' ? '#b32025' : '#191919', bold: false, align: kind === 'table' ? 'center' : 'left' } }
    if(kind==='text') {
      if(documentPageMode(this.doc)==='dynamic') w.style.fontId='wqy9-lunar'
      else w.style.fontFamily='sans-serif'
      w.style.fontSize=12;w.cells[0][0].richText={runs:[{text:w.cells[0][0].text}],padding:4}
    }
    if (typography) Object.assign(w.style, typography)
    this.change(doc => doc.widgets.push(w)); this.selection = { id: w.id }
    if (kind === 'qr') { w.cells = [[{ text: 'https://inktag.dev' }]]; w.width = w.height = Math.min(100, this.doc.height - 34, this.doc.width - 16) }
    if (kind === 'line') w.height = 1
  }
  addStroke(points: Point[], color: string, width: number, eraser = false) {
    if (!points.length || !/^#[0-9a-f]{6}$/i.test(color) || !Number.isInteger(width) || width < 1 || width > 16) return
    const limitY = this.doc.height - bottomInset(this.doc) - 1
    const clean = points.slice(0, 1024).map(p => ({ x: clamp(Math.round(p.x), 0, this.doc.width - 1), y: clamp(Math.round(p.y), 0, limitY) }))
    const radius = Math.ceil(width / 2)
    const x = Math.max(0, Math.min(...clean.map(p => p.x)) - radius), y = Math.max(0, Math.min(...clean.map(p => p.y)) - radius)
    const right = Math.min(this.doc.width, Math.max(...clean.map(p => p.x)) + radius + 1)
    const bottom = Math.min(this.doc.height - bottomInset(this.doc), Math.max(...clean.map(p => p.y)) + radius + 1)
    const widget: Widget = { id: this.nextId('stroke'), kind: 'stroke', name: eraser ? '橡皮擦' : '自由画笔',
      x, y, width: right - x, height: bottom - y, cells: [[{ text: '' }]],
      style: { fontSize: 9, color, bold: false }, stroke: { width, points: clean.map(p => ({ x: p.x - x, y: p.y - y })) } }
    this.change(doc => doc.widgets.push(widget)); this.selection = undefined
  }
  setFrame(frame: Partial<Frame>) {
    if (this.multiple) {
      const bounds = this.selectionBounds!
      if (Object.values(frame).some(v => !Number.isFinite(v))) return
      this.nudge(Math.round((frame.x ?? bounds.x) - bounds.x), Math.round((frame.y ?? bounds.y) - bounds.y)); return
    }
    const w = this.selected
    if (!w || locked(w) || Object.values(frame).some(v => !Number.isFinite(v))) return
    if (!resizable(w)) frame = { x: frame.x ?? w.x, y: frame.y ?? w.y }
    this.change(() => {
      const min = minimumSize(w)
      w.width = clamp(Math.round(frame.width ?? w.width), Math.min(min.width, w.width), this.doc.width)
      w.height = clamp(Math.round(frame.height ?? w.height), Math.min(min.height, w.height), this.doc.height - bottomInset(this.doc))
      w.x = clamp(Math.round(frame.x ?? w.x), 0, this.doc.width - w.width)
      w.y = clamp(Math.round(frame.y ?? w.y), 0, this.doc.height - bottomInset(this.doc) - w.height)
    })
  }
  setImage(image: string, id = this.selection?.id) {
    const w = this.doc.widgets.find(w => w.id === id)
    if (w && !locked(w) && kindOf(w) === 'image' && /^data:image\/(png|jpeg|webp);base64,/.test(image)) this.change(() => { w.image = image; w.imageDither ??= 'auto' })
  }
  fitImage(fit: 'fit' | 'cover') { const w = this.selected; if (w && !locked(w) && !this.multiple && kindOf(w) === 'image') this.change(() => { w.imageFit = fit }) }
  fillCanvasWithImage() {
    const w = this.selected
    if (!w || locked(w) || this.multiple || kindOf(w) !== 'image' || w.imageSlots) return
    // The device-owned mark is a separate overlay, not an uneditable white strip.
    this.change(doc => { doc.footerInset = 0; Object.assign(w, {x:0,y:0,width:doc.width,height:doc.height,imageFit:'cover'}) })
  }
  reorder(direction: -1 | 1) {
    if (this.selectionLocked) return
    const ids = new Set(this.selectedWidgets.map(w => w.id))
    this.change(doc => {
      const indices = doc.widgets.map((_w, i) => i); if (direction === 1) indices.reverse()
      for (const index of indices) {
        const next = index + direction, other = doc.widgets[next]
        if (ids.has(doc.widgets[index].id) && other && !ids.has(other.id) && !locked(other))
          [doc.widgets[index], doc.widgets[next]] = [doc.widgets[next], doc.widgets[index]]
      }
    })
  }
  structure(axis: 'row' | 'col', remove = false): string | undefined {
    const w = this.selected
    if (!w || locked(w) || this.multiple || !['table', 'list', 'todo'].includes(kindOf(w)) || (axis === 'col' && kindOf(w) !== 'table')) return
    const rows = w.cells.length, cols = w.cells[0].length
    const count = axis === 'row' ? rows : cols
    if (remove && count <= 1) return '至少保留一行、一列。'
    if (!remove && count >= 30) return '本轮最多支持 30 行或列。'
    const index = Math.min(count - 1, (axis === 'row' ? this.selection?.row : this.selection?.col) ?? count - 1)
    const candidate = clone(w)
    if (axis === 'row') candidate.cells.splice(remove ? index : index + 1, remove ? 1 : 0, ...remove ? [] : [Array.from({ length: cols }, () => ({ text: '' }))])
    else candidate.cells.forEach(row => row.splice(remove ? index : index + 1, remove ? 1 : 0, ...remove ? [] : [{ text: '' }]))
    const min = minimumSize(candidate)
    if (!remove && (min.height > this.doc.height - 18 - w.y || min.width > this.doc.width - w.x)) return '空间不足：请先移动控件或减小字号，再增加行列。'
    this.change(() => { w.cells = candidate.cells; if (!remove) { w.width = Math.max(w.width, min.width); w.height = Math.max(w.height, min.height) } })
    this.selection = { id: w.id }
  }
  private record(before: Document) {
    if (JSON.stringify(before) === JSON.stringify(this.doc)) return
    this.past.push(before); if (this.past.length > 80) this.past.shift()
    this.future = []
  }
  change(action: (doc: Document) => void) { const before = clone(this.doc); action(this.doc); this.record(before) }
  private restoreHistory(doc: Document) {
    const ids = this.selectedWidgets.map(w => w.id), s = this.selection
    this.doc = doc
    const w = doc.widgets.find(w => w.id === s?.id)
    if (ids.length > 1) this.applySelection(ids)
    else this.selection = !w || !s ? undefined : s.row !== undefined && s.col !== undefined && (!w.cells[s.row]?.[s.col] || locked(w)) ? { id: w.id } : s
  }
  undo() { this.cancel(); const doc = this.past.pop(); if (doc) { this.future.push(this.doc); this.restoreHistory(doc) } }
  redo() { this.cancel(); const doc = this.future.pop(); if (doc) { this.past.push(this.doc); this.restoreHistory(doc) } }
  deselect() { this.cancel(); this.selection = undefined }
  private expandIds(ids: string[]) {
    const selected = new Set(ids), groups = new Set(this.doc.widgets.filter(w => selected.has(w.id)).map(w => w.groupId).filter(Boolean))
    return this.doc.widgets.filter(w => selected.has(w.id) || Boolean(w.groupId && groups.has(w.groupId))).map(w => w.id)
  }
  private applySelection(ids: string[]) {
    const expanded = this.expandIds(ids)
    this.selection = expanded.length ? { id: expanded[expanded.length - 1] } : undefined
    this.extraSelection = expanded.slice(0, -1)
  }
  selectMany(ids: string[]) { this.cancel(); this.applySelection(ids) }
  select(id: string, additive = false) {
    this.cancel()
    const members = this.expandIds([id]), ids = this.selectedWidgets.map(w => w.id)
    this.applySelection(!additive ? members : members.every(key => ids.includes(key)) ? ids.filter(key => !members.includes(key)) : [...ids, ...members])
  }
  selectAll() { this.selectMany(this.doc.widgets.filter(w => !locked(w)).map(w => w.id)) }
  down(p: Point, handleRadius = 4, additive = false) {
    this.cancel()
    const selected = this.selected
    const corner = selected && resizable(selected) ? resizeCornerAt(selected, p, handleRadius) : undefined
    if (!additive && !this.multiSelect && !this.multiple && selected && corner) {
      this.gesture = { start: p, before: clone(this.doc), widgets: [clone(selected)], mode: 'resize', corner, moved: false }; return
    }
    const hit = [...this.doc.widgets].sort((a,b) => Number(deviceMark(a)) - Number(deviceMark(b))).reverse().find(w => contains(w, p))
    if (!hit) {
      if (!additive && !this.multiSelect && !this.selectionLocked && this.multiple && this.units.length === 1 && contains(this.selectionBounds!, p)) {
        this.gesture = { start: p, before: clone(this.doc), widgets: clone(this.selectedWidgets), mode: 'move', moved: false }; return
      }
      const ids = additive || this.multiSelect ? this.selectedWidgets.map(w => w.id) : []
      this.applySelection(ids)
      if (contains({ x: 0, y: 0, width: this.doc.width, height: this.doc.height - bottomInset(this.doc) }, p))
        this.boxGesture = { start: p, ids, moved: false }
      return
    }
    if (additive || this.multiSelect) { this.select(hit.id, true); return }
    // The ENTIRE first gesture is selection-only, even if the pointer drifts.
    if (!this.selectedWidgets.some(w => w.id === hit.id)) { this.select(hit.id); return }
    if (this.selectionLocked) return
    this.gesture = { start: p, before: clone(this.doc), widgets: clone(this.selectedWidgets), mode: 'move', moved: false }
  }
  move(p: Point, threshold = 2, snapDistance = 0, bypassSnap = false) {
    const box = this.boxGesture
    if (box) {
      if (!box.moved && Math.hypot(p.x - box.start.x, p.y - box.start.y) < threshold) return
      box.moved = true
      const end = { x: clamp(p.x, 0, this.doc.width), y: clamp(p.y, 0, this.doc.height - bottomInset(this.doc)) }
      this.marquee = { x: Math.min(box.start.x, end.x), y: Math.min(box.start.y, end.y), width: Math.abs(end.x - box.start.x), height: Math.abs(end.y - box.start.y) }
      const rect = this.marquee
      this.applySelection([...box.ids, ...this.doc.widgets.filter(w => !locked(w) && w.x >= rect.x && w.y >= rect.y && w.x + w.width <= rect.x + rect.width && w.y + w.height <= rect.y + rect.height).map(w => w.id)])
      return true
    }
    const g = this.gesture, w = this.selected
    if (!g || !w) return
    const dx = Math.round(p.x - g.start.x), dy = Math.round(p.y - g.start.y)
    if (!g.moved && Math.hypot(dx, dy) < threshold) return
    g.moved = true
    this.currentSelection = { id: this.selection!.id }
    const bounds = boundsOf(g.widgets)!, height = this.doc.height - bottomInset(this.doc)
    const ids = new Set(g.widgets.map(item => item.id))
    const targets = [{ x: 0, y: 0, width: this.doc.width, height }, ...this.doc.widgets.filter(item => !ids.has(item.id))]
    const snap = (frame: Frame) => this.snapEnabled && !bypassSnap && snapDistance > 0
      ? snapFrame(frame, targets, this.showGuides ? this.doc.guides ?? [] : [], snapDistance, g.mode === 'resize', g.corner) : { x: 0, y: 0, lines: [] }
    if (g.mode === 'move') {
      let x = clamp(bounds.x + dx, 0, this.doc.width - bounds.width), y = clamp(bounds.y + dy, 0, height - bounds.height)
      const result = snap({ ...bounds, x, y })
      x = clamp(x + result.x, 0, this.doc.width - bounds.width); y = clamp(y + result.y, 0, height - bounds.height)
      this.snapLines = result.lines
      for (const before of g.widgets) { const item = this.doc.widgets.find(item => item.id === before.id)!; item.x = before.x + x - bounds.x; item.y = before.y + y - bounds.y }
    } else {
      const min = minimumSize(w)
      const before = g.widgets[0]
      const west = g.corner?.endsWith('w'), north = g.corner?.startsWith('n')
      const minWidth = Math.min(min.width,before.width), minHeight = Math.min(min.height,before.height)
      let left = west ? clamp(before.x+dx,0,before.x+before.width-minWidth) : before.x
      let right = west ? before.x+before.width : clamp(before.x+before.width+dx,before.x+minWidth,this.doc.width)
      let top = north ? clamp(before.y+dy,0,before.y+before.height-minHeight) : before.y
      let bottom = north ? before.y+before.height : clamp(before.y+before.height+dy,before.y+minHeight,height)
      Object.assign(w,{x:left,y:top,width:right-left,height:bottom-top})
      const result = snap(w)
      if(west) left=clamp(left+result.x,0,right-minWidth); else right=clamp(right+result.x,left+minWidth,this.doc.width)
      if(north) top=clamp(top+result.y,0,bottom-minHeight); else bottom=clamp(bottom+result.y,top+minHeight,height)
      Object.assign(w,{x:left,y:top,width:right-left,height:bottom-top})
      this.snapLines = result.lines
    }
    return true
  }
  up(p: Point) {
    const g = this.gesture, w = this.selected
    this.gesture = undefined; this.boxGesture = undefined; this.marquee = undefined; this.snapLines = []
    if (!g || !w) return
    if (g.moved) this.record(g.before)
    else if (!this.multiple && g.mode === 'move' && contains(w, p) && !['image','native','line','qr','stroke'].includes(kindOf(w))) {
      this.selection = { id: w.id, row: Math.min(w.cells.length - 1, Math.floor((p.y - w.y) * w.cells.length / w.height)),
        col: Math.min(w.cells[0].length - 1, Math.floor((p.x - w.x) * w.cells[0].length / w.width)) }
      if (kindOf(w) === 'todo' && p.x - w.x < w.style.fontSize + 12) {
        this.toggleChecked(); this.selection = { id: w.id }
      }
    }
  }
  cancel() {
    if (this.gesture) this.doc = this.gesture.before
    if (this.boxGesture) this.applySelection(this.boxGesture.ids)
    this.gesture = undefined; this.boxGesture = undefined; this.marquee = undefined; this.snapLines = []; this.guidePreview = undefined
  }
  nudge(dx: number, dy: number) {
    const widgets = this.selectedWidgets, bounds = boundsOf(widgets)
    if (!bounds || this.selectionLocked || !Number.isFinite(dx) || !Number.isFinite(dy)) return
    dx = clamp(Math.round(dx), -bounds.x, this.doc.width - bounds.x - bounds.width)
    dy = clamp(Math.round(dy), -bounds.y, this.doc.height - bottomInset(this.doc) - bounds.y - bounds.height)
    this.change(() => widgets.forEach(w => { w.x += dx; w.y += dy }))
  }
  align(alignment: Alignment, to: 'selection' | 'canvas' = this.units.length > 1 ? 'selection' : 'canvas') {
    if (this.selectionLocked || !this.selected) return
    const units = this.units, target = to === 'canvas' ? { x: 0, y: 0, width: this.doc.width, height: this.doc.height - bottomInset(this.doc) } : boundsOf(units)!
    this.change(() => units.forEach(unit => { const delta = alignmentOffset(unit, target, alignment); unit.members.forEach(w => { w.x += delta.x; w.y += delta.y }) }))
  }
  distribute(axis: 'x' | 'y'): string | undefined {
    if (this.selectionLocked) return
    const units = this.units.sort((a, b) => a[axis] - b[axis]), size = axis === 'x' ? 'width' : 'height'
    if (units.length < 3) return '至少选择三个独立控件或组合。'
    const first = units[0][axis], last = units.at(-1)![axis] + units.at(-1)![size]
    const gap = (last - first - units.reduce((sum, u) => sum + u[size], 0)) / (units.length - 1)
    if (gap < 0) return '可用空间不足，请先拉开两端控件。'
    this.change(() => {
      let position = first
      units.forEach(unit => { const delta = Math.round(position) - unit[axis]; unit.members.forEach(w => { w[axis] += delta }); position += unit[size] + gap })
    })
  }
  setLocked(value: boolean, ids = this.selectedWidgets.map(w => w.id)) {
    this.cancel()
    const selected = new Set(this.expandIds(ids))
    this.change(doc => doc.widgets.filter(w => selected.has(w.id) && !deviceMark(w)).forEach(w => { if (value) w.locked = true; else delete w.locked }))
    if (this.selection) this.currentSelection = { id: this.selection.id }
  }
  group() {
    const widgets = this.selectedWidgets
    if (widgets.length < 2 || this.selectionLocked || this.units.length < 2) return
    let i = 1; while (this.doc.widgets.some(w => w.groupId === `group-${i}`)) i++
    this.change(() => widgets.forEach(w => { w.groupId = `group-${i}` }))
  }
  ungroup() {
    if (this.selectionLocked) return
    const widgets = this.selectedWidgets
    this.change(() => widgets.forEach(w => { delete w.groupId }))
  }
  addGuide(axis: Guide['axis']) {
    if ((this.doc.guides?.length ?? 0) >= 100) return '最多保留 100 条参考线。'
    let i = 1; while (this.doc.guides?.some(g => g.id === `guide-${i}`)) i++
    this.change(doc => (doc.guides ??= []).push({ id: `guide-${i}`, axis, position: Math.round((axis === 'x' ? doc.width : doc.height - bottomInset(doc)) / 2) }))
    this.showGuides = true
  }
  setGuide(id: string, position: number) {
    const guide = this.doc.guides?.find(g => g.id === id)
    if (!guide || !Number.isFinite(position)) return
    this.change(() => { guide.position = clamp(Math.round(position), 0, guide.axis === 'x' ? this.doc.width : this.doc.height - bottomInset(this.doc)) })
  }
  removeGuide(id: string) { this.change(doc => { doc.guides = doc.guides?.filter(g => g.id !== id) }) }
  updateCell(text: string) {
    if (this.selection) this.updateCellAt(this.selection, text)
  }
  updateCellAt(s: Selection, text: string) {
    const w = this.doc.widgets.find(item => item.id === s.id)
    if (w && !locked(w) && !this.multiple && s.row !== undefined && s.col !== undefined && w.cells[s.row]?.[s.col]) this.change(() => {
      const cell = w.cells[s.row!][s.col!]; cell.text = text
      // Plain-cell editing is an explicit replacement, never leave stale hidden rich content.
      if (cell.richText) cell.richText.runs = [{ text }]
    })
  }
  toggleChecked() { const w = this.selected, s = this.selection; if (w && !locked(w) && !this.multiple && kindOf(w) === 'todo' && s?.row !== undefined) this.change(() => { const cell = w.cells[s.row!][0]; cell.checked = !cell.checked }) }
  resetCellStyle() { const w = this.selected, s = this.selection; if (w && !locked(w) && !this.multiple && s?.row !== undefined && s.col !== undefined) this.change(() => { delete w.cells[s.row!][s.col!].style }) }
  updateStyle(style: Partial<Style>) {
    const w = this.selected, s = this.selection
    if (!w || this.selectionLocked) return
    this.change(() => {
      if (s?.row !== undefined && s.col !== undefined) {
        const c = w.cells[s.row][s.col]; c.style = { ...c.style, ...style }
      } else this.selectedWidgets.forEach(widget => { widget.style = { ...widget.style, ...style } })
    })
  }
  copy() { this.clipboard = this.selected && !this.selectionLocked ? clone(this.selectedWidgets) : undefined }
  paste() {
    if (!this.clipboard?.length || this.doc.widgets.length + this.clipboard.length > 200) return
    const widgets = clone(this.clipboard), bounds = boundsOf(widgets)!, groups = new Map<string, string>()
    const dx = Math.min(4, this.doc.width - bounds.x - bounds.width), dy = Math.min(4, this.doc.height - bottomInset(this.doc) - bounds.y - bounds.height)
    this.change(doc => widgets.forEach(w => {
      w.id = this.nextId('copy'); w.name += ' 副本'; w.x += dx; w.y += dy
      if (w.groupId) {
        if (!groups.has(w.groupId)) { let i = 1; while (doc.widgets.some(item => item.groupId === `group-${i}`) || [...groups.values()].includes(`group-${i}`)) i++; groups.set(w.groupId, `group-${i}`) }
        w.groupId = groups.get(w.groupId)
      }
      doc.widgets.push(w)
    }))
    this.applySelection(widgets.map(w => w.id))
  }
  remove() { if (this.selectionLocked) return; const ids = new Set(this.selectedWidgets.map(w => w.id)); this.change(doc => { doc.widgets = doc.widgets.filter(w => !ids.has(w.id)) }); this.selection = undefined }
}
