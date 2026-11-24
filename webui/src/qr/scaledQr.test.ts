import {describe,it,expect} from 'vitest'
import {createQrMatrix,qrModule} from './qr'
import {scaledQrRuns} from './scaledQr'

describe('canvas QR continuous pixel scaling',()=>{
  it.each(['https://inktag.dev','WIFI:T:WPA;S:INKTAG-STUDIO;P:inktag1234;;'])('preserves every module and quiet zone: %s',value=>{
    const matrix=createQrMatrix(value),total=matrix.size+8
    for(let size=total;size<200;size++){
      const qr=scaledQrRuns(matrix,size+12,size),pixels=new Uint8Array(size*size)
      expect(qr.size).toBe(size)
      for(const run of qr.runs){
        expect([run.x,run.y,run.width,run.height].every(Number.isInteger)).toBe(true)
        for(let y=run.y;y<run.y+run.height;y++)for(let x=run.x-qr.left;x<run.x-qr.left+run.width;x++)pixels[y*size+x]=1
      }
      const actual:number[]=[],expected:number[]=[]
      for(let y=0;y<total;y++)for(let x=0;x<total;x++){
        const px=Math.round(x*size/total),py=Math.round(y*size/total)
        actual.push(pixels[py*size+px]);expected.push(Number(x>=4&&y>=4&&x<total-4&&y<total-4&&qrModule(matrix,y-4,x-4)))
      }
      expect(actual).toEqual(expected)
    }
  })
  it('refuses fewer than one pixel per module instead of corrupting the code',()=>{
    const matrix=createQrMatrix('https://inktag.dev')
    expect(()=>scaledQrRuns(matrix,matrix.size+7,100)).toThrow('二维码区域太小')
  })
})
