import { useEffect, useRef, useState } from 'react'
import { textVariables } from '../../device-gui/textVariables'

export function VariablePicker({insert}:{insert:(text:string)=>void}) {
  const [open,setOpen]=useState(false),[query,setQuery]=useState(''),host=useRef<HTMLDivElement>(null),button=useRef<HTMLButtonElement>(null)
  useEffect(()=>{
    if(!open)return
    const outside=(e:PointerEvent)=>{if(!host.current?.contains(e.target as Node))setOpen(false)}
    document.addEventListener('pointerdown',outside);return()=>document.removeEventListener('pointerdown',outside)
  },[open])
  const matches=textVariables.filter(v=>`${v.label} ${v.id}`.toLowerCase().includes(query.toLowerCase()))
  return <div className="variable-picker" ref={host} onKeyDown={e=>{e.stopPropagation();if(e.key==='Escape'){setOpen(false);button.current?.focus()}}}>
    <div className="text-content-heading"><span>内容</span><button ref={button} aria-label="插入变量" aria-expanded={open} onMouseDown={e=>e.preventDefault()} onClick={()=>{setQuery('');setOpen(!open)}}>＋ 插入变量</button></div>
    {open&&<div className="variable-picker-panel" role="dialog" aria-label="选择变量">
      <input autoFocus aria-label="搜索变量" placeholder="搜索名称或变量名" value={query} onChange={e=>setQuery(e.target.value)}/>
      <div className="variable-picker-list">{matches.map(v=><button key={v.id} onMouseDown={e=>e.preventDefault()} onClick={()=>{insert(`{{${v.id}}}`);setOpen(false)}}><span>{v.label}</span><code>{`{{${v.id}}}`}</code></button>)}
        {!matches.length&&<p>没有匹配的变量</p>}</div>
    </div>}
  </div>
}
