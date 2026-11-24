import { expect, it, vi } from 'vitest'
import { DevicePixelGfx } from './devicePixelGfx'
import { renderFirmwareNativePage } from './nativePageRenderer'
import type { NativeScene } from './nativeScene'

it('small squares keep the colon fixed even for a narrow leading 1', () => {
  const original = DevicePixelGfx.prototype.fillRect
  let colons: Array<[number, number]> = []
  const spy = vi.spyOn(DevicePixelGfx.prototype, 'fillRect').mockImplementation(function (
    this: DevicePixelGfx, x, y, width, height, color,
  ) {
    if (width === 6 && height === 6 && y > 50 && y < 150 && color === 1) colons.push([x, y])
    original.call(this, x, y, width, height, color)
  })
  try {
    let expected: Array<[number, number]> | undefined
    for (const [hour, minute] of [[0, 0], [11, 11], [23, 59], [9, 59], [10, 0], [23, 59], [24, 0]]) {
      colons = []
      renderFirmwareNativePage({ width: 200, height: 200, page: 'clock', layoutRevision: 'v1',
        timestamp: Math.floor(1792022400 / 86400) * 86400 + hour * 3600 + minute * 60 })
      expect(colons).toHaveLength(2)
      expected ??= colons
      expect(colons).toEqual(expected)
    }
  } finally {
    spy.mockRestore()
  }
})

it('keeps three square clocks readable with proportionate fixed digits and margins across all hours',()=>{
  for(const side of [152,200,240])for(let hour=0;hour<24;hour++)for(const minute of [0,11,28,50,59]){
    const scene:NativeScene={parts:[]}
    const frame=renderFirmwareNativePage({width:side,height:side,page:'clock',scene,
      timestamp:Date.UTC(2026,8,25,hour,minute)/1000})
    const clock=scene.parts.find(p=>p.element.type==='clock')!.element.source
    const top=clock.y,height=clock.height,size=side===240 ? 4:side===200 ? 3:2
    let left=side,right=-1
    for(let y=top;y<top+height;y++)for(let x=0;x<side;x++)if(frame.pixels[y*side+x]===1){left=Math.min(left,x);right=Math.max(right,x)}
    const narrowLeadingOne=hour>=10&&hour<20 ? 7*size+1:0
    expect(Math.abs(left+right-(side-1)-narrowLeadingOne),`${side}/${hour}:${minute}`).toBeLessThanOrEqual(1)
    const minimumMargin=side===240 ? 20:Math.floor(side/10)
    expect(left).toBeGreaterThanOrEqual(minimumMargin)
    expect(side-1-right).toBeGreaterThanOrEqual(minimumMargin)
    if(hour===0&&minute===0)expect(right-left+1).toBeGreaterThan(side*0.6)
  }
})
