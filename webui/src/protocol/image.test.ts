import { describe, expect, it } from 'vitest'
import { decodeStoredImage, encodeImage, imageCrc32 } from './image'
import { makeScreenConfig, TransferFormat } from './screen'

function frame(colors: Array<[number, number, number]>) {
  return {
    width: colors.length,
    height: 1,
    data: new Uint8ClampedArray(colors.flatMap(([r, g, b]) => [r, g, b, 255])),
  }
}

describe('image wire encoders', () => {
  it.each([[1,400,400], [2,720,480]])('shares Osprey 0x41 recipe %i native palette', (recipe,width,height) => {
    const screen = makeScreenConfig({driver:0x41, recipe, color:10, transform:0, width, height,
      visibleX:0,visibleY:0,visibleWidth:width,visibleHeight:height})
    const data = new Uint8ClampedArray(width*height*4).fill(255)
    const colors = [[0,0,0],[255,255,255],[235,194,25],[220,48,38],[0,0,255],[41,204,20]]
    colors.forEach((rgb,index) => data.set([...rgb,255],index*4))
    const encoded = encodeImage({width,height,data},screen)
    expect(encoded.transfers).toHaveLength(1)
    expect(encoded.transfers[0].step).toBe('bw')
    expect(encoded.transfers[0].data).toHaveLength(width*height/2)
    expect([...encoded.transfers[0].data.slice(0,3)]).toEqual([0x01,0x23,0x56])
    const decoded = decodeStoredImage(encoded.transfers[0].data,screen,5,1).data
    expect(decoded.length).toBe(data.length)
    // Compare every byte without Vitest's costly recursive million-item diff.
    expect(decoded.every((value,index) => value === data[index])).toBe(true)
  })
  it('keeps Osprey single-pass six-color nibbles distinct from UC8159 and Lenmory', () => {
    const screen = makeScreenConfig({ driver: 0x40, recipe: 1, color: 10, width: 240, height: 240,
      transform: 0, visibleX: 0, visibleY: 0, visibleWidth: 240, visibleHeight: 240 })
    const colors: Array<[number, number, number]> = [
      [0,0,0], [255,255,255], [235,194,25], [220,48,38], [0,0,255], [41,204,20],
    ]
    const data = new Uint8ClampedArray(240*240*4).fill(255)
    colors.forEach((rgb,index) => data.set([...rgb,255],index*4))
    const encoded = encodeImage({width:240,height:240,data},screen)
    expect(encoded.format).toBe(TransferFormat.INDEXED_4BPP)
    expect(encoded.transfers).toHaveLength(1)
    expect(encoded.transfers[0].step).toBe('bw')
    const payload = encoded.transfers[0].data
    expect(payload).toHaveLength(28800)
    expect([...payload.slice(0,3)]).toEqual([0x01,0x23,0x56])
    expect([...decodeStoredImage(payload,screen,5,1).data.slice(0,24)]).toEqual([...data.slice(0,24)])
    payload[0]=0x44
    expect(() => decodeStoredImage(payload,screen,5,1)).toThrow('色码无效')
  })
  it('packs monochrome pixels most-significant-bit first', () => {
    const screen = makeScreenConfig({ driver: 0x13, color: 1, transform: 0, width: 8, height: 1, visibleX: 0, visibleY: 0, visibleWidth: 8, visibleHeight: 1 })
    const image = frame(Array.from({ length: 8 }, (_, index) => index % 2 ? [255, 255, 255] : [0, 0, 0]))
    const encoded = encodeImage(image, screen)
    expect(encoded.format).toBe(TransferFormat.MONO_1BPP)
    expect([...encoded.transfers[0].data]).toEqual([0x55])
  })

  it('keeps black and accent planes independent', () => {
    const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0, width: 8, height: 1, visibleX: 0, visibleY: 0, visibleWidth: 8, visibleHeight: 1 })
    const black: [number, number, number] = [0, 0, 0], white: [number, number, number] = [255, 255, 255], red: [number, number, number] = [220, 48, 38]
    const encoded = encodeImage(frame([black, white, red, black, white, red, white, red]), screen)
    expect(encoded.format).toBe(TransferFormat.DUAL_1BPP)
    expect([...encoded.transfers[0].data]).toEqual([0x6f])
    expect([...encoded.transfers[1].data]).toEqual([0xda])
  })

  it('packs JD79xxx black, white, yellow and red into two-bit indices', () => {
    const screen = makeScreenConfig({ driver: 0x30, recipe: 1, color: 3, transform: 0, width: 4, height: 1, visibleX: 0, visibleY: 0, visibleWidth: 4, visibleHeight: 1 })
    const encoded = encodeImage(frame([[0, 0, 0], [255, 255, 255], [235, 194, 25], [220, 48, 38]]), screen)
    expect(encoded.format).toBe(TransferFormat.INDEXED_2BPP)
    expect([...encoded.transfers[0].data]).toEqual([0x1b])
  })

  it('emits all four codes for the Hanshow JD79668 packed profile', () => {
    const screen = makeScreenConfig({ driver: 0x33, recipe: 1, color: 3, transform: 0, width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 4, visibleHeight: 1 })
    const image = frame([[0, 0, 0], [255, 255, 255], [235, 194, 25], [220, 48, 38]])
    const encoded = encodeImage(image, screen)
    expect(encoded.format).toBe(TransferFormat.INDEXED_2BPP)
    expect(encoded.transfers[0].data[0]).toBe(0x1b)
    expect([6, 4, 2, 0].map((shift) => (encoded.transfers[0].data[0] >> shift) & 3))
      .toEqual([0, 1, 2, 3])
  })

  it('decodes an explicitly read dual-plane device resource back to visible pixels', () => {
    const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0, width: 8, height: 1, visibleX: 0, visibleY: 0, visibleWidth: 8, visibleHeight: 1 })
    const source = frame([[220, 48, 38], [0, 0, 0], ...Array(6).fill([255, 255, 255])])
    const encoded = encodeImage(source, screen)
    const bytes = new Uint8Array(encoded.transfers.reduce((sum, item) => sum + item.data.length, 0))
    let offset = 0
    for (const transfer of encoded.transfers) { bytes.set(transfer.data, offset); offset += transfer.data.length }
    const decoded = decodeStoredImage(bytes, screen, encoded.format, encoded.transfers.length)
    expect(decoded.width).toBe(8)
    expect([...decoded.data.slice(0, 12)]).toEqual([220, 48, 38, 255, 0, 0, 0, 255, 255, 255, 255, 255])
    const legacy = bytes.slice()
    legacy[0] &= 0x7f // Historical red had both plane bits clear.
    expect([...decodeStoredImage(legacy, screen, encoded.format, encoded.transfers.length).data])
      .toEqual([...decoded.data])
  })

  it('encodes the Lenmory E6 Area0 precursor and Area6 black/yellow pass', () => {
    const screen = makeScreenConfig({
      driver: 0x32, recipe: 1, color: 10, transform: 0,
      width: 760, height: 568, visibleX: 0, visibleY: 0,
      visibleWidth: 760, visibleHeight: 568,
    })
    const colors: Array<[number, number, number]> = [
      [0, 0, 0], [255, 255, 255], [220, 48, 38], [235, 194, 25],
      [41, 204, 20], [0, 0, 255],
    ]
    const data = new Uint8ClampedArray(760 * 568 * 4)
    data.fill(0xff)
    colors.forEach(([r, g, b], index) => data.set([r, g, b, 255], index * 4))
    const encoded = encodeImage({ width: 760, height: 568, data }, screen)
    expect(encoded.format).toBe(TransferFormat.INDEXED_2X2BPP)
    expect(encoded.transfers.map((item) => item.step)).toEqual(['e6-area0', 'e6-area6'])
    expect(encoded.transfers[0].data.length).toBe(107_920)
    expect(encoded.transfers[1].data.length).toBe(107_920)
    expect([...encoded.transfers[0].data.slice(0, 2)]).toEqual([0x5d, 0x85])
    expect([...encoded.transfers[1].data.slice(0, 2)]).toEqual([0x16, 0x55])
  })

  it('uses the standard CRC32 across transfer layers', () => {
    expect(imageCrc32([{ step: 'bw', data: new TextEncoder().encode('123456789') }])).toBe(0xcbf43926)
  })
})
