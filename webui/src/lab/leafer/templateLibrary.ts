import { type Document, type Widget, type Kind } from '../../../../packages/inktag-editor-core/src/index'
import { pageTemplates, templateKindLabels, templateKindOrder, templatesForResolution } from '../../data/templates'
import recipes from '../../data/template-library/layouts.json'
import leaferOverrides from './template-overrides.json'
import { nativeDocument } from './nativeBridge'
import { weatherDocument } from './weatherTemplate'
import { defaultCreationTypography } from '../../data/creationFonts'

export { templateKindLabels, templateKindOrder }
export const labTemplates = pageTemplates
type Node = { type: string; x: number; y: number; w?: number; h?: number; text?: string; size?: number; name?: string; value?: string; color?: string; bold?: boolean; checklist?: boolean; items?: string[]; rows?: string[][]; accent?: string; columnColors?: string[]; itemColors?: string[]; checkedItems?: number[]; headerBackground?: string; headerColor?: string; borderColor?: string; cellStyles?: Record<string, { color?: string; background?: string; bold?: boolean }> }
const layouts = recipes.layouts as Record<string, Record<string, { elements: Node[]; compact?: { core: number; fontSize: number; rows?: string[][]; items?: string[] } }>>
type CompactOverride = { core: number; fontSize: number; rows?: string[][]; items?: string[]; cellStyles?: Node['cellStyles'] }
type LayoutOverride = { extraElements?: Node[]; nodeOverrides?: Record<string, Partial<Node>>; omit?: number[]; compact?: CompactOverride; squareCompact?: CompactOverride }
const overrides = leaferOverrides as Record<string, { textReplacements?: Record<string, string>; landscape?: LayoutOverride; portrait?: LayoutOverride; square?: LayoutOverride }>
export function templateDocument(id: string, requestedWidth?: number, requestedHeight?: number): Document {
  const template = labTemplates.find(t => t.id === id)
    || (requestedWidth && requestedHeight ? templatesForResolution(requestedWidth, requestedHeight).find(t => t.id === id) : undefined)
  if (!template) throw new Error('未知模板')
  const { width, height } = template
  if (template.layoutData?.startsWith('weather-')) return weatherDocument(width,height,template.layoutData.slice(8))
  if (template.nativeAction && ['calendar','clock'].includes(template.nativeAction)) return nativeDocument(template.nativeAction as 'calendar' | 'clock', width, height)
  const orientation = height > width ? 'portrait' : 'landscape'
  const layout = layouts[template.layoutData!]?.[orientation]
  if (!layout) throw new Error('模板布局不可用')
  const theme = overrides[template.layoutData!]
  const nearSquare = Math.max(width,height) / Math.min(width,height) <= 1.25
  const changes = nearSquare ? theme?.square || theme?.[orientation] : theme?.[orientation]
  const elements = [...layout.elements.map((node, index) => ({ ...node,
    ...changes?.nodeOverrides?.[index],
    text: theme?.textReplacements?.[node.text || ''] ?? changes?.nodeOverrides?.[index]?.text ?? node.text,
  })).filter((_, index) => !changes?.omit?.includes(index)), ...(changes?.extraElements || [])]
  const compactPreset: CompactOverride | undefined = (nearSquare && changes?.squareCompact) || changes?.compact || layout.compact
  const compact = compactPreset && Math.max(width,height) <= recipes.compact.maxLongEdge && Math.min(width,height) <= recipes.compact.maxShortEdge ? compactPreset : undefined
  const nodes = compact ? [{ ...elements[compact.core], x: recipes.compact.inset / width, y: recipes.compact.inset / height,
    w: (width - recipes.compact.inset * 2) / width, h: (height - recipes.compact.inset - recipes.compact.footer) / height,
    size: compact.fontSize, rows: compact.rows ?? elements[compact.core].rows,
    items: compact.items ?? elements[compact.core].items,
    cellStyles: compact.cellStyles ?? elements[compact.core].cellStyles }] : elements
  const widgets = nodes.map((node, i): Widget => {
    const kind: Kind = node.type === 'list' ? node.checklist ? 'todo' : 'list' : node.type === 'image-slot' ? 'image' : node.type as Kind
    const x = Math.round(node.x * width), y = Math.min(height - 19, Math.round(node.y * height))
    const fontSize = Math.max(12, Math.round((node.size ?? .05) * Math.min(width,height)))
    const w = Math.min(width - x, Math.round((node.w ?? (1 - node.x - .04)) * width))
    const h = Math.max(1, Math.min(height - 18 - y, kind === 'line' ? 1 : Math.round(node.h ? node.h * height : fontSize * 1.5)))
    const rows = node.rows || node.items?.map(text => [text]) || [[node.text || node.value || '']]
    const columns = Math.max(...rows.map(row => row.length))
    return { id: `recipe-${i}`, kind, name: node.name || (kind === 'text' && node.text ? node.text.slice(0,18) : { text: '文字', table: '课表', todo: '待办事项', list: '列表', qr: '二维码', image: '图片框', line: '分割线', stroke: '自由画笔', native: '控件' }[kind]),
      x, y, width: Math.max(1,w), height: h, cells: rows.map((row,r) => Array.from({length:columns}, (_,c) => ({ text: row[c] || '', checked: kind === 'todo' && node.checkedItems?.includes(r),
        style: { ...(kind === 'table' && !r ? { color: node.headerColor || '#ffffff', background: node.headerBackground || node.accent || '#b32025', bold: true }
          : node.itemColors?.[r] || node.columnColors?.[c] ? { color: node.itemColors?.[r] || node.columnColors?.[c] } : {}), ...node.cellStyles?.[`${r}:${c}`] } }))),
      style: { fontSize, color: node.color || '#191919', bold: Boolean(node.bold), align: kind === 'table' ? 'center' : 'left', borderColor: node.borderColor } }
  })
  fitRecipeControls(widgets,width,height,template.layoutData!,Boolean(compact))
  const { fontFamily, fontSize } = defaultCreationTypography(width, height)
  for (const widget of widgets) if (['text','list','todo','table'].includes(widget.kind ?? '')) {
    widget.style.fontFamily = fontFamily
    widget.style.fontSize = Math.max(1, Math.floor(widget.style.fontSize / fontSize)) * fontSize
  }
  return { version: 1, width, height, pageMode: 'static', colorMode: 2, footerInset: 0, widgets }
}

/** Recipes express proportions; minimum readable type needs pixel-sized gaps.
 * Only new presets are fitted here, never a user's saved document. */
function fitRecipeControls(widgets: Widget[],width:number,height:number,recipe:string,compact:boolean) {
  const gap=2,bottom=height-18
  if(recipe==='photo'){
    const [image,title,caption]=widgets
    title.height=Math.ceil(title.style.fontSize*1.3);caption.height=Math.ceil(caption.style.fontSize*1.3)
    caption.y=bottom-caption.height;title.y=caption.y-gap-title.height
    image.height=Math.min(image.height,title.y-gap-image.y)
  }
  if(recipe==='business'&&width>height&&height<=200){
    const text=widgets.filter(w=>w.kind==='text'),qr=widgets.find(w=>w.kind==='qr')!
    const inset=Math.max(4,Math.round(width*.04)),size=Math.min(bottom-inset*2,Math.round(width*.4))
    qr.x=width-inset-size;qr.y=inset;qr.width=qr.height=size
    const textWidth=qr.x-gap-inset,inlineNames=height<=128,row=Math.floor((bottom-inset*2)/(text.length-Number(inlineNames)))
    text.forEach((w,i)=>{w.x=inset;w.y=inset+Math.max(0,i-Number(inlineNames))*row;w.width=textWidth;w.height=Math.max(15,row-2);w.style.fontSize=i===0?Math.max(12,Math.min(18,Math.floor(w.height/1.2))):12
      if(i===2)w.cells[0][0].text='产品设计'
    })
    if(inlineNames){text[0].width=Math.floor(textWidth*.55);text[1].x=inset+text[0].width+gap;text[1].width=textWidth-text[0].width-gap}
    widgets.splice(0,widgets.length,...text,qr)
    return
  }
  if(recipe==='business'&&height>width&&height<=300){
    const inset=4,row=18,text=widgets.filter(w=>w.kind==='text'),qr=widgets.find(w=>w.kind==='qr')!,rule=widgets.find(w=>w.kind==='line')!
    text.forEach(w=>{w.x=inset;w.width=width-inset*2;w.height=row;w.style.fontSize=12})
    text.slice(0,3).forEach((w,i)=>w.y=inset+i*(row+gap))
    text[2].cells[0][0].text='产品设计'
    if(width<120)text[4].cells[0][0].text='hi@inktag.dev'
    rule.y=text[2].y+row+gap;rule.height=1
    qr.y=rule.y+gap+1;qr.height=qr.width=Math.min(width-inset*2,bottom-qr.y-2*(row+gap));qr.x=Math.floor((width-qr.width)/2)
    text[3].y=qr.y+qr.height+gap;text[4].y=text[3].y+row+gap
  }
  if(recipe==='wifi'&&width>height&&height<=200){
    const text=widgets.filter(w=>w.kind==='text'),qr=widgets.find(w=>w.kind==='qr')!,inset=4,size=bottom-inset*2
    qr.x=width-inset-size;qr.y=inset;qr.width=qr.height=size
    text.forEach((w,i)=>{w.x=inset;w.y=inset+i*Math.floor((size-18)/2);w.width=qr.x-inset-gap;w.height=18;w.style.fontSize=12})
    widgets.splice(0,widgets.length,...text,qr)
    return
  }
  if(recipe==='wifi'&&width<120&&height>width){
    widgets.find(w=>w.kind==='text'&&w.cells[0][0].text==='INKTAG-STUDIO')!.cells[0][0].text='INKTAG'
    widgets.find(w=>w.kind==='qr')!.cells[0][0].text='WIFI:T:WPA;S:INKTAG;P:inktag1234;;'
  }
  // Separate header text from its rule, then move the body as one block.
  if(!compact&&['todo','focus','schedule','wifi','diptych','grid'].includes(recipe)){
    const rule=widgets.find(w=>w.kind==='line')
    if(rule){
      const heading=widgets.filter(w=>w.kind==='text'&&w.y<rule.y)
      const old=rule.y
      rule.y=Math.max(rule.y,...heading.map(w=>w.y+w.height+gap))
      const shift=rule.y-old
      for(const w of widgets.filter(w=>w!==rule&&!heading.includes(w)&&w.y>=old)){
        w.y+=shift;w.height=Math.min(w.height,bottom-w.y)
      }
      // Headers in the same band must have independent horizontal bounds.
      heading.sort((a,b)=>a.x-b.x).forEach((w,i)=>{const next=heading[i+1];if(next&&next.x>w.x&&next.y<w.y+w.height)w.width=Math.min(w.width,next.x-w.x-gap)})
    }
  }
  if(recipe==='focus'){
    const list=widgets.find(w=>w.kind==='todo')!,note=widgets.find(w=>w.kind==='text'&&w.cells[0][0].text==='随手记')!
    if(width===height&&width<=240){
      // A narrow two-column checklist clips even 12px Chinese. Stack the
      // editable list and notes instead, retaining all three complete items.
      const heading=widgets[0],rule=widgets.find(w=>w.kind==='line')!
      heading.x=6;heading.y=4;heading.width=width-12;heading.height=18;heading.style.fontSize=12
      rule.x=6;rule.y=24;rule.width=width-12
      Object.assign(list,{x:6,y:28,width:width-12,height:Math.floor((bottom-28)*.55)})
      list.style.fontSize=12
      Object.assign(note,{x:6,y:list.y+list.height+gap,width:width-12,height:18})
      note.style.fontSize=12
      const lines=widgets.filter(w=>w.kind==='line'&&w!==rule)
      lines.forEach((w,i)=>Object.assign(w,{x:6,y:note.y+note.height+gap+i*16,width:width-12,height:1}))
      for(const w of lines.filter(w=>w.y>=bottom))widgets.splice(widgets.indexOf(w),1)
      return
    }
    if(width>height&&width<=300)list.cells.forEach((row,i)=>{row[0].text=['完成要事','阅读片刻','散步放松'][i]})
    if(note){const lines=widgets.filter(w=>w.kind==='line'&&w.y>=note.y&&w.x>=note.x)
      lines.forEach((w,i)=>{w.y=Math.max(w.y,note.y+note.height+gap+i*Math.max(16,Math.round(height*.15)));w.height=1})
      for(const w of lines.filter(w=>w.y>=bottom))widgets.splice(widgets.indexOf(w),1)
    }
  }
}
