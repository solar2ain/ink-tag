import { useEffect, useRef, useState } from 'react'

/** Keep partially typed numbers editable while valid values update the canvas immediately. */
export function LiveNumberInput({value,onChange,min=6,max=96,label}:{value:number;onChange:(n:number)=>void;min?:number;max?:number;label:string}) {
  const input=useRef<HTMLInputElement>(null),[draft,setDraft]=useState(String(value))
  useEffect(()=>{if(document.activeElement!==input.current)setDraft(String(value))},[value])
  return <input ref={input} aria-label={label} title="字号（px）" type="number" min={min} max={max} step={1} value={draft}
    onChange={e=>{const text=e.target.value;setDraft(text);const n=Number(text);if(text!==''&&Number.isFinite(n)&&n>=min&&n<=max&&n!==value)onChange(n)}}
    onBlur={()=>setDraft(String(value))} onKeyDown={e=>{if(e.key==='Enter')e.currentTarget.blur()}}/>
}
