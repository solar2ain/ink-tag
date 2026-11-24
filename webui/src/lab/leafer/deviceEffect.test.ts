import { describe, expect, it } from 'vitest'
import { deviceEffect, paperPreview } from './deviceEffect'
import { decodeStoredImage, encodeImage } from '../../protocol/image'
import { paletteForColor } from '../../image/processing'
import type { ScreenConfig } from '../../types'

const source = { width: 13, height: 7, data: Uint8ClampedArray.from({ length: 13 * 7 * 4 }, (_, i) => i % 4 === 3 ? 255 : (i * 71) % 256) }
const profile: ScreenConfig = { schemaVersion: 1, driver: 1, recipe: 0, color: 2, transform: 0, nativeUiScale: 0,
  width: 13, height: 7, visibleX: 0, visibleY: 0, visibleWidth: 13, visibleHeight: 7 }
describe('offline device effect', () => {
  it.each([1,2,3,4,5,6,7,8,9,10,11])('never invents color on neutral antialiasing, palette %i',color=>{
    const gray={width:256,height:1,data:Uint8ClampedArray.from({length:1024},(_,i)=>i%4===3?255:Math.floor(i/4))}
    for(const screen of [undefined,{...profile,width:256,height:1,visibleWidth:256,visibleHeight:1,color,driver:color>=10?0x40:1}]) {
      const frame=deviceEffect(gray,color,screen).frame
      for(let i=0;i<frame.data.length;i+=4)expect([...frame.data.slice(i,i+3)]).toEqual([frame.data[i],frame.data[i],frame.data[i]])
    }
  })
  it('changes paper only in the presentation copy, preserving black/red and encoded white',()=>{
    const frame={width:3,height:1,data:new Uint8ClampedArray([255,255,255,255,0,0,0,255,220,48,38,255])}
    expect([...paperPreview(frame).data]).toEqual([230,226,217,255,0,0,0,255,220,48,38,255])
    expect([...frame.data.slice(0,4)]).toEqual([255,255,255,255])
  })
  it.each([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11])('uses only the selected %i palette without changing the source', color => {
    const before = source.data.slice(), result = deviceEffect(source, color)
    expect(result.profileMatched).toBe(false)
    expect([result.frame.width, result.frame.height]).toEqual([13, 7])
    const colors = new Set(paletteForColor(color).map(ink => ink.rgb.join(',')))
    for (let i = 0; i < result.frame.data.length; i += 4) expect(colors.has(result.frame.data.slice(i, i + 3).join(','))).toBe(true)
    expect(source.data).toEqual(before)
  })
  it.each([1, 2, 4, 10])('uses the actual send codec for matching color %i including unaligned rows', color => {
    const screen = { ...profile, color, driver: color === 10 ? 0x40 : 1 }
    const encoded = encodeImage(source, screen)
    const expected = decodeStoredImage(Uint8Array.from(encoded.transfers.flatMap(transfer => [...transfer.data])), screen, encoded.format, encoded.transfers.length)
    expect(deviceEffect(source, color, screen)).toEqual({ frame: expected, profileMatched: true })
  })
  it('does not silently resize or use a different device palette', () => {
    expect(deviceEffect(source, 1, profile).profileMatched).toBe(false)
    expect(deviceEffect(source, 2, { ...profile, visibleWidth: 12 }).profileMatched).toBe(false)
  })
  it('keeps logical orientation when the physical screen is rotated', () => {
    const screen = { ...profile, width: 7, height: 13, visibleWidth: 7, visibleHeight: 13, transform: 1 }
    expect(deviceEffect(source, 2, screen).frame).toEqual(deviceEffect(source, 2, profile).frame)
  })
})
