import type * as Leafer from 'leafer-editor'
import { cellFrame, drawPlan, kindOf, locked, deviceMark, resizable, resizeCorners, strokePixelRuns, documentPageMode, type EditorCore, type Widget, type Document, type PageMode } from '../../../../packages/inktag-editor-core/src/index'
import { inkColor } from './palette'
import { previewPaper } from './deviceEffect'
import { createQrMatrix } from '../../qr/qr'
import { scaledQrRuns } from '../../qr/scaledQr'
import { demoWeather, weatherPrimitives, type WeatherData } from '../../../../packages/inktag-editor-core/src/weather'
import { creationFontAssets, creationPixelFont, loadCreationFont } from '../../data/creationFonts'
import { ensureBundledFont } from '../../data/bundledFonts'

/** The same scene + gesture adapter runs with leafer-editor or @leafer-editor/miniapp. */
export function mountScene(engine: Pick<typeof Leafer, 'Group' | 'Box' | 'Rect' | 'Text' | 'PointerEvent'>,
  canvas: Leafer.Leafer, core: EditorCore, notify: () => void, zoom = 1,
  raster?: (widget: Widget, doc: Document) => { width: number; height: number; pixels: Uint8Array } | undefined,
  resolveText: (widget: Widget, text: string) => string = (_widget,text) => text,
  richRaster?: (widget: Widget, content: NonNullable<Widget['cells'][0][0]['richText']>, style: Widget['style'], width: number,height: number,mode?:PageMode) => {width:number;height:number;pixels:Uint8Array;alpha?:Uint8Array},
  brand?: (width:number,height:number) => {x:number;y:number;width:number;height:number;pixels:Uint8Array} | undefined,
  manualInput = false, images?: {image:(w:Widget,doc:Document)=>string|undefined;ready:(doc:Document)=>Promise<void>;dispose:()=>void}, weather: () => WeatherData = demoWeather) {
  const { Group, Box, Rect, Text, PointerEvent } = engine
  const root = new Group({ scaleX: zoom, scaleY: zoom })
  canvas.add(root)
  const render = (preview = true, includeDeviceMark = preview) => {
    root.clear()
    const { width, height } = core.doc
    const pageMode = documentPageMode(core.doc)
    // Same perceived white as the image editor. Presentation only: export
    // calls render(false), preserving native white pixels for dithering/send.
    const paper=previewPaper
    const ink = (color: string) => {const value=inkColor(color, core.doc.colorMode);return preview&&value.toLowerCase()==='#ffffff'?paper:value}
    const pixelColors = ['#ffffff','#000000','#dc3026','#ebc219','#2c61a5','#137463','#e18b23']
    const pixels = (frame: {width:number;height:number;pixels:Uint8Array;alpha?:Uint8Array}, group: Leafer.Group) => {
      for(let y=0;y<frame.height;y++) for(let x=0;x<frame.width;) {
        const color=frame.pixels[y*frame.width+x],visible=frame.alpha?frame.alpha[y*frame.width+x]:Boolean(color),start=x++
        while(x<frame.width&&frame.pixels[y*frame.width+x]===color&&(!frame.alpha||frame.alpha[y*frame.width+x]===visible))x++
        if(visible)group.add(new Rect({x:start,y,width:x-start,height:1,fill:ink(pixelColors[color]||'#000000')}))
      }
    }
    root.add(new Rect({ width, height, fill: preview?paper:'#ffffff' }))
    const native = core.doc.widgets.filter(w => w.native).sort((a,b) => Number(deviceMark(a)) - Number(deviceMark(b)))
    const layers = new Map<string, Leafer.Group>()
    for (const item of drawPlan(core.doc)) {
      if (core.doc.widgets.some(w=>w.id===item.widgetId&&w.hostWeather)) continue
      if (item.kind === 'native' || item.kind === 'stroke') continue
      if (!layers.has(item.widgetId)) layers.set(item.widgetId,new Group())
      const layer = layers.get(item.widgetId)!
      if (item.kind === 'line') { layer.add(new Rect({ x: item.x, y: item.y, width: item.width, height: item.height, fill: ink(item.color) })); continue }
      const box = new Box({ x: item.x, y: item.y, width: item.width, height: item.height,
        overflow: 'hide', fill: ink(item.background) })
      if (item.border) {
        const edge = ink(item.borderColor || '#191919')
        box.add(new Rect({ x: 0, y: 0, width: item.width, height: 1, fill: edge }))
        box.add(new Rect({ x: 0, y: 0, width: 1, height: item.height, fill: edge }))
        if (item.lastCol) box.add(new Rect({ x: item.width - 1, y: 0, width: 1, height: item.height, fill: edge }))
        if (item.lastRow) box.add(new Rect({ x: 0, y: item.height - 1, width: item.width, height: 1, fill: edge }))
      }
      if (item.kind === 'qr') {
        try {
          const matrix = createQrMatrix(item.text || 'https://inktag.dev')
          const qr=scaledQrRuns(matrix,item.width,item.height)
          box.add(new Rect({x:qr.left,y:qr.top,width:qr.size,height:qr.size,fill:ink('#ffffff')}))
          for(const run of qr.runs)box.add(new Rect({...run,fill:'#000000'}))
        } catch(error) { box.add(new Text({ text: error instanceof RangeError?error.message:'二维码内容过长', fontSize: 9, fill: '#b32025' })) }
        layer.add(box); continue
      }
      if (item.kind === 'image') {
        const image = images?.image(core.doc.widgets.find(w=>w.id===item.widgetId)!, core.doc) ?? item.image
        box.add(new Rect({ width: item.width, height: item.height, stroke: image ? undefined : '#191919', strokeWidth: 1,
          fill: image ? { type: 'image', url: image, mode: item.imageFit === 'cover' ? 'cover' : 'fit' } : '#ffffff' }))
        if (!image) box.add(new Text({ width: item.width, height: item.height,
          text: item.imageSlots ? `图槽轮播\n${item.imageSlots.slotIds.length ? item.imageSlots.slotIds.slice(0,5).map(id=>id+1).join(' → ')+(item.imageSlots.slotIds.length>5?' …':'') : '请选择图槽'}\n${item.imageSlots.intervalSeconds} 秒 · 预览占位` : '＋ 添加图片',
          fontSize: Math.max(6, Math.min(12, Math.floor(Math.min(item.width / 7, item.height / 3)))),
          fill: '#c42d30', textAlign: 'center', verticalAlign: 'middle' }))
        layer.add(box); continue
      }
      const inset = item.kind === 'todo' || item.kind === 'list' ? item.fontSize + 12 : 4
      if (item.kind === 'todo') {
        const size = Math.min(item.fontSize, Math.max(4, item.height - 4)), y = (item.height - size) / 2
        box.add(new Rect({ x: 4, y, width: size, height: size,
          stroke: ink('#b32025'), strokeWidth: 1, fill: item.checked ? ink('#b32025') : ink(item.background) }))
        // Geometric tick: no font baseline/bearing can shift it out of its box.
        if (item.checked) for (const [x1,y1,x2,y2] of [[.2,.5,.42,.72],[.42,.72,.82,.28]]) {
          box.add(new Rect({ x: 4+x1*size, y:y+y1*size, width:Math.hypot(x2-x1,y2-y1)*size,
            height:Math.max(1,size*.1), rotation:Math.atan2(y2-y1,x2-x1)*180/Math.PI, fill:ink('#ffffff') }))
        }
      } else if (item.kind === 'list') box.add(new Text({ x: 4, width: item.fontSize, height: item.height,
        text: '•', fontSize: item.fontSize, fill: ink(item.color), verticalAlign: 'middle' }))
      const simplePixelText = !item.richText && pageMode === 'static' && creationPixelFont(item.fontFamily)
      const content = item.richText ?? (simplePixelText ? { runs: [{ text: item.text }] } : undefined)
      if(content && richRaster) {
        const offset = item.kind==='text' && !simplePixelText ? 0 : inset
        const group=new Group({x:offset})
        const frame = richRaster(core.doc.widgets.find(w=>w.id===item.widgetId)!,content,item,Math.max(1,item.width-offset-(simplePixelText?4:0)),item.height,pageMode)
        pixels(frame,group)
        if (item.kind === 'todo' && item.checked) {
          let left=frame.width,right=-1,top=frame.height,bottom=-1
          frame.pixels.forEach((p,i)=>{if(frame.alpha ? frame.alpha[i] : p){const x=i%frame.width,y=Math.floor(i/frame.width);left=Math.min(left,x);right=Math.max(right,x);top=Math.min(top,y);bottom=Math.max(bottom,y)}})
          if(right>=left)group.add(new Rect({x:left,y:Math.round((top+bottom)/2),width:right-left+1,height:Math.max(1,Math.round(item.fontSize/14)),fill:ink(item.color)}))
        }
        box.add(group)
      } else box.add(new Text({ x: inset, width: Math.max(0, item.width - inset - 4), height: item.height,
        text: resolveText(core.doc.widgets.find(w => w.id === item.widgetId)!,item.text), fontFamily: item.fontFamily || 'sans-serif', fontSize: item.fontSize,
        fontWeight: item.bold ? 'bold' : 'normal', fill: ink(item.color),
        textDecoration: item.kind === 'todo' && item.checked ? 'delete' : 'none',
        textAlign: item.align || 'center', verticalAlign: 'middle', textWrap: item.kind === 'text' ? 'normal' : 'none', textOverflow: 'hide',
        opacity: 1 }))
      layer.add(box)
    }
    for (const widget of core.doc.widgets) if (widget.hostWeather) {
      const layer=new Box({x:widget.x,y:widget.y,width:widget.width,height:widget.height,overflow:'hide',fill:ink(widget.style.background??'transparent')})
      for(const p of weatherPrimitives(widget,weather())) {
        if(p.kind==='text') layer.add(new Text({x:p.x,y:p.y,width:p.width,height:p.height,text:p.text,fontSize:p.size,fontFamily:widget.style.fontFamily||'sans-serif',fontWeight:p.bold?'bold':'normal',fill:ink(p.color),textAlign:p.align||'left',verticalAlign:'middle',textOverflow:'hide'}))
        else if(p.kind==='rect') layer.add(new Rect({x:p.x,y:p.y,width:p.width,height:p.height,fill:ink(p.color)}))
        else layer.add(new Rect({x:p.x,y:p.y,width:Math.hypot(p.x2-p.x,p.y2-p.y),height:p.thickness,rotation:Math.atan2(p.y2-p.y,p.x2-p.x)*180/Math.PI,fill:ink(p.color)}))
      }
      layers.set(widget.id,layer)
    }
    for (const widget of core.doc.widgets) if (kindOf(widget) === 'stroke') {
      const layer = new Group({ x: widget.x, y: widget.y })
      const fill = ink(widget.style.color)
      for (const run of strokePixelRuns(widget)) layer.add(new Rect({ ...run, height: 1, fill }))
      layers.set(widget.id, layer)
    }
    for (const w of native) {
      if (deviceMark(w) && (!includeDeviceMark || brand)) continue
      const frame = raster?.(w, core.doc)
      if (!frame) continue
      const group = new Group({ x: w.x, y: w.y })
      pixels(frame,group)
      layers.set(w.id,group)
    }
    for (const w of [...core.doc.widgets].sort((a,b) => Number(deviceMark(a)) - Number(deviceMark(b)))) { const layer=layers.get(w.id); if (layer) root.add(layer) }
    // Device mark is an immutable preview overlay, never stored in the editable document.
    if (includeDeviceMark && brand) { const frame=brand(width,height); if(frame){const group=new Group({x:frame.x,y:frame.y}); pixels(frame,group); root.add(group)} }
    else if (includeDeviceMark && !native.some(deviceMark)) root.add(new Text({ x: width - 90, y: height - 16, width: 86, height: 14, text: 'InkTag base',
      fontSize: 10, fontWeight: 'bold', textAlign: 'right', verticalAlign: 'middle', fill: '#191919' }))
    const w = core.selected, s = core.selection
    if(preview)root.add(new Rect({width,height,stroke:'#191919',strokeWidth:2,fill:undefined,hittable:false}))
    if (preview && w) {
      const border = (frame: { x: number; y: number; width: number; height: number }) => {
        const edge = Math.max(1 / zoom, 1)
        root.add(new Rect({ x: frame.x, y: frame.y, width: frame.width, height: edge, fill: '#2874e0' }))
        root.add(new Rect({ x: frame.x, y: frame.y + frame.height - edge, width: frame.width, height: edge, fill: '#2874e0' }))
        root.add(new Rect({ x: frame.x, y: frame.y, width: edge, height: frame.height, fill: '#2874e0' }))
        root.add(new Rect({ x: frame.x + frame.width - edge, y: frame.y, width: edge, height: frame.height, fill: '#2874e0' }))
      }
      for (const selected of core.selectedWidgets) border(selected)
      if (core.multiple) border(core.selectionBounds!)
      if (!core.multiple && !locked(w) && s?.row !== undefined && s.col !== undefined) {
        const cell = cellFrame(w, s.row, s.col)
        root.add(new Rect({ ...cell, fill: '#2874e022' }))
        border(cell)
      }
      if (!core.multiple && resizable(w)) for (const h of resizeCorners(w)) root.add(new Rect({ x: h.x - 4 / zoom, y: h.y - 4 / zoom,
        width: 8 / zoom, height: 8 / zoom, fill: '#ffffff', stroke: '#2874e0', strokeWidth: 1 / zoom, cursor: h.corner==='nw'||h.corner==='se'?'nwse-resize':'nesw-resize' }))
    }
    if (preview) {
      const guideLine = (axis: 'x' | 'y', position: number, color: string, dashed: boolean) => {
        const end = axis === 'x' ? height : width, step = dashed ? 8 / zoom : end
        for (let start = 0; start < end; start += step) root.add(new Rect({ x: axis === 'x' ? position : start, y: axis === 'y' ? position : start,
          width: axis === 'x' ? 1 / zoom : Math.min(dashed ? 4 / zoom : end, end - start),
          height: axis === 'y' ? 1 / zoom : Math.min(dashed ? 4 / zoom : end, end - start), fill: color, hittable: false }))
      }
      if (core.showGuides) for (const guide of core.doc.guides ?? []) guideLine(guide.axis, core.guidePreview?.id === guide.id ? core.guidePreview.position : guide.position, '#139cba', true)
      for (const line of core.snapLines) guideLine(line.axis, line.position, '#d743a1', false)
      if (core.marquee) root.add(new Rect({ ...core.marquee, fill: '#2874e01a', stroke: '#2874e0', strokeWidth: 1 / zoom, hittable: false }))
    }
  }
  const changed = () => { render(); notify() }
  const down = (e: Leafer.PointerEvent) => { core.down(e.getInnerPoint(root), 6 / zoom, e.shiftKey || e.ctrlKey || e.metaKey); changed() }
  const move = (e: Leafer.PointerEvent) => { if (core.move(e.getInnerPoint(root), 3 / zoom, 5 / zoom, e.altKey)) changed() }
  const up = (e: Leafer.PointerEvent) => { if ('isCancel' in e && e.isCancel) core.cancel(); else core.up(e.getInnerPoint(root)); changed() }
  if (!manualInput) {
    canvas.on(PointerEvent.DOWN, down)
    canvas.on(PointerEvent.MOVE, move)
    canvas.on(PointerEvent.UP, up)
  }
  render()
  return { render, exportPng: async (includeDeviceMark = false) => {
    const documentBefore = JSON.stringify(core.doc)
    const fonts = documentPageMode(core.doc) === 'static' ? creationFontAssets(core.doc).map(loadCreationFont) : []
    await Promise.all([...fonts, ensureBundledFont(documentBefore), images?.ready(core.doc)])
    if (documentBefore !== JSON.stringify(core.doc)) throw new Error('准备导出期间画板已修改，请待预览完成后重试。')
    const view = { x: root.x, y: root.y, scaleX: root.scaleX, scaleY: root.scaleY }
    root.set({ x: 0, y: 0, scaleX: 1, scaleY: 1 }); render(false, includeDeviceMark)
    try {
      const { width, height } = core.doc
      const result = await root.export('png', { blob: true, clip: { x: 0, y: 0, width, height }, pixelRatio: 1, smooth: false })
      if (!(result.data instanceof Blob) || result.error) throw new Error('画板导出失败')
      return result.data
    } finally { root.set(view); render() }
  }, viewport: (view: { x: number; y: number; zoom: number }) => {
    zoom = view.zoom; root.set({ x: view.x, y: view.y, scaleX: zoom, scaleY: zoom }); render()
  }, destroy: () => {
    images?.dispose(); core.cancel(); canvas.off(PointerEvent.DOWN, down); canvas.off(PointerEvent.MOVE, move); canvas.off(PointerEvent.UP, up); root.destroy()
  } }
}
