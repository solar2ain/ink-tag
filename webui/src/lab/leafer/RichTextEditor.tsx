import { createContext, useContext, useLayoutEffect, useRef, useState, type ReactNode } from 'react'
import { Bold, AlignLeft, AlignCenter, AlignRight, Undo2, Redo2 } from 'lucide-react'
import { documentPageMode, type EditorCore, type RichText, type TextRun } from '../../../../packages/inktag-editor-core/src/index'
import { editText, formatText, templateText, tokenRange, type TextRange } from '../../../../packages/inktag-editor-core/src/textEditing'
import { deviceGuiFontOptions } from '../../device-gui/model'
import { VariablePicker } from './VariablePicker'
import { applyTextPreset, selectedTextPreset } from './textPresetState'
import { nativeFontSize } from './richTextRenderer'
import { CreationFontOptions, CreationFontSize, creationFontValue, selectedFontSize } from './CreationFontFields'
import { inksFor } from './palette'
import presets from './textPresets.json'

type SelectedText = TextRange & {key:string}
const Context=createContext<{range:SelectedText|null;setRange:(range:SelectedText)=>void;active:{current:HTMLElement|null}}|null>(null)
export function RichTextSelectionProvider({children}:{children:ReactNode}) {
  const [range,setRange]=useState<SelectedText|null>(null),active=useRef<HTMLElement|null>(null)
  return <Context.Provider value={{range,setRange,active}}>{children}</Context.Provider>
}
function domRange(root:HTMLElement):TextRange|undefined {
  const selection=window.getSelection()
  if(!selection?.rangeCount)return
  const range=selection.getRangeAt(0)
  if(!root.contains(range.startContainer)||!root.contains(range.endContainer))return
  const prefix=range.cloneRange();prefix.selectNodeContents(root);prefix.setEnd(range.startContainer,range.startOffset)
  return {start:prefix.toString().length,end:prefix.toString().length+range.toString().length}
}
function restoreRange(root:HTMLElement,selected:TextRange) {
  const walker=document.createTreeWalker(root,NodeFilter.SHOW_TEXT),nodes:Text[]=[]
  let node;while((node=walker.nextNode()))nodes.push(node as Text)
  if(!nodes.length){root.append(document.createTextNode(''));nodes.push(root.firstChild as Text)}
  const point=(position:number):[Node,number]=>{
    let offset=0
    for(const text of nodes){if(position<=offset+text.length)return [text,Math.max(0,position-offset)];offset+=text.length}
    const last=nodes[nodes.length-1];return [last,last.length]
  }
  const range=document.createRange();range.setStart(...point(selected.start));range.setEnd(...point(selected.end))
  const selection=window.getSelection();selection?.removeAllRanges();selection?.addRange(range)
}

export function RichTextEditor({core,act,canvas=false}:{core:EditorCore;act:(fn:()=>unknown)=>void;canvas?:boolean}) {
  const session=useContext(Context)!,root=useRef<HTMLDivElement>(null),composing=useRef(false)
  const mode=documentPageMode(core.doc), dynamic=mode==='dynamic'
  const w=core.selected!,s=core.selection!,row=s.row??0,col=s.col??0,cell=w.cells[row][col]
  const content:RichText=cell.richText??{runs:[{text:cell.text}],padding:0}
  const text=templateText(content.runs),key=`${w.id}:${row}:${col}`
  const range=session.range?.key===key?session.range:{start:0,end:0}
  const base={...w.style,...cell.style}
  let at=0
  const current=content.runs.find(run=>{at+=run.text.length;return at>range.start})||content.runs[0]
  const style={...base,...current?.style}
  const capture=()=>{const value=root.current&&domRange(root.current);if(value){session.active.current=root.current;session.setRange({...value,key})}}
  const write=(next:RichText)=>act(()=>core.change(()=>{cell.richText=next;cell.text=templateText(next.runs)}))
  const sync=()=>{
    if(composing.current||!root.current)return
    capture()
    const next=(root.current.textContent||'').slice(0,10000)
    if(next!==text)write({...content,runs:editText(content.runs,next)})
  }
  useLayoutEffect(()=>{
    if(composing.current||!root.current)return
    const host=root.current,active=session.active.current===host&&document.activeElement===host
    const selected=active?domRange(host):undefined
    host.replaceChildren(...content.runs.map(run=>{
      const span=document.createElement('span'),local={...base,...run.style}
      span.textContent=run.text;span.style.color=local.color
      span.style.fontFamily=run.fontId||local.fontId?'sans-serif':local.fontFamily||'sans-serif'
      span.style.fontSize=`${Math.max(12,local.fontSize)}px`;span.style.fontWeight=local.bold?'700':'400'
      if(run.gap)span.style.marginLeft=`${Math.max(0,run.gap)}px`
      if(run.text.includes('{{'))span.className='editable-variable'
      if(local.color==='#ffffff')span.style.background='#59636a'
      return span
    }))
    if(active)restoreRange(host,selected||range)
  },[JSON.stringify(content),JSON.stringify(base)])
  const change=(fn:(run:TextRun)=>TextRun,focusEditor=true)=>{
    const selected=tokenRange(text,range.start===range.end?{start:0,end:text.length}:range)
    const host=session.active.current?.isConnected?session.active.current:root.current
    // Keep the selection live when a toolbar takes focus (especially native selects).
    if(focusEditor&&host?.isConnected){host.focus();restoreRange(host,selected)}
    session.setRange({...selected,key})
    write({...content,runs:formatText(content.runs,range,fn)})
  }
  const insert=(value:string)=>{
    const host=session.active.current?.isConnected?session.active.current:root.current!
    const selected=domRange(host)||range
    host.focus();restoreRange(host,selected)
    const selection=window.getSelection(),r=selection?.getRangeAt(0)
    if(!r)return
    r.deleteContents();const node=document.createTextNode(value);r.insertNode(node);r.setStartAfter(node);r.collapse(true)
    selection!.removeAllRanges();selection!.addRange(r)
    // The active editor can be the canvas instance; use its DOM, not the sidebar DOM.
    const next=(host.textContent||'').slice(0,10000)
    session.active.current=host;session.setRange({...domRange(host)!,key})
    write({...content,runs:editText(content.runs,next)})
  }
  const history=(redo:boolean)=>act(()=>redo?core.redo():core.undo())
  const font=current?.fontId||current?.style?.fontFamily||base.fontId||base.fontFamily||'sans-serif'
  return <div className={`simple-rich-editor ${canvas?'on-canvas-editor':''}`}>
    {!canvas&&dynamic&&<div className="rich-preset-row"><select aria-label="文字预置" value={selectedTextPreset(content,base)} onChange={e=>{
      if(e.target.value){session.setRange({key,start:0,end:0});act(()=>applyTextPreset(core,e.target.value))}
    }}><option value="" disabled>自定义</option>{presets.map(p=><option key={p.id} value={p.id}>{p.name}</option>)}</select></div>}
    <div className="rich-format-row" role="toolbar" aria-label="文字格式">
      <select aria-label="选中文字字体" value={creationFontValue(font, mode)} onChange={e=>{
        const device=deviceGuiFontOptions.find(f=>f.id===e.target.value),value=e.target.value
        change(run=>({...run,fontId:device?.id,scale:device?256:undefined,style:{...run.style,fontFamily:device?undefined:value,fontId:undefined,fontSize:device?nativeFontSize(device.id):selectedFontSize(value, style.fontSize)}}))
      }}><CreationFontOptions mode={mode}/></select>
      <CreationFontSize label="选中文字字号" family={font} mode={mode} value={style.fontSize} onChange={size=>change(run=>{const id=run.fontId||(run.style?.fontFamily?undefined:base.fontId);return {...run,scale:id?Math.round(size/nativeFontSize(id)*256):undefined,style:{...run.style,fontSize:size}}},false)}/>
      <button aria-label="加粗文字" title="加粗（Ctrl / ⌘ B）" aria-pressed={Boolean(style.bold)} onMouseDown={e=>e.preventDefault()} onClick={()=>change(run=>({...run,style:{...run.style,bold:!style.bold}}))}><Bold size={15}/></button>
    </div>
    <div className="rich-color-row"><div role="group" aria-label="文字选区颜色">{inksFor(core.doc.colorMode).map(ink=><button key={ink.color} title={ink.name} aria-label={`文字颜色 · ${ink.name}`} aria-pressed={style.color===ink.color} onMouseDown={e=>e.preventDefault()} onClick={()=>change(run=>({...run,style:{...run.style,color:ink.color}}))}><i style={{background:ink.color}}/></button>)}</div>
      <span className="rich-scope">{range.start!==range.end?'已选文字':'全部文字'}</span>
      {canvas&&<><button title="撤销" aria-label="撤销文字编辑" disabled={!core.canUndo} onMouseDown={e=>e.preventDefault()} onClick={()=>history(false)}><Undo2 size={13}/></button>
      <button title="重做" aria-label="重做文字编辑" disabled={!core.canRedo} onMouseDown={e=>e.preventDefault()} onClick={()=>history(true)}><Redo2 size={13}/></button></>}
    </div>
    {dynamic ? <VariablePicker insert={insert}/> : <div className="text-content-heading"><span>内容</span></div>}
    <div ref={root} contentEditable suppressContentEditableWarning role="textbox" aria-label={canvas?'画布文字编辑':'富文本内容'} aria-multiline="true" spellCheck={false} className="rich-editable"
      onFocus={capture} onSelect={capture} onMouseUp={capture} onKeyUp={capture} onInput={sync}
      onCompositionStart={()=>{composing.current=true}} onCompositionEnd={()=>{composing.current=false;sync()}}
      onPaste={e=>{e.preventDefault();capture();insert(e.clipboardData.getData('text/plain'))}}
      onDragOver={e=>e.preventDefault()} onDrop={e=>{e.preventDefault();capture();const text=e.dataTransfer.getData('text/plain');if(text)insert(text)}}
      onBeforeInput={e=>{const event=e.nativeEvent as InputEvent;if(['insertParagraph','insertLineBreak'].includes(event.inputType)){e.preventDefault();capture();insert('\n')}else if(event.inputType?.startsWith('format'))e.preventDefault()}}
      onKeyDown={e=>{
        if(e.nativeEvent.isComposing)return
        if(canvas&&e.key==='Escape'){e.preventDefault();act(()=>core.select(w.id));return}
        const mod=e.ctrlKey||e.metaKey,k=e.key.toLowerCase()
        if(mod&&['z','y','b','i','u'].includes(k)){e.preventDefault();capture();if(k==='z'||k==='y')history(k==='y'||e.shiftKey);else if(k==='b')change(run=>({...run,style:{...run.style,bold:!style.bold}}))}
        if(e.key==='Enter'){e.preventDefault();capture();insert('\n')}
      }}/>
    {!canvas&&<div className="rich-align-row"><small>段落对齐</small><div>{([['left',AlignLeft,'左对齐'],['center',AlignCenter,'居中'],['right',AlignRight,'右对齐']] as const).map(([align,Icon,label])=><button key={align} aria-label={label} title={label} aria-pressed={(base.align||'left')===align} onClick={()=>act(()=>core.updateStyle({align}))}><Icon size={13}/></button>)}</div></div>}
  </div>
}
