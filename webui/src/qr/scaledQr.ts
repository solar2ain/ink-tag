import { qrModule, type QrMatrix } from './qr'

/** Pixel-aligned nearest-neighbour module edges, with four quiet modules.
 * Use every available pixel instead of quantising the whole symbol to 1x/2x.
 * Each module stays solid black/white; no interpolation or blurred edges. */
export function scaledQrRuns(matrix:QrMatrix,width:number,height:number){
  const size=Math.floor(Math.min(width,height)),total=matrix.size+8
  if(size<total)throw new RangeError('二维码区域太小')
  const left=Math.floor((width-size)/2),top=Math.floor((height-size)/2)
  const edge=(module:number)=>Math.round(module*size/total)
  const runs:Array<{x:number;y:number;width:number;height:number}>=[]
  for(let r=0;r<matrix.size;r++)for(let c=0;c<matrix.size;){
    if(!qrModule(matrix,r,c)){c++;continue}
    const start=c++
    while(c<matrix.size&&qrModule(matrix,r,c))c++
    runs.push({x:left+edge(start+4),y:top+edge(r+4),width:edge(c+4)-edge(start+4),height:edge(r+5)-edge(r+4)})
  }
  return {left,top,size,runs}
}
