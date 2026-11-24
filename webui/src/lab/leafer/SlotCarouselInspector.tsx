import { useEffect, useState } from 'react'
import type { EditorCore, ImageSlotCarousel } from '../../../../packages/inktag-editor-core/src/index'
import { parseSlotNumbers } from './slotCarousel'

export function SlotCarouselInspector({core,act}:{core:EditorCore;act:(fn:()=>unknown)=>void}) {
  const w=core.selected!,slots=w.imageSlots!
  const sequence=slots.slotIds.map(id=>id+1).join(', ')
  const [numbers,setNumbers]=useState(sequence),[seconds,setSeconds]=useState(String(slots.intervalSeconds))
  useEffect(()=>setNumbers(sequence),[sequence])
  useEffect(()=>setSeconds(String(slots.intervalSeconds)),[slots.intervalSeconds])
  const validNumbers=parseSlotNumbers(numbers)!==undefined
  const validSeconds=seconds!==''&&Number.isInteger(Number(seconds))&&Number(seconds)>=30&&Number(seconds)<=86400
  const update=(change:Partial<ImageSlotCarousel>)=>act(()=>core.change(()=>{w.imageSlots={...w.imageSlots!,...change}}))
  return <section className="inspector-section slot-carousel-settings">
    <h3>图槽轮播</h3>
    <p>引用设备已有图槽，按所填顺序轮播；编号从 1 开始，不复制图槽中的图片。</p>
    <label>图槽编号（按顺序）<input aria-label="轮播图槽编号" aria-invalid={!validNumbers} value={numbers} placeholder="例如：1, 3, 2" onChange={event=>{
      const text=event.target.value;setNumbers(text)
      const ids=parseSlotNumbers(text)
      if(ids && ids.join(',')!==slots.slotIds.join(','))update({slotIds:ids})
    }}/></label>
    {!validNumbers&&<p role="alert">图槽编号须为 1–255，用逗号或空格分隔；本次修改尚未应用。</p>}
    <label>轮播间隔（秒）<input aria-label="轮播间隔（秒）" aria-invalid={!validSeconds} type="number" min={30} max={86400} step={1} value={seconds} onChange={event=>{
      const text=event.target.value;setSeconds(text);const value=Number(text)
      if(text!==''&&Number.isInteger(value)&&value>=30&&value<=86400&&value!==slots.intervalSeconds)update({intervalSeconds:value})
    }}/></label>
    {!validSeconds&&<p role="alert">间隔须为 30–86400 秒的整数；本次修改尚未应用。</p>}
    <p className="field-hint">当前仅保存轮播区域和参数，暂不支持在设备或主机服务中运行。画板为占位预览，不代表设备中已有对应图片。</p>
  </section>
}
