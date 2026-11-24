import { transformRgba, type RgbaFrame } from '../../../packages/inktag-client-core/src/screen'
import { automaticImageRotation } from './orientation'

/** Align orientation first, then center crop/pad without scaling.
 * Shared by preview and send; never changes the editable source or device profile. */
export function adaptFrameToSize(source: RgbaFrame, size: {width:number;height:number}): RgbaFrame {
  const {width,height}=size
  if (source.width===width && source.height===height) return source
  if (![width,height,source.width,source.height].every(v=>Number.isInteger(v)&&v>0) || source.data.length!==source.width*source.height*4) throw new Error('画面尺寸无效')
  if (automaticImageRotation(source.width,source.height,width,height)) source=transformRgba(source,1)
  if (source.width===width && source.height===height) return source
  const data=new Uint8ClampedArray(width*height*4).fill(255)
  const drawWidth=source.width,drawHeight=source.height
  const left=Math.floor((width-drawWidth)/2),top=Math.floor((height-drawHeight)/2)
  for(let y=Math.max(0,top);y<Math.min(height,top+drawHeight);y++) for(let x=Math.max(0,left);x<Math.min(width,left+drawWidth);x++) {
    const sx=Math.min(source.width-1,Math.floor((x-left+.5)*source.width/drawWidth))
    const sy=Math.min(source.height-1,Math.floor((y-top+.5)*source.height/drawHeight))
    const from=(sy*source.width+sx)*4,to=(y*width+x)*4
    data.set(source.data.subarray(from,from+4),to)
  }
  return {width,height,data}
}

export function adaptCanvasToSize(source: HTMLCanvasElement, size: {width:number;height:number}) {
  if(source.width===size.width&&source.height===size.height)return source
  const input=source.getContext('2d')
  if(!input)throw new Error('无法读取画面')
  const frame=adaptFrameToSize(input.getImageData(0,0,source.width,source.height),size)
  const canvas=document.createElement('canvas');canvas.width=frame.width;canvas.height=frame.height
  const output=canvas.getContext('2d');if(!output)throw new Error('无法调整画面尺寸')
  output.putImageData(new ImageData(new Uint8ClampedArray(frame.data),frame.width,frame.height),0,0)
  return canvas
}
