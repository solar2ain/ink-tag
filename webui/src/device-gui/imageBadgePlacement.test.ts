import {expect,it} from 'vitest'
import {editionBadgeGeometry,editionBadgePlacement} from './editionBadgeBitmap'
for(const [w,h] of [[720,480],[480,720],[250,122],[122,250],[200,200]])for(const edition of ['base','pro'] as const) {
  it(`maps every ${edition} badge pixel to the actual ${w}x${h} device corner`,()=>{
    const original=editionBadgeGeometry(w,h,edition)
    const same=editionBadgePlacement(w,h,w,h,edition)
    expect(same).toEqual({...original,rotation:0})
    const placed=editionBadgePlacement(h,w,w,h,edition)
    expect(editionBadgePlacement(h,w,w,h,edition,true)).toEqual({...editionBadgeGeometry(h,w,edition),rotation:0})
    for(let y=0;y<original.height;y++)for(let x=0;x<original.width;x++) {
      if(w===h)expect(placed).toEqual(same)
      else {
        const previewX=placed.x+y,previewY=placed.y-1-x
        expect([w-1-previewY,previewX]).toEqual([original.x+x,original.y+y])
      }
    }
  })
}
