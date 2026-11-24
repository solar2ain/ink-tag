import { useEffect, useRef, useState } from 'react'
import type { CanvasImageAdjustments, CanvasImageAlgorithm, EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { ImageAssetPicker } from '../../components/ImageAssetPicker'
import { readCanvasImage } from './imageRaster'

export function CanvasImageControls({core,act,notice,request}:{core:EditorCore;act:(fn:()=>unknown)=>void;notice:(message:string)=>void;request?:{id:string;nonce:number}}) {
  const input=useRef<HTMLInputElement>(null), epoch=useRef(0), [picker,setPicker]=useState(false),[busy,setBusy]=useState(false),[message,setMessage]=useState('')
  const w=core.selected!
  const adjustments=w.imageAdjustments??{contrast:1,strength:1,heavyInk:false},processing=Boolean(w.imageDither&&w.imageDither!=='source')
  const adjust=(value:Partial<CanvasImageAdjustments>)=>act(()=>core.change(()=>{w.imageAdjustments={...adjustments,...value}}))
  useEffect(()=>{epoch.current++;setBusy(false);setMessage('');setPicker(false);return()=>{epoch.current++}},[w.id])
  useEffect(()=>{if(request?.id===w.id)input.current?.click()},[request,w.id])
  const upload=async(file?:File)=>{
    if(!file)return
    const id=w.id, current=epoch.current
    setBusy(true);setMessage('正在读取图片…')
    try {
      const image=await readCanvasImage(file)
      if(current!==epoch.current)return
      act(()=>core.setImage(image,id));setMessage('图片已添加；预览按当前屏幕色板处理。')
    } catch(error) {if(current===epoch.current){const text=error instanceof Error?error.message:'图片读取失败，请重试。';setMessage(text);notice(text)}}
    finally {if(current===epoch.current)setBusy(false)}
  }
  return <div className="canvas-image-controls">
    <input hidden data-canvas-image ref={input} type="file" accept="image/png,image/jpeg,image/webp,.png,.jpg,.jpeg,.webp" onChange={event=>{const file=event.currentTarget.files?.[0];event.currentTarget.value='';void upload(file)}} />
    <div className="image-source-actions"><button disabled={busy} className="image-upload" onClick={()=>input.current?.click()}>{busy?'正在读取…':w.image?'替换本地图片':'添加本地图片'}</button><button disabled={busy} className="image-upload" onClick={()=>setPicker(true)}>从素材库选择</button></div>
    {message&&<p role="status" className="image-import-status">{message}</p>}
    <button className="image-upload" onClick={()=>act(()=>core.fillCanvasWithImage())}>铺满画板</button>
    <label>图片填充<select aria-label="图片填充" value={w.imageFit||'fit'} onChange={e=>act(()=>core.fitImage(e.target.value as 'fit'|'cover'))}><option value="fit">完整显示（等比）</option><option value="cover">铺满裁切（等比）</option></select></label>
    <label>抖动算法<select aria-label="图片抖动算法" value={w.imageDither||'source'} onChange={e=>act(()=>core.change(()=>{w.imageDither=e.target.value as CanvasImageAlgorithm}))}>
      <option value="source">原图（不处理）</option><option value="auto">Hybrid 自适应（默认）</option><option value="floydSteinberg">Floyd-Steinberg</option><option value="atkinson">Atkinson</option><option value="bayer">Bayer 有序抖动</option><option value="none">仅色板量化</option>
    </select></label>
    <label>对比度 <output>{adjustments.contrast.toFixed(1)}</output><input type="range" aria-label="图片对比度" min="0.5" max="2" step="0.1" value={adjustments.contrast} disabled={!processing} onChange={e=>adjust({contrast:Number(e.target.value)})}/></label>
    <label>抖动强度 <output>{adjustments.strength.toFixed(1)}</output><input type="range" aria-label="图片抖动强度" min="0" max="5" step="0.1" value={adjustments.strength} disabled={!processing||w.imageDither==='none'} onChange={e=>adjust({strength:Number(e.target.value)})}/></label>
    <label className="lab-check"><input type="checkbox" aria-label="图片重色模式" checked={adjustments.heavyInk} disabled={!processing} onChange={e=>adjust({heavyInk:e.target.checked})}/>重色模式</label>
    <p>按图片框的实际像素与画板色彩处理，调整尺寸后自动重算。保留原图，可随时更换算法；预览与导出使用同一结果。</p>
    <p>PNG / JPEG / WebP，最多 20 MiB。大图自动缩小；图片只保存在本机。铺满画板不会移除设备标识。</p>
    {picker&&<ImageAssetPicker close={()=>setPicker(false)} choose={asset=>void upload(new File([asset.blob],asset.name,{type:asset.type}))}/>}
  </div>
}
