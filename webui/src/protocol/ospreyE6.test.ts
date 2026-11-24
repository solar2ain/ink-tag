import { describe, expect, it } from 'vitest'
import { Buffer } from 'node:buffer'
import { encodeImage, decodeStoredImage } from './image'
import { canvasSizeForScreen, makeScreenConfig, validateScreenConfig } from './screen'
import { deviceScreenProfiles } from '../data/screenProfiles'

describe('Osptek native E6 profiles', () => {
  it.each(Array.from({ length: 16 }, (_, i) => i))('uses the later 720x480 RAM checker on transform %i, not only matching frame length', transform => {
    const screen=makeScreenConfig({ driver:0x41,recipe:2,color:10,transform,width:720,height:480,
      visibleX:0,visibleY:0,visibleWidth:720,visibleHeight:480 })
    const {width,height}=canvasSizeForScreen(screen)
    expect([width,height]).toEqual(transform&1 ? [480,720]:[720,480])
    const pigment=(x:number,y:number)=>(Math.floor(x/90)+Math.floor(y/80))%2===0 ? 2:3
    const colors=[[235,194,25,255],[220,48,38,255]]
    const data=new Uint8ClampedArray(width*height*4)
    for(let y=0;y<height;y++)for(let x=0;x<width;x++) {
      const rotation=transform&3
      let [rx,ry]=rotation===0 ? [x,y]:rotation===1 ? [719-y,x]:rotation===2 ? [719-x,479-y]:[y,479-x]
      if(transform&4)rx=719-rx
      if(transform&8)ry=479-ry
      data.set(colors[pigment(rx,ry)-2],(y*width+x)*4)
    }
    const encoded=encodeImage({width,height,data},screen)
    const expected=new Uint8Array(172800)
    for(let y=0;y<480;y++)for(let x=0;x<720;x+=2)expected[y*360+x/2]=pigment(x,y)<<4|pigment(x+1,y)
    expect(encoded.format).toBe(5)
    expect(encoded.transfers).toHaveLength(1)
    expect(Buffer.compare(Buffer.from(encoded.transfers[0].data),Buffer.from(expected))).toBe(0)
    expect(expected[0]).toBe(0x22);expect(expected[45]).toBe(0x33);expect(expected[80*360]).toBe(0x33)
  })
  it('rejects the retired portrait RAM dimensions without inventing a compatibility recipe',()=>{
    expect(validateScreenConfig(makeScreenConfig({driver:0x41,recipe:2,color:10,transform:0,
      width:480,height:720,visibleX:0,visibleY:0,visibleWidth:480,visibleHeight:720}))).toBe(false)
  })
  for (const [driver, recipe, width, height] of [[0x40, 1, 240, 240], [0x41, 1, 400, 400], [0x41, 2, 720, 480]]) {
    it(`roundtrips six pigments on ${width}x${height} recipe ${recipe}`, () => {
      const screen = makeScreenConfig({ driver, recipe, color: 10, transform: 0,
        width, height, visibleX: 0, visibleY: 0, visibleWidth: width, visibleHeight: height })
      expect(validateScreenConfig(screen)).toBe(true)
      const data = new Uint8ClampedArray(width * height * 4).fill(255)
      const palette = [[0, 0, 0], [255, 255, 255], [235, 194, 25], [220, 48, 38], [0, 0, 255], [41, 204, 20]]
      palette.forEach((rgb, pixel) => data.set([...rgb, 255], pixel * 4))
      const encoded = encodeImage({ width, height, data }, screen)
      expect(encoded.format).toBe(5)
      expect(encoded.transfers.length).toBe(1)
      expect(encoded.transfers[0].data.length).toBe(width * height / 2)
      expect([...encoded.transfers[0].data.slice(0, 3)]).toEqual([0x01, 0x23, 0x56])
      // Compare every byte without the generic deep-equality walk over a
      // 1.38 MB typed array (which can exhaust the normal test timeout).
      expect(Buffer.compare(Buffer.from(decodeStoredImage(encoded.transfers[0].data, screen, 5, 1).data), Buffer.from(data))).toBe(0)
      const corrupt = encoded.transfers[0].data.slice()
      corrupt[0] = 0x44
      expect(() => decodeStoredImage(corrupt, screen, 5, 1)).toThrow()
    })
  }
  it('exposes exactly three experimental catalog entries without changing other panels', () => {
    const profiles = deviceScreenProfiles.filter((item) => item.id.startsWith('osprey_'))
    expect(profiles.map((item) => item.id)).toEqual(['osprey_154_e6', 'osprey_169_e6', 'osprey_370_e6'])
    expect(profiles.every((item) => validateScreenConfig(item.config))).toBe(true)
  })
})
