import { describe, expect, it, vi } from 'vitest'
import { DevicePixelGfx } from './devicePixelGfx'
import * as components from './deviceGuiComponents'
import { renderFirmwareNativePage, v1DeviceInfoQrGeometry, v1PowerStatusFrame, v1LayoutPresetForSize, v1CalendarColumnBounds } from './nativePageRenderer'
import type { NativeScene } from './nativeScene'
import golden from './layoutV1CandidateGolden.generated.json'
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
const { PNG } = createRequire(import.meta.url)('pngjs')

const screens = [[152, 152], [200, 200], [240, 240], [212, 104], [250, 128], [296, 152], [400, 300], [600, 448], [752, 568], [800, 480]] as const
const scales = [1, 4, 2, 5, 3] as const
const scaleEnums: Record<number, number> = {100: 1, 125: 4, 150: 2, 175: 5, 200: 3}
const currentFont = (gfx: DevicePixelGfx) => (gfx as unknown as {fontId:string}).fontId

function writeQaFrame(frame: ReturnType<typeof renderFirmwareNativePage>, name: string) {
  if (process.env.INKTAG_LAYOUT_QA !== '1') return
  const png=new PNG({width:frame.width,height:frame.height}), palette=[[255,255,255],[23,26,24],[200,45,40]]
  frame.pixels.forEach((pixel,i)=>{
    palette[pixel]!.forEach((v,c)=>{png.data[i*4+c]=v}); png.data[i*4+3]=255
  })
  mkdirSync(new URL('../../../.local/layout-v1-candidate/',import.meta.url),{recursive:true})
  writeFileSync(new URL(`../../../.local/layout-v1-candidate/qa-${name}.png`,import.meta.url),PNG.sync.write(png))
}

describe('V1 design contracts', () => {
  it('keeps all small-square temperature digits and Celsius in the same unscaled WQY9 face', () => {
    const print=DevicePixelGfx.prototype.print
    const calls:{text:string;font:string;scale:number}[]=[]
    const spy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      calls.push({text,font:currentFont(this),scale:this.fontScaleQ8})
      return print.call(this,text)
    })
    try {
      for(const [width,height] of screens) for(const page of ['calendar','clock'] as const) {
        calls.length=0
        renderFirmwareNativePage({width,height,page,layoutRevision:'v1',temperature:-24,voltage:3300})
        const temperature=calls.find(call=>call.text==='-24℃')!
        expect(temperature.font).toBe('wqy9-lunar')
        expect(temperature.scale).toBe(256)
        const voltage=calls.find(call=>call.text==='3.3V')
        if(voltage) {expect(voltage.font).toBe('wqy9-lunar');expect(voltage.scale).toBe(256)}
      }
    } finally {spy.mockRestore()}
  })

  it('trims compact selection below the last actual date ink instead of the full cell', () => {
    const fill=DevicePixelGfx.prototype.fillRoundRect,print=DevicePixelGfx.prototype.print,pixel=DevicePixelGfx.prototype.drawPixel
    let box:{y:number;height:number}|undefined,inkBottom=-1,active=false
    const fillSpy=vi.spyOn(DevicePixelGfx.prototype,'fillRoundRect').mockImplementation(function(this:DevicePixelGfx,x,y,w,h,r,c) {
      if(c===2&&y>30&&w<this.width/6&&h>8)box={y,height:h}
      return fill.call(this,x,y,w,h,r,c)
    })
    const pixelSpy=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,c) {
      if(active&&c===0&&box&&y>=box.y&&y<box.y+box.height)inkBottom=Math.max(inkBottom,y)
      return pixel.call(this,x,y,c)
    })
    const printSpy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      active=this.foreground===0&&this.ty>30
      const result=print.call(this,text);active=false;return result
    })
    try {
      for(const [width,height] of screens.filter(([width])=>width<350)) for(const weekStart of [0,1]) {
        box=undefined;inkBottom=-1
        const frame=renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1',weekStart,
          timestamp:Date.UTC(2026,10,18,14,28)/1000})
        expect(box,`${width}: selection`).toBeDefined()
        expect(inkBottom).toBeGreaterThanOrEqual(box!.y)
        expect(box!.y+box!.height-1-inkBottom,`${width}: bottom padding`).toBeLessThanOrEqual(2)
        writeQaFrame(frame,`${width}-compact-selected-padding-week-${weekStart}`)
      }
    } finally {fillSpy.mockRestore();pixelSpy.mockRestore();printSpy.mockRestore()}
  })

  it('centres compact number/lunar pairs as one close two-line block', () => {
    const print=DevicePixelGfx.prototype.print
    const lines:{text:string;y:number;color:number}[]=[]
    const spy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      lines.push({text,y:this.ty,color:this.foreground});return print.call(this,text)
    })
    try {
      for(const [width,height] of [[240,240],[296,152]]) for(const month of [5,9,11]) for(const weekStart of [0,1]) {
        lines.length=0
        const frame=renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1',weekStart,
          timestamp:Date.UTC(2026,month!-1,18,14,28)/1000})
        const numeral=lines.find(line=>line.text==='18'&&line.color===0)!
        const lunar=lines.find(line=>line.color===0&&line.y>numeral.y&&!/^\d/.test(line.text))!
        expect(lunar.y-numeral.y,`${width}/${month}/${weekStart}: line gap`).toBeLessThanOrEqual(12)
        expect(lunar.y-numeral.y).toBeGreaterThanOrEqual(9)
        writeQaFrame(frame,`${width}-compact-date-lunar-${month}-week-${weekStart}`)
      }
    } finally {spy.mockRestore()}
  })

  it('keeps native WQY9 weekdays regular and bolds only promoted wide glyphs', () => {
    const print=DevicePixelGfx.prototype.print
    const calls:{text:string,x:number,y:number,scale:number}[]=[]
    const spy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text){
      if(this.foreground===0 && /^[日一二三四五六]$/.test(text)) calls.push({text,x:this.tx,y:this.ty,scale:this.fontScaleQ8})
      return print.call(this,text)
    })
    try {
      for(const [width,height] of screens) for(const nativeUiScale of scales){
        calls.length=0
        const frame=renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1',nativeUiScale})
        const band=calls.filter(call=>call.y>=calls[0]!.y && call.y<=calls[0]!.y+1)
        const bold = width >= 350 && nativeUiScale !== 1
        expect(band).toHaveLength(bold?28:7)
        // 288 requests the regular WQY9 master, not the promoted WQY12 face.
        if(width===200&&height===200) expect(band.every(call=>call.scale===256)).toBe(true)
        if(bold) for(let column=0;column<7;column++){
          const regular=band[column*4]!
          for(let offset=1;offset<4;offset++) expect(band[column*4+offset]).toEqual({...regular,x:regular.x+(offset&1),y:regular.y+(offset>>1)})
        }
        writeQaFrame(frame,`${width}-calendar-bold-weekdays-${nativeUiScale}`)
      }
    } finally {spy.mockRestore()}
  })

  it('centres wide weekday ink vertically including the bold overprint', () => {
    const print=DevicePixelGfx.prototype.print,pixel=DevicePixelGfx.prototype.drawPixel
    let active=false,top=Infinity,bottom=-Infinity
    const prints=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text){
      active=this.foreground===0 && /^[日一二三四五六]$/.test(text)
      const result=print.call(this,text);active=false;return result
    })
    const pixels=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,color){
      if(active && color===0){top=Math.min(top,y);bottom=Math.max(bottom,y)}
      return pixel.call(this,x,y,color)
    })
    try {
      for(const [width,height] of screens.filter(([w])=>w>=400)) for(const nativeUiScale of scales){
        top=Infinity;bottom=-Infinity
        const frame=renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1',nativeUiScale})
        const bandY=v1PowerStatusFrame({width,height,nativeUiScale,page:'calendar'}).height+(width===400&&height===300?22:26)
        const gridHeight=height-bandY-(width>500?33:23)
        const bandHeight=Math.max(15,Math.min(Math.trunc(gridHeight/6),height>=400?42:26))
        const above=top-bandY,below=bandY+bandHeight-1-bottom
        expect(Math.abs(above-below),`${width}/${nativeUiScale}: above ${above}, below ${below}`).toBeLessThanOrEqual(1)
        expect(Math.min(above,below)).toBeGreaterThanOrEqual(2)
        writeQaFrame(frame,`${width}-calendar-centred-weekdays-${nativeUiScale}`)
      }
    } finally {prints.mockRestore();pixels.mockRestore()}
  })

  it('preserves every battery level when simplifying the C fixed-point formula', () => {
    for (let millivolts=0; millivolts<=65535; millivolts++) {
      const sample=Math.trunc(millivolts*2047/3600)
      const simplified=sample>1705 ? 100 : sample>1584 ? 28+Math.trunc((sample-1584)*72/121)
        : sample>1360 ? 4+Math.trunc((sample-1360)*3/28) : sample>1136 ? Math.trunc((sample-1136)/56) : 0
      expect(simplified).toBe(components.deviceBatteryPercent(millivolts))
    }
  })

  it('keeps wide native lunar glyphs separated from enlarged numerals by the layout gap', () => {
    const print=DevicePixelGfx.prototype.print, pixel=DevicePixelGfx.prototype.drawPixel
    let numberBottom=-Infinity, top=Infinity, bottom=-Infinity, tracking=false, haveNumber=false
    const gaps:number[]=[]
    const pixelSpy=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,color) {
      if (tracking) {top=Math.min(top,y);bottom=Math.max(bottom,y)}
      return pixel.call(this,x,y,color)
    })
    const printSpy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      const number=this.ty > v1PowerStatusFrame({width:this.width,height:this.height,nativeUiScale:1}).height + 26
        && currentFont(this)==='helv-bold-14-num' && /^\d{1,2}$/.test(text)
      const lunar=haveNumber && /[\u4e00-\u9fff]/.test(text) && text!=='班' && text!=='休'
      top=Infinity;bottom=-Infinity;tracking=number || lunar
      const result=print.call(this,text); tracking=false
      if (number) {numberBottom=bottom;haveNumber=true}
      if (lunar) {expect(this.fontScaleQ8).toBe(256);gaps.push(top-numberBottom-1);haveNumber=false}
      return result
    })
    try {
      for (const [width,height] of screens.filter(([w])=>w>=350)) for (const nativeUiScale of scales) for(const month of [2,8]) {
        gaps.length=0;haveNumber=false
        renderFirmwareNativePage({width,height,nativeUiScale,page:'calendar',layoutRevision:'v1',timestamp:Date.UTC(2026,month,25,14,28)/1000})
        expect(gaps.length).toBeGreaterThanOrEqual(28)
        expect(Math.min(...gaps),`${width}/${nativeUiScale}/${month}: lunar gap`).toBeGreaterThanOrEqual(width>500?4:2)
        expect(Math.max(...gaps),`${width}/${nativeUiScale}/${month}: lunar gap`).toBeLessThanOrEqual(width>500?6:4)
      }
    } finally {printSpy.mockRestore();pixelSpy.mockRestore()}
  })

  it('keeps bracketed calendar/clock BLE labels at native size', () => {
    const print = DevicePixelGfx.prototype.print
    const calls: {text:string;scale:number;font:string}[] = []
    const spy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this:DevicePixelGfx,text) {
      if (text.includes('3A57')) calls.push({text,scale:this.fontScaleQ8,font:currentFont(this)})
      return print.call(this,text)
    })
    try {
      for (const [width,height] of screens) for (const nativeUiScale of scales) for (const page of ['calendar','clock'] as const) {
        calls.length=0
        renderFirmwareNativePage({width,height,page,nativeUiScale,layoutRevision:'v1'})
        expect(calls.length).toBeGreaterThan(0)
        for (const call of calls) {
          expect(call.text.startsWith('[') && call.text.endsWith(']')).toBe(true)
          if (page==='clock' && width<350) expect([call.font,call.scale]).toEqual(['wqy9-lunar',256])
        }
      }
    } finally {spy.mockRestore()}
  })

  it('uses ID4 for compact footers and the complete factory name for wider footers at every scale', () => {
    const print = DevicePixelGfx.prototype.print, labels: string[] = []
    const spy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      if (text.includes('3A57')) labels.push(text)
      return print.call(this, text)
    })
    try {
      for (const [width, height] of screens) for (const nativeUiScale of scales) {
        for (const platform of ['GR', 'OM', 'TL', 'NRF', 'DA']) for (const page of ['calendar', 'clock'] as const) {
          labels.length = 0
          const name = `IKT_${platform}_3A57`
          renderFirmwareNativePage({width, height, nativeUiScale, page, layoutRevision: 'v1', bluetoothName: name, edition: 'pro'})
          const expected = width < 300 && height < 300 ? '3A57' : name
          // Device-info's separate NAME row is a friendly label, not the BLE field.
          expect(labels).toContain(`[${expected}]`)
          expect(labels).not.toContain(`${platform}_3A57`)
          expect(labels).not.toContain(`[${platform}_3A57]`)
        }
      }
    } finally {spy.mockRestore()}
  })

  it('keeps compact footer content clear of the visible Pro badge with normal margins', () => {
    const print = DevicePixelGfx.prototype.print, draw = components.drawInkTagEditionBadge
    const calls: {text: string; x: number; y: number; width: number}[] = []
    let badge: ReturnType<typeof draw> | undefined
    const labels = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      calls.push({text, x: this.tx, y: this.ty, width: this.getUTF8Width(text)})
      return print.call(this, text)
    })
    const badges = vi.spyOn(components, 'drawInkTagEditionBadge').mockImplementation((...args) => (badge = draw(...args)))
    const report: object[] = []
    // A full lunar cycle including the 2025 leap month, not only one short date.
    const dates = Array.from({length: 400}, (_, day) => Date.UTC(2025, 5, 1 + day, 14, 28))
    try {
      for (const [width, height] of screens.filter(([w]) => w < 300)) for (const page of ['calendar', 'clock'] as const) {
        let minimumGap = Infinity, maximumGap = 0
        for (const timestamp of dates) {
          calls.length = 0; badge = undefined
          const frame = renderFirmwareNativePage({width, height, page, layoutRevision: 'v1', edition: 'pro', activated: true,
            bluetoothName: 'IKT_GR_3A57', timestamp: timestamp / 1000})
          const footer = calls.filter(call => call.y > height - (width===200&&height===200 ? 23:18) && call.text.trim())
          const currentBadge = badge as ReturnType<typeof draw> | undefined
          expect(currentBadge?.width).toBe(59)
          expect(width - currentBadge!.x - currentBadge!.width).toBe(3)
          expect(footer[0]!.x).toBe(3)
          const gap = currentBadge!.x - Math.max(...footer.map(call => call.x + call.width))
          expect(gap, `${width}/${page}: label gap`).toBeGreaterThanOrEqual(6)
          minimumGap = Math.min(minimumGap, gap); maximumGap = Math.max(maximumGap, gap)
          if (timestamp === dates[0] || gap === minimumGap && gap !== maximumGap)
            writeQaFrame(frame, `${width}-${page}-pro-footer`)
        }
        report.push({width, height, page, minimumGap, maximumGap, spareAfterSixPixelGap: minimumGap - 6})
      }
      if (process.env.INKTAG_LAYOUT_QA === '1') writeFileSync(new URL('../../../.local/layout-v1-candidate/pro-footer-space.json', import.meta.url), JSON.stringify(report, null, 2))
    } finally {labels.mockRestore(); badges.mockRestore()}
  }, 60_000)

  it('centres every wide calendar numeral independently of work/holiday labels', () => {
    const print = DevicePixelGfx.prototype.print
    const spy = vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      if (this.ty > v1PowerStatusFrame({width:this.width,height:this.height,nativeUiScale:1}).height + 26
        && currentFont(this)==='helv-bold-14-num' && /^\d{1,2}$/.test(text)) {
        const layout=Math.max(256,Math.min(512,Math.trunc(this.width*256/400),Math.trunc(this.height*256/300)))
        const margin=Math.trunc((10*layout+128)/256), cell=Math.trunc((this.width-margin*2)/7)
        const col=Math.trunc((this.tx-margin)/cell)
        const bounds=v1CalendarColumnBounds(this.width-margin*2,col)
        expect(Math.abs(this.tx+this.getUTF8Width(text)/2-(margin+bounds.x+bounds.width/2))).toBeLessThanOrEqual(1)
      }
      return print.call(this,text)
    })
    try {
      for (const [width,height] of screens.filter(([w])=>w>=350)) for (const nativeUiScale of scales)
        renderFirmwareNativePage({width,height,nativeUiScale,page:'calendar',layoutRevision:'v1'})
    } finally {spy.mockRestore()}
  })

  it('keeps native WQY9 power and branding in the shared header and BLE black', () => {
    const print = DevicePixelGfx.prototype.print
    const bleColors: number[] = []
    const spy = vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this: DevicePixelGfx,text) {
      if (text==='3A57' || text==='IKT_GR_3A57') bleColors.push(this.foreground)
      return print.call(this,text)
    })
    try {
      for (const [width,height] of screens) for (const nativeUiScale of scales) {
        const frame=renderFirmwareNativePage({width,height,page:'device-info',nativeUiScale,layoutRevision:'v1'})
        const status=v1PowerStatusFrame({width,height,nativeUiScale})
        const regions=[{start:0,end:status.x-2},{start:status.x,end:width}]
        const centers=regions.map(region=>{
          const ys:number[]=[]
          for(let y=0;y<status.height-1;y++) for(let x=region.start;x<region.end;x++) if(frame.pixels[y*width+x]!==0) ys.push(y)
          return (Math.min(...ys)+Math.max(...ys))/2
        })
        // Shipping C keeps its brand baseline; native WQY9's ink centre is
        // 2.5px below that mark on wide screens. Full C-pixel parity is below.
        expect(Math.abs(centers[0]!-centers[1]!),`${width}/${nativeUiScale}: brand ${centers[0]}, power ${centers[1]}`).toBeLessThanOrEqual(3)
      }
      expect(bleColors.every(color=>color===1)).toBe(true)
    } finally {spy.mockRestore()}
  })

  it('uses the BWRY pigment set in the shared device-info brand mark', () => {
    const gfx = new DevicePixelGfx(39, 28)
    components.drawInkTagDeviceMark(gfx, { x: 0, y: 0, width: 39, height: 28 }, 3)
    expect(new Set(gfx.frame.pixels)).toEqual(new Set([0, 1, 2, 3]))
    expect(gfx.frame.pixels[14 * 39 + 8]).toBe(1)
    expect(gfx.frame.pixels[14 * 39 + 16]).toBe(2)
    expect(gfx.frame.pixels[14 * 39 + 26]).toBe(3)
    expect(gfx.frame.pixels[14 * 39 + 36]).toBe(0)
  })

  it('uses connected dashed rails on compact calendars and a full grid on wider calendars', () => {
    const dotted=vi.spyOn(DevicePixelGfx.prototype,'drawDottedLine'), vertical=vi.spyOn(DevicePixelGfx.prototype,'drawFastVLine')
    const horizontal=vi.spyOn(DevicePixelGfx.prototype,'drawFastHLine')
    try {
      for(const [width,height] of screens.filter(([w,h])=>w!==h)) {
        dotted.mockClear(); vertical.mockClear(); horizontal.mockClear()
        renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1'})
        if(width<350) {
          const rail=width===296?90:74, divider=width-rail-5
          expect(vertical.mock.calls).toContainEqual([divider,2,height-19,1])
          const offsets=width===296 ? [64,40] : [56,36]
          expect(dotted.mock.calls).toEqual(offsets.map(offset=>height-offset).map(y=>[divider,y,width-3,y,1,2,2]))
          expect(horizontal.mock.calls).toContainEqual([divider,17,width-3-divider+1,1])
        } else {
          expect(dotted.mock.calls.filter(([x,,endX])=>x===endX)).toHaveLength(6)
          expect(dotted.mock.calls.filter(([,y,,endY])=>y===endY)).toHaveLength(4)
        }
      }
    } finally {dotted.mockRestore(); vertical.mockRestore(); horizontal.mockRestore()}
  })

  it('redistributes 2.13 day whitespace to centred year-month and weekday rows', () => {
    for (const [width,height] of [[212,104],[250,128]]) for (const nativeUiScale of scales) {
      const frame=renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1',nativeUiScale})
      const rows=[[18,height-56],[height-55,height-36],[height-35,height-18]]
      for (const [top,bottom] of rows) {
        const inkRows:number[]=[]
        for (let y=top;y<bottom;y++) {
          if (frame.pixels.slice(y*width+width-77,y*width+width-3).some(pixel=>pixel!==0)) inkRows.push(y)
        }
        expect(inkRows.length).toBeGreaterThan(0)
        const above=Math.min(...inkRows)-top, below=bottom-1-Math.max(...inkRows)
        expect(Math.min(above,below),`${width}/${nativeUiScale}/${top}: padding`).toBeGreaterThanOrEqual(3)
        expect(Math.abs(above-below),`${width}/${nativeUiScale}/${top}: centring`).toBeLessThanOrEqual(1)
      }
    }
  })

  it('keeps narrowed 2.13 rails clear of temperature ink', () => {
    const print=DevicePixelGfx.prototype.print, pixel=DevicePixelGfx.prototype.drawPixel
    let active=false, left=Infinity
    const pixelSpy=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,color) {
      if (active && color!==0) left=Math.min(left,x)
      return pixel.call(this,x,y,color)
    })
    const printSpy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      active=text.includes('℃'); const result=print.call(this,text); active=false; return result
    })
    try {
      for (const [width,height] of [[212,104],[250,128]]) for (const temperature of [0,9,24,99,-9,-10,-20,-127,127]) {
        left=Infinity
        const frame=renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1',temperature})
        writeQaFrame(frame,`${width}-rail-temperature-${temperature}`)
        const rail=Math.max(74,v1PowerStatusFrame({width,height,nativeUiScale:1,temperature}).width+1)
        expect(left-(width-rail-5)-1,`${width}/${temperature}: temperature padding`).toBeGreaterThanOrEqual(2)
      }
    } finally {printSpy.mockRestore();pixelSpy.mockRestore()}
  })

  it('keeps small power labels two pixels apart without moving voltage or changing larger screens', () => {
    const print=DevicePixelGfx.prototype.print
    let temperatureEnd=0, voltageX=0
    const spy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      if (text.includes('℃')) temperatureEnd=this.tx+this.getUTF8Width(text)
      if (/^\d\.\dV$/.test(text)) voltageX=this.tx
      return print.call(this,text)
    })
    try {
      for (const [width,height] of screens) for (const page of ['calendar','clock','device-info'] as const) {
        temperatureEnd=0; voltageX=0
        renderFirmwareNativePage({width,height,page,layoutRevision:'v1',nativeUiScale:1})
        if (width>=200 && width!==212) expect(voltageX-temperatureEnd,`${width}/${page}`).toBe(width<=250?2:4)
        else expect(voltageX).toBe(0)
      }
      for (let value=-128;value<=127;value++) {
        const absolute=Math.abs(value)
        expect(1+Number(value<0)+Number(absolute>=10)+Number(absolute>=100)).toBe(String(value).length)
      }
      for (let scale=160;scale<=384;scale++) {
        const battery=Math.max(2,Math.trunc((scale*2+128)/256))
        const label=Math.max(4,Math.trunc((scale*4+128)/256))
        expect(battery).toBe(Math.min(battery,4)); expect(label).toBe(Math.min(label,7))
      }
    } finally {spy.mockRestore()}
  })

  it('fits double-digit months and weekdays at native WQY9 in the narrowed rail', () => {
    const print=DevicePixelGfx.prototype.print, pixel=DevicePixelGfx.prototype.drawPixel
    let divider=0, footer=0, active=false, left=Infinity, right=-Infinity
    const metadata:{text:string,left:number,right:number,scale:number,font:string}[]=[]
    const pixels=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,color) {
      if (active && color!==0) {left=Math.min(left,x);right=Math.max(right,x)}
      return pixel.call(this,x,y,color)
    })
    const prints=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      active=this.tx>divider && this.ty>18 && this.ty<footer
      left=Infinity;right=-Infinity
      const result=print.call(this,text)
      if (active) metadata.push({text,left,right,scale:this.fontScaleQ8,font:currentFont(this)})
      active=false;return result
    })
    try {
      for (const [width,height] of [[212,104],[250,128]]) for (const month of [9,10,12]) for (const temperature of [24,-10,127]) {
        const rail=Math.max(74,v1PowerStatusFrame({width,height,nativeUiScale:1,temperature}).width+1)
        divider=width-rail-5;footer=height-18;metadata.length=0
        const frame=renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1',temperature,
          timestamp:Date.UTC(2026,month-1,month===9?30:31,14,28)/1000,bluetoothName:'InkTag_GR_3A57'})
        expect(metadata.slice(0,5).map(run=>run.text)).toEqual(['2026','年',String(month),'月','星期'])
        expect(metadata[5]!.text).toMatch(/^[日一二三四五六]$/)
        for (const run of metadata) {
          expect(run.font).toBe('wqy9-lunar');expect(run.scale).toBe(256)
          expect(run.left-divider-1,`${width}/${month}/${temperature}/${run.text}: left margin`).toBeGreaterThanOrEqual(2)
          expect(width-1-run.right,`${width}/${month}/${temperature}/${run.text}: screen margin`).toBeGreaterThanOrEqual(3)
        }
        writeQaFrame(frame,`${width}-rail-month-${month}-temperature-${temperature}`)
      }
    } finally {prints.mockRestore();pixels.mockRestore()}
  })

  it('uses accent for every compact-rect rail date and black on monochrome panels', () => {
    const spy=vi.spyOn(components,'drawSevenSegmentNumber')
    try {
      for (const [width,height] of [[212,104],[250,128],[296,152]]) for (const colorCount of [1,2,3]) {
        spy.mockClear()
        renderFirmwareNativePage({width,height,page:'calendar',layoutRevision:'v1',colorCount})
        expect(spy.mock.calls).toHaveLength(1)
        expect(spy.mock.calls[0]![5]).toBe(colorCount>1?2:1)
      }
    } finally {spy.mockRestore()}
  })

  it('reserves the square footer for Bluetooth even in full five-row and six-row calendars', () => {
    const print=DevicePixelGfx.prototype.print, pixel=DevicePixelGfx.prototype.drawPixel
    let active=false, top=Infinity, bottom=-Infinity, left=Infinity, right=-Infinity
    const calls:{text:string,y:number,top:number,bottom:number,left:number,right:number}[]=[]
    const pixels=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,color) {
      if(active && color!==0) {top=Math.min(top,y);bottom=Math.max(bottom,y);left=Math.min(left,x);right=Math.max(right,x)}
      return pixel.call(this,x,y,color)
    })
    const prints=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text) {
      const y=this.ty;active=true;top=Infinity;bottom=-Infinity;left=Infinity;right=-Infinity
      const result=print.call(this,text);active=false;calls.push({text,y,top,bottom,left,right});return result
    })
    try {
      for (const width of [152,200]) for (const weekStart of [0,1]) for (const month of [5,8]) {
        calls.length=0
        const frame=renderFirmwareNativePage({width,height:width,page:'calendar',layoutRevision:'v1',weekStart,
          timestamp:Date.UTC(2026,month-1,31,14,28)/1000,bluetoothName:'InkTag_GR_3A57'})
        const days=calls.filter(run=>/^\d{1,2}$/.test(run.text) && run.y>20)
        const ble=calls.find(run=>run.text==='[3A57]')!
        expect(days).toHaveLength(31)
        expect(days.every(run=>run.bottom<width-(width===200 ? 23:18))).toBe(true)
        expect(ble.top).toBeGreaterThanOrEqual(width-(width===200 ? 20:15))
        expect(ble.bottom).toBeLessThanOrEqual(width-3)
        expect(ble.right).toBeLessThanOrEqual(width-67)
        expect(calls.some(run=>run.text==='丙午')).toBe(false)
        expect(calls.some(run=>run.text==='星期')).toBe(false)
        writeQaFrame(frame,`${width}-square-calendar-month-${month}-week-${weekStart}`)
      }
      for (const width of [152,200]) {
        calls.length=0
        const frame=renderFirmwareNativePage({width,height:width,page:'clock',layoutRevision:'v1',bluetoothName:'InkTag_GR_3A57'})
        const footer=width===200 ? 23:18
        const row=calls.filter(run=>run.y>=width-footer-16 && run.y<width-footer)
        expect(row.map(run=>run.text).join('')).toBe('星期五八月十五')
        const rowLeft=Math.min(...row.map(run=>run.left)),rowRight=Math.max(...row.map(run=>run.right))
        expect(Math.abs((rowLeft+rowRight)/2-(width-1)/2)).toBeLessThanOrEqual(1)
        expect(Math.max(...row.map(run=>run.bottom))).toBeLessThanOrEqual(width-footer-3)
        expect(calls.find(run=>run.text==='[3A57]')!.y).toBe(width-(width===200 ? 7:4))
        writeQaFrame(frame,`${width}-square-clock-footer`)
      }
    } finally {prints.mockRestore();pixels.mockRestore()}
  })

  it('centres clock ink in the whole body and leaves symmetric colon gaps', () => {
    const draw = components.drawSevenSegmentNumber
    const numbers: {x: number; y: number; size: number; heightSize: number}[] = []
    const spy = vi.spyOn(components, 'drawSevenSegmentNumber').mockImplementation((gfx, number, x, y, size, foreground, background, digits, heightSize=size) => {
      numbers.push({x,y,size,heightSize})
      return draw(gfx,number,x,y,size,foreground,background,digits,heightSize)
    })
    try {
      for (const [width,height] of screens) for (const hour of [0,12,14,23]) {
        numbers.length = 0
        const frame = renderFirmwareNativePage({width,height,page:'clock',layoutRevision:'v1',timestamp:Date.UTC(2026,8,25,hour,28)/1000})
        const [hours, minutes] = numbers
        expect(numbers).toHaveLength(2)
        const top = width<350 ? v1PowerStatusFrame({width,height,nativeUiScale:1,page:'clock'}).height : v1PowerStatusFrame({width,height,nativeUiScale:1,page:'clock'}).height + (width===400&&height===300?22:26)
        const bottom = height-(width===200&&height===200 ? 23:width<350 ? 18 : width>500 ? 33 : 23)
        const ys: number[] = []
        for (let y=hours!.y; y<hours!.y+18*hours!.heightSize+2*hours!.size+2; y++) {
          if (frame.pixels.slice(y*width,(y+1)*width).some(pixel=>pixel===1)) ys.push(y)
        }
        expect(Math.abs((Math.min(...ys)-top)-(bottom-1-Math.max(...ys))),`${width}/${hour}: vertical ink centring`).toBeLessThanOrEqual(1)
        const padding = Math.max(1,Math.trunc(hours!.size/2))
        const colon = hours!.x+2*(11*hours!.size+2)+padding
        expect(minutes!.x-colon-4*hours!.size).toBe(padding)
        expect(frame.pixels[(hours!.y+Math.trunc(9*hours!.heightSize/2)+1)*width+colon]).toBe(1)
      }
    } finally { spy.mockRestore() }
  })

  it('centres DEVICE INFO ink vertically in the footer at every scale', () => {
    for (const [width, height] of screens) for (const nativeUiScale of scales) {
      const frame = renderFirmwareNativePage({width, height, page: 'device-info', nativeUiScale, layoutRevision: 'v1'})
      const footerY = height - (width > 500 ? 31 : width >= 350 ? 21 : 18)
      const ys: number[] = []
      for (let y = footerY + 1; y < height; y++) for (let x = 3; x < width / 2; x++) {
        if (frame.pixels[y * width + x] === 1) ys.push(y)
      }
      const above = Math.min(...ys) - footerY - 1, below = height - 1 - Math.max(...ys)
      expect(Math.abs(above - below), `${width}/${nativeUiScale}: upper ${above}, lower ${below}`).toBeLessThanOrEqual(1)
      expect(Math.min(above, below)).toBeGreaterThanOrEqual(2)
    }
  })

  it('keeps clock Bluetooth below the separator and clear of the centred time', () => {
    const draw=components.drawSevenSegmentNumber, print=DevicePixelGfx.prototype.print, pixel=DevicePixelGfx.prototype.drawPixel
    let timeBottom=0, timeRight=0, bleTop=Infinity, bleLeft=Infinity, bleBottom=0, active=false
    const numberSpy=vi.spyOn(components,'drawSevenSegmentNumber').mockImplementation((gfx,n,x,y,size,fg,bg,digits)=>{
      timeBottom=Math.max(timeBottom,y+20*size+2); timeRight=Math.max(timeRight,x+20*size+4)
      return draw(gfx,n,x,y,size,fg,bg,digits)
    })
    const printSpy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text){
      active=text.includes('3A57') && text.startsWith('['); const result=print.call(this,text); active=false; return result
    })
    const pixelSpy=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,color){
      if(active) {bleTop=Math.min(bleTop,y);bleLeft=Math.min(bleLeft,x);bleBottom=Math.max(bleBottom,y)}
      return pixel.call(this,x,y,color)
    })
    try {
      for(const [width,height] of screens.filter(([w,h])=>w<350 && w!==h)) for(const nativeUiScale of scales) {
        timeBottom=0; timeRight=0; bleTop=Infinity;bleLeft=Infinity;bleBottom=0
        renderFirmwareNativePage({width,height,page:'clock',layoutRevision:'v1',nativeUiScale})
        expect(Number.isFinite(bleTop)).toBe(true)
        expect(Math.max(bleTop-timeBottom,bleLeft-timeRight),`${width}/${nativeUiScale}: Bluetooth gap`).toBeGreaterThanOrEqual(5)
        expect(bleTop-(height-18)-1).toBeGreaterThanOrEqual(3)
        expect(height-1-bleBottom).toBeGreaterThanOrEqual(2)
      }
    } finally {numberSpy.mockRestore(); printSpy.mockRestore(); pixelSpy.mockRestore()}
  })

  it('aligns wide Bluetooth and DEVICE INFO ink centres and removes only the calendar top rule', () => {
    for(const [width,height] of screens.filter(([w])=>w>=400)) for(const nativeUiScale of scales) {
      const centers:number[]=[]
      for(const page of ['calendar','clock','device-info'] as const) {
        const frame=renderFirmwareNativePage({width,height,page,layoutRevision:'v1',nativeUiScale})
        const footer=height-(width>500?31:21), ys:number[]=[]
        for(let y=footer+1;y<height;y++) for(let x=3;x<width/3;x++) if(frame.pixels[y*width+x]===1) ys.push(y)
        centers.push((Math.min(...ys)+Math.max(...ys))/2)
        if(page!=='device-info') {
          const lineY=v1PowerStatusFrame({width,height,nativeUiScale,page}).height + (width===400&&height===300?21:25)
          expect(frame.pixels[lineY*width+Math.trunc(width/2)]).toBe(page==='clock'?1:0)
        }
      }
      expect(Math.max(...centers)-Math.min(...centers),`${width}/${nativeUiScale}: footer centres`).toBeLessThanOrEqual(1)
    }
  })

  it('preserves lunar strokes with a smaller lunar face in six-row 296 calendars', () => {
    const print=DevicePixelGfx.prototype.print, pixel=DevicePixelGfx.prototype.drawPixel
    let actual:Set<string>|undefined, count=0, rows=5
    const pixelSpy=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,color){
      actual?.add(`${x},${y}`); return pixel.call(this,x,y,color)
    })
    const printSpy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text){
      if(this.width!==296 || this.tx>=196 || this.ty<=16 || this.ty>=134 || !/[\u4e00-\u9fff]/.test(text)) return print.call(this,text)
      const scale=this.fontScaleQ8, x=this.tx, y=this.ty
      const expected:string[]=[]; let advance=0
      for(const char of text) {
        const native=new DevicePixelGfx(32,32); native.fontRasterMode='nearest'; native.setCursor(0,16); print.call(native,char)
        if(rows===5) native.frame.pixels.forEach((color,i)=>{if(color) expected.push(`${x+advance+i%32},${y+Math.trunc(i/32)-16}`)})
        else for(let row=0;row<32;row++)for(let column=0;column<32;){
          if(!native.frame.pixels[row*32+column]){column++;continue}
          const start=column
          while(column<32&&native.frame.pixels[row*32+column])column++
          const round=(n:number)=>Math.floor(n*scale/256+0.5)
          const left=round(start),top=round(row-16)
          // Every original horizontal stroke must retain at least one pixel.
          for(let yy=top;yy<Math.max(top+1,round(row-15));yy++)
            for(let xx=left;xx<Math.max(left+1,round(column));xx++)expected.push(`${x+advance+xx},${y+yy}`)
        }
        advance+=Math.trunc(native.getUTF8Width(char)*scale/256)
      }
      actual=new Set(); const result=print.call(this,text); const drawn=actual; actual=undefined; count++
      expect(scale).toBe(rows===5?256:224)
      expect(expected.every(point=>drawn.has(point)),`${text}/${rows}: dropped source stroke`).toBe(true)
      return result
    })
    try {
      for(const month of [8,9]) {
        rows=month===8?6:5; count=0
        const frame=renderFirmwareNativePage({width:296,height:152,page:'calendar',layoutRevision:'v1',timestamp:Date.UTC(2026,month-1,25)/1000})
        writeQaFrame(frame,`296-calendar-${rows}-rows`)
        expect(count).toBeGreaterThanOrEqual(30)
      }
    } finally {printSpy.mockRestore(); pixelSpy.mockRestore()}
  })

  it('places readable compact dots and wide work/holiday labels inside the selected background', () => {
    const print = DevicePixelGfx.prototype.print, rounded = DevicePixelGfx.prototype.fillRoundRect, rect = DevicePixelGfx.prototype.fillRect
    const pixel = DevicePixelGfx.prototype.drawPixel
    let numeral: {day: number; x: number; y: number; width: number} | undefined, printing = false
    let before: Uint8Array | undefined
    const escaped: string[] = [], labels: string[] = []
    const dots: {day: number; x: number; y: number; size: number; width: number; numeral: NonNullable<typeof numeral>}[] = []
    const capture = (gfx: DevicePixelGfx, x: number, y: number, width: number, size: number, color: number) => {
      if (printing || !numeral || gfx.width >= 350 || y >= gfx.height - (gfx.width===200&&gfx.height===200 ? 28:18)) return
      dots.push({day: numeral.day, x, y, width, size, numeral})
      if (color === 0) {
        // A marker may share white
        // numeral pixels, but must still add visible white pixels to red ink.
        let visible=0
        for(let dy=0;dy<size;dy++)for(let dx=0;dx<width;dx++) {
          if(size===3&&dy!==1&&(dx===0||dx===width-1))continue
          expect(x+dx).toBeGreaterThanOrEqual(2);expect(x+dx).toBeLessThan(gfx.width-2)
          expect(y+dy).toBeGreaterThanOrEqual(2);expect(y+dy).toBeLessThan(gfx.height-18)
          if(gfx.frame.pixels[(y+dy)*gfx.width+x+dx]===2)visible++
        }
        expect(visible,`${gfx.width}: selected holiday marker remains visible`).toBeGreaterThan(0)
      }
    }
    const textSpy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      if (/^\d{1,2}$/.test(text)) numeral = {day: Number(text), x: this.tx, y: this.ty, width: this.getUTF8Width(text)}
      if (text === '班' || text === '休') labels.push(text)
      printing = true; before = this.foreground === 0 && this.background === 2 && !/^[日一二三四五六]$/.test(text) ? this.frame.pixels.slice() : undefined
      const result = print.call(this, text)
      printing = false; before = undefined
      return result
    })
    const dotSpy = vi.spyOn(DevicePixelGfx.prototype, 'fillRoundRect').mockImplementation(function(this: DevicePixelGfx, x,y,w,h,radius,color) {
      if (w === 4 && h === 3 && radius === 1) capture(this, x, y, w, h, color)
      return rounded.call(this,x,y,w,h,radius,color)
    })
    const rectSpy = vi.spyOn(DevicePixelGfx.prototype, 'fillRect').mockImplementation(function(this: DevicePixelGfx, x,y,w,h,color) {
      if (w === 2 && h === 2) capture(this, x,y,w,h,color)
      return rect.call(this,x,y,w,h,color)
    })
    const pixelSpy = vi.spyOn(DevicePixelGfx.prototype, 'drawPixel').mockImplementation(function(this: DevicePixelGfx, x,y,color) {
      if (before && color === 0 && before[y * this.width + x] !== 2) escaped.push(`${this.width}:${x},${y}`)
      return pixel.call(this,x,y,color)
    })
    try {
      // May 2026 is six rows when Sunday-first; its May 9 work marker is selected.
      for (const [width, height] of screens) for (const [month, selected, weekStart] of [[9, 20, 1], [9, 25, 1], [5, 9, 0]]) for (const nativeUiScale of scales) {
        dots.length = 0; labels.length = 0; escaped.length = 0; numeral = undefined
        renderFirmwareNativePage({width, height, nativeUiScale, weekStart, page: 'calendar', layoutRevision: 'v1', timestamp: Date.UTC(2026, month! - 1, selected, 14, 28) / 1000})
        expect(escaped, `${width}/${nativeUiScale}: reverse ink covered`).toEqual([])
        expect(labels).toEqual(width >= 350 ? month === 9 ? ['班','休','休','休'] : ['休','休','休','休','休','班'] : [])
        expect(dots.map(dot => dot.day)).toEqual(width < 350 ? month === 9 ? [20,25,26,27] : [1,2,3,4,5,9] : [])
        for (const dot of dots) {
          expect(dot.size).toBeGreaterThanOrEqual(2)
          if (v1LayoutPresetForSize(width,height).calendarLunarCells) {
            expect(dot.x).toBeGreaterThan(dot.numeral.x)
            expect(dot.y).toBe(dot.numeral.y - 6)
          } else {
            expect(Math.abs(dot.x + (dot.width - 1) / 2 - dot.numeral.x - dot.numeral.width / 2)).toBeLessThanOrEqual(1)
            expect(dot.y).toBe(dot.numeral.y + (width === 212 && month === 5 ? 1 : 2))
          }
        }
      }
    } finally { textSpy.mockRestore(); dotSpy.mockRestore(); rectSpy.mockRestore(); pixelSpy.mockRestore() }
  })

  it('device information fits the advertised BLE prefix and MCU, never a removed PCB product label', () => {
    for(const [width,height] of screens)for(const nativeUiScale of scales)for(const prefix of ['GR','OM','TL','NRF','DA']) {
      const scene:NativeScene={parts:[]},name=`IKT_${prefix}_3A57`
      renderFirmwareNativePage({width,height,nativeUiScale,page:'device-info',layoutRevision:'v1',bluetoothName:name,mcuName:'TEST_MCU',boardName:'retired-product',scene})
      const ble=scene.parts.find(p=>p.element.key==='info-value-BLE:0')!
      const texts=ble.commands.filter(c=>c.type==='text')
      expect(texts).toHaveLength(1)
      const command=texts[0]!
      expect(command.text.length).toBeGreaterThan(0)
      expect(v1LayoutPresetForSize(width,height).family === 'compact-square'
        ? command.text === '3A57' : name.startsWith(command.text)).toBe(true)
      expect(command.text).not.toMatch(/[\[\]]/)
      expect(command.foreground).toBe(1)
      expect(scene.parts.some(p=>p.element.key==='info-value-MCU:0')).toBe(true)
      expect(scene.parts.some(p=>p.commands.some(c=>c.type==='text'&&c.text.includes('retired-product')))).toBe(false)
    }
  })

  it('swaps compact-rect weekday and BLE without losing the existing footer lunar detail', () => {
    const print = DevicePixelGfx.prototype.print
    const calls: {text:string;x:number;y:number;width:number}[] = []
    const spy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text:string) {
      calls.push({text,x:this.tx,y:this.ty,width:this.getUTF8Width(text)}); return print.call(this,text)
    })
    try {
      for (const [width,height] of screens.filter(([width,height])=>width<350 && width!==height)) for(const page of ['calendar','clock'] as const) {
        calls.length = 0
        const frame=renderFirmwareNativePage({width,height,page,layoutRevision:'v1'})
        const ble=calls.find(call=>call.text.startsWith('[') && call.text.includes('3A57'))!
        expect(ble.x).toBe(3); expect(ble.y).toBe(height-4)
        expect(ble.text).toBe('[3A57]')
        const weekday=calls.find(call=>call.text==='星期')!
        expect(weekday.y).toBe(page==='clock'?13:height-(width===296?24:22))
        const lunar=calls.filter(call=>call.y===height-4 && !call.text.startsWith('['))
        expect(lunar.map(call=>call.text).join('')).toBe(width===212?'八月十五':'丙午马年八月十五')
        expect(lunar[0]!.x-ble.x-ble.width).toBe(4)
        expect(lunar[0]!.x).toBeGreaterThanOrEqual(37)
        expect(lunar.at(-1)!.x+lunar.at(-1)!.width).toBeLessThanOrEqual(width-66)
        if (width>=250) {
          const year=calls.find(call=>call.text==='年' && call.y===height-4)!, month=calls.find(call=>call.text==='八' && call.y===height-4)!
          expect(month, JSON.stringify(calls)).toBeDefined()
          expect(month.x-year.x-year.width).toBe(4)
        }
        writeQaFrame(frame,`${width}-${page}-swapped-footer`)
      }
    } finally { spy.mockRestore() }
  })

  it('preserves two-pixel screen safety padding and continuous compact header rules', () => {
    for (const [width, height] of screens) for (const page of ['calendar', 'clock', 'device-info'] as const) {
      const frame = renderFirmwareNativePage({width, height, page, layoutRevision: 'v1', nativeUiScale: width === 600 ? 4 : width > 500 ? 2 : 1})
      for (const y of [0, 1, height - 2, height - 1]) {
        expect(Array.from(frame.pixels.slice(y * width, (y + 1) * width)).every(pixel => pixel === 0), `${width}/${page}: safe row ${y}`).toBe(true)
      }
      if (width <= 250 && !(page === 'calendar' && width !== height)) {
        const largerText=[200,240].includes(width)&&width===height&&page!=='device-info'
        const header=v1PowerStatusFrame({width,height,nativeUiScale:1,page}).height,footer=largerText&&width===200 ? 23:18
        expect(Array.from(frame.pixels.slice((header-1) * width + 3, header * width - 3)).every(pixel => pixel === 1), `${width}/${page}: full header rule`).toBe(true)
        for (const y of largerText ? [header-2,height-footer+1,height-footer+2]:[15,16,height-17,height-16]) {
          expect(Array.from(frame.pixels.slice(y * width, (y + 1) * width)).every(pixel => pixel === 0), `${width}/${page}: bar padding ${y}`).toBe(true)
        }
      }
      writeQaFrame(frame,`${width}-${page}`)
    }
  })

  it('shares the wide date, power, countdown and footer while retaining calendar lunar spacing', () => {
    for (const [width, height] of screens.filter(([width]) => width >= 400)) for (const nativeUiScale of scales) {
      const calendar = renderFirmwareNativePage({width, height, page: 'calendar', nativeUiScale, layoutRevision: 'v1'})
      const clock = renderFirmwareNativePage({width, height, page: 'clock', nativeUiScale, layoutRevision: 'v1'})
      const header = v1PowerStatusFrame({width,height,nativeUiScale}).height + 26
      const footer = width > 500 ? 31 : 21
      const status = v1PowerStatusFrame({width,height,nativeUiScale})
      // Current C intentionally gives the calendar lunar line two-space separators.
      // Compare every other header pixel, including the unchanged countdown.
      if(width!==400 || height!==300) expect(calendar.pixels.slice(0, (header - 2) * width).every((pixel, i) =>
        Math.trunc(i/width) >= status.height && i%width < status.x || pixel === clock.pixels[i]), `${width}/${nativeUiScale}: header`).toBe(true)
      expect(calendar.pixels.slice((height - footer) * width).every((pixel, i) => pixel === clock.pixels[(height - footer) * width + i]), `${width}/${nativeUiScale}: footer`).toBe(true)
    }
  })

  it('keeps compact metadata native, weekdays in their designated region and battery on the text line', () => {
    const calls: {text: string; x: number; y: number; width: number; scale: number}[] = []
    const batteries: {x: number; y: number; height: number}[] = []
    const print = DevicePixelGfx.prototype.print, rect = DevicePixelGfx.prototype.drawRect
    const printSpy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      calls.push({text, x: this.tx, y: this.ty, width: this.getUTF8Width(text), scale: this.fontScaleQ8})
      return print.call(this, text)
    })
    const rectSpy = vi.spyOn(DevicePixelGfx.prototype, 'drawRect').mockImplementation(function(this: DevicePixelGfx, x, y, width, height, color) {
      if ((width === 12 || width === 16) && height === 8) batteries.push({x, y, height})
      return rect.call(this, x, y, width, height, color)
    })
    try {
      for (const [width, height] of screens.filter(([width]) => width < 350)) {
        for (const page of ['calendar', 'clock', 'device-info'] as const) {
          calls.length = 0; batteries.length = 0
          renderFirmwareNativePage({width, height, page, layoutRevision: 'v1', temperature: -24, voltage: 3300})
          const status = v1PowerStatusFrame({width, height, nativeUiScale: 1})
          expect(status.height).toBe(18)
          const temperature = calls.find(call => call.text === '-24℃')!
          if([200,240].includes(width)&&width===height&&page!=='device-info') {
            const header=width===240 ? 27:26
            expect(v1PowerStatusFrame({width,height,nativeUiScale:1,page}).height).toBe(header)
            expect(temperature.scale).toBe(256);expect(temperature.y).toBe(width===240 ? 19:20)
            expect(calls.find(call=>call.text==='星期' && call.y<header)).toBeUndefined()
            expect(calls.some(call=>call.text.includes('V'))).toBe(true)
            expect(calls.find(call=>call.text==='[3A57]')!.scale).toBe(256)
            continue
          }
          expect(temperature.scale).toBe(256)
          expect(temperature.y).toBe(13)
          expect(batteries.at(-1)?.y).toBe(4)
          if (page !== 'device-info') {
            const weekday = calls.find(call => call.text.startsWith('星期'))
            if (width===height && page==='calendar') expect(weekday).toBeUndefined()
            else {
              expect(weekday).toBeDefined()
              if(width!==height && page==='clock') expect(weekday!.y).toBe(13)
              else {
                expect(weekday!.y).toBeGreaterThanOrEqual(height - (width===height ? 34 : 40))
                expect(weekday!.y).toBeLessThan(height-18)
              }
              expect(weekday!.scale).toBe(256)
            }
            if(width===height || page==='calendar') expect(calls.filter(call => call.y <= 16).every(call => !/星期|丙午/.test(call.text))).toBe(true)
          }
          if (page === 'device-info') {
            expect(calls.every(call => call.scale === 256), `${width}: native info`).toBe(true)
            const device = calls.find(call => call.text === 'DEVICE')!, info = calls.find(call => call.text === 'INFO')!
            expect(info.x - device.x - device.width).toBe(2)
            const keys = calls.filter(call => ['NAME', 'FW', 'BLE', 'PCB', 'EPD', 'TIER'].includes(call.text))
            expect(new Set(keys.map(key => key.x + key.width)).size).toBeLessThanOrEqual(1)
            for (const key of calls.filter(call => ['NAME', 'FW', 'BLE', 'PCB', 'EPD', 'TIER'].includes(call.text))) {
              const value = calls.find(call => call.y === key.y && call.x > key.x)!
              expect(value.x - key.x - key.width, `${width}/${key.text}: key/value gap`).toBeGreaterThanOrEqual(8)
              if (key.text === 'BLE') expect(value.text).not.toMatch(/[\[\]]/)
            }
          }
        }
      }
    } finally { printSpy.mockRestore(); rectSpy.mockRestore() }
  })

  it('keeps QR sections within half the screen and fills at least 80% with integer modules', () => {
    for (const [width, height] of screens) {
      const header = v1PowerStatusFrame({width, height, nativeUiScale: 1}).height + 1
      const qr = v1DeviceInfoQrGeometry(width, height, header, height - header - 28)
      expect(qr.qrSectionWidth).toBeLessThanOrEqual(width / 2)
      expect(qr.qrSide / qr.qrSectionWidth).toBeGreaterThanOrEqual(0.8)
      expect(Number.isInteger(qr.qrScale)).toBe(true)
      expect(qr.qrSide).toBe((qr.qr.size + 8) * qr.qrScale)
      expect(qr.qrX).toBeGreaterThanOrEqual(0)
    }
    expect(v1DeviceInfoQrGeometry(200, 200, 34, 140).qrSide).toBe(99)
    expect(v1DeviceInfoQrGeometry(152, 152, 26, 106).qrSide).toBe(66)
    expect(v1DeviceInfoQrGeometry(212,104,18,68).qrSide).toBe(66)
    expect(v1DeviceInfoQrGeometry(250,128,18,92).qrSide).toBe(66)
    expect(v1DeviceInfoQrGeometry(400,300,25,254).qrSectionWidth).toBe(198)
  })

  it('never chooses a font missing any displayed glyph', () => {
    const original = DevicePixelGfx.prototype.print
    const missing = new Set<string>()
    const spy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      if (!this.supportsText(text)) missing.add(text)
      return original.call(this, text)
    })
    try {
      for (const [width, height] of screens) for (const nativeUiScale of scales) {
        for (const page of ['calendar', 'clock', 'device-info'] as const) {
          renderFirmwareNativePage({width, height, page, nativeUiScale, layoutRevision: 'v1'})
        }
      }
      expect([...missing]).toEqual([])
    } finally { spy.mockRestore() }
  })

  it('retains lunar cells in six-row months on the 296 preset', () => {
    const original = DevicePixelGfx.prototype.print
    let lunarCells = 0
    const spy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      if (this.tx < 190 && this.ty > 16 && /[初十廿月]/.test(text)) lunarCells++
      return original.call(this, text)
    })
    try {
      renderFirmwareNativePage({width: 296, height: 152, page: 'calendar', layoutRevision: 'v1', timestamp: Date.parse('2026-08-31T12:00:00Z') / 1000})
      expect(lunarCells).toBeGreaterThan(25)
    } finally { spy.mockRestore() }
  })

  it('keeps compact lunar ink clear of numerals, dots and adjacent rows', () => {
    const print = DevicePixelGfx.prototype.print, pixel = DevicePixelGfx.prototype.drawPixel
    type Ink = {text: string; x: number; top: number; bottom: number}
    const calls: Ink[] = []
    let active: Ink | undefined
    const pixelSpy = vi.spyOn(DevicePixelGfx.prototype, 'drawPixel').mockImplementation(function(this: DevicePixelGfx, x, y, color) {
      if (active) { active.top = Math.min(active.top, y); active.bottom = Math.max(active.bottom, y) }
      return pixel.call(this, x, y, color)
    })
    const printSpy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text) {
      active = {text, x: this.tx, top: Infinity, bottom: -Infinity}
      const result = print.call(this, text)
      calls.push(active); active = undefined
      return result
    })
    try {
      for (const month of [8, 9]) for (const weekStart of [0, 1]) for (const nativeUiScale of scales) {
        calls.length = 0
        const frame=renderFirmwareNativePage({width: 296, height: 152, page: 'calendar', layoutRevision: 'v1', nativeUiScale, weekStart,
          timestamp: Date.UTC(2026, month - 1, 25, 14, 28) / 1000})
        const first = (new Date(Date.UTC(2026, month - 1, 1)).getUTCDay() - weekStart + 7) % 7
        const days = new Date(Date.UTC(2026, month, 0)).getUTCDate(), rows = Math.max(5, Math.ceil((first + days) / 7))
        const gridTop=rows===6?14:18, cellHeight=Math.trunc((132-(rows===6?12:16))/rows)
        for(const y of rows===6?[133]:[16,17,133]) expect(frame.pixels.slice(y*296+3,y*296+199).every(pixel=>pixel===0)).toBe(true)
        const grid = calls.filter(call => call.x < 196 && call.top >= gridTop && call.bottom < 134)
        expect(grid).toHaveLength(days * 2)
        for (let day = 1; day <= days; day++) {
          const numeral = grid[(day - 1) * 2]!, lunar = grid[(day - 1) * 2 + 1]!
          const row = Math.trunc((first + day - 1) / 7)
          expect(numeral.text).toBe(String(day))
          expect(numeral.top).toBeGreaterThanOrEqual(gridTop + row * cellHeight)
          expect(lunar.top - numeral.bottom).toBeGreaterThanOrEqual(2)
          expect(lunar.top - numeral.bottom).toBeLessThanOrEqual(3)
          expect(lunar.bottom).toBeLessThan(gridTop + (row + 1) * cellHeight)
        }
      }
    } finally { printSpy.mockRestore(); pixelSpy.mockRestore() }
  })

  it('keeps all 31 days at native WQY9 in six-row 2.13 calendars', () => {
    const print = DevicePixelGfx.prototype.print
    const cells: {text: string; scale: number}[] = []
    const spy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      if (this.ty >= 16 && this.ty < this.frame.height - 18 && this.tx < this.frame.width - 86 && /^\d+$/.test(text)) cells.push({text, scale: this.fontScaleQ8})
      return print.call(this, text)
    })
    try {
      for (const [width, height] of [[212, 104], [250, 128]]) {
        cells.length = 0
        renderFirmwareNativePage({width, height, page: 'calendar', layoutRevision: 'v1', timestamp: Date.parse('2026-08-31T12:00:00Z') / 1000})
        expect(cells).toHaveLength(31)
        expect(cells.every(cell => cell.scale === 256)).toBe(true)
      }
    } finally { spy.mockRestore() }
  })

  it('matches independently rendered C pixels at every selectable scale and week start', async () => {
    expect(golden.variants).toHaveLength(screens.length * 40)
    const mismatches: string[] = []
    for (const asset of golden.variants) {
      const frame = renderFirmwareNativePage({
        width: asset.width, height: asset.height,
        page: asset.page as 'calendar' | 'clock' | 'device-info' | 'low-battery',
        edition: asset.variant as 'base' | 'pro', nativeUiScale: scaleEnums[asset.scalePercent],
        weekStart: asset.weekStart, layoutRevision: 'v1',
      })
      const digest = await crypto.subtle.digest('SHA-256', Uint8Array.from(frame.pixels).buffer)
      const hash = Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, '0')).join('')
      if (hash !== asset.pixelSha256) {
        const prefix = asset.width === asset.height ? 'square' : asset.width === 400 ? 'normal' : asset.width > 500 ? 'large' : 'rect'
        const file = `${prefix}-${asset.width}-${asset.page}-${asset.scalePercent}-${asset.variant}${asset.page === 'calendar' ? `-week-${asset.weekStart}` : ''}.png`
        const referenceUrl = new URL(`../../public/layouts-generated/v1/${file}`, import.meta.url)
        if (!existsSync(referenceUrl)) {
          mismatches.push(`${file}: hash ${hash} != ${asset.pixelSha256}; regenerate C assets for pixel diagnostics`)
          continue
        }
        const reference = PNG.sync.read(readFileSync(referenceUrl))
        const rendered = new PNG({width: frame.width, height: frame.height})
        let left = frame.width, top = frame.height, right = 0, bottom = 0, count = 0
        const palette = [[255,255,255],[23,26,24],[200,45,40]]
        frame.pixels.forEach((pixel, index) => {
          const rgb = palette[pixel]!
          for (let channel = 0; channel < 3; channel++) rendered.data[index * 4 + channel] = rgb[channel]
          rendered.data[index * 4 + 3] = 255
          if (rgb.some((value, channel) => reference.data[index * 4 + channel] !== value)) {
            const x = index % frame.width, y = Math.trunc(index / frame.width)
            left = Math.min(left, x); top = Math.min(top, y); right = Math.max(right, x); bottom = Math.max(bottom, y); count++
          }
        })
        mkdirSync(new URL('../../../.local/layout-v1-candidate/', import.meta.url), {recursive: true})
        writeFileSync(new URL(`../../../.local/layout-v1-candidate/browser-${file}`, import.meta.url), PNG.sync.write(rendered))
        mismatches.push(`${file}: ${count}px [${left},${top}..${right},${bottom}]`)
      }
    }
    expect(mismatches, mismatches.join('\n')).toEqual([])
  })

  it('keeps the full clock year, unit spacing and shared power baseline at every scale', () => {
    const original = DevicePixelGfx.prototype.print
    const calls: {text: string; x: number; y: number; width: number; color: number}[] = []
    const spy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      calls.push({text, x: this.tx, y: this.ty, width: this.getUTF8Width(text), color: this.foreground})
      return original.call(this, text)
    })
    try {
      for (const [width, height] of screens) for (const scale of scales) {
        calls.length = 0
        renderFirmwareNativePage({width, height, page: 'clock', layoutRevision: 'v1', nativeUiScale: scale,
          timestamp: Date.parse('2026-12-31T12:00:00Z') / 1000, temperature: 24, voltage: 3300})
        const year = calls.find((call) => call.text === '2026')!
        const unit = calls.find((call) => call.y < 50 && call.text === '年')!
        const temperature = calls.find((call) => call.text === '24℃')!
        const status = v1PowerStatusFrame({width, height, nativeUiScale: scale})
        expect(year, `${width}/${scale}: year`).toBeDefined()
        if([200,240].includes(width)&&width===height) {
          const baseline=width===240 ? 19:20
          expect(year.y).toBe(baseline);expect(temperature.y).toBe(baseline)
          expect(unit.x-year.x-year.width).toBe(width===200 ? 3:1)
          const date=calls.filter(call=>call.y===baseline && call.x<temperature.x)
          expect(date.filter((call,i)=>i===0||call.text!==date[i-1]!.text).map(call=>call.text).join('')).toBe('2026年12月31日')
          for (const digits of ['2026','12','31']) {
            const pair=date.filter(call=>call.text===digits)
            expect(pair).toHaveLength(2)
            expect(pair[1]!.x).toBe(pair[0]!.x+1)
          }
          expect(Boolean(calls.find(call=>call.text==='3.3V'))).toBe(true)
          continue
        }
        expect(year.x + year.width).toBeLessThan(status.x)
        expect(unit.x - year.x - year.width).toBe(width>=400?2:1)
        expect(unit.color).toBe(1)
        expect(year.color).toBe(2)
        expect(temperature.y, `${width}/${scale}: baseline`).toBe(year.y)
        const lastDatePart = calls.filter((call) => call.y === year.y && call.x < status.x).at(-1)!
        expect(lastDatePart.x + lastDatePart.width, `${width}/${scale}: complete date`).toBeLessThanOrEqual(status.x - 2)
        const voltage = calls.find((call) => call.text === '3.3V')
        if (width >= 200 && width !== 212) {
          expect(voltage).toBeDefined()
          expect(voltage!.x - temperature.x - temperature.width).toBeGreaterThanOrEqual(width<=250?2:4)
          expect(voltage!.y).toBe(temperature.y)
        } else expect(voltage).toBeUndefined()
      }
    } finally { spy.mockRestore() }
  })

  it('fits the full Chinese date and native WQY9 on 152 even with a negative temperature', () => {
    const calls: {text: string; x: number; y: number; width: number; scale: number}[] = []
    const print = DevicePixelGfx.prototype.print
    const spy = vi.spyOn(DevicePixelGfx.prototype, 'print').mockImplementation(function(this: DevicePixelGfx, text: string) {
      calls.push({text, x: this.tx, y: this.ty, width: this.getUTF8Width(text), scale: this.fontScaleQ8})
      return print.call(this, text)
    })
    try {
      for (const temperature of [24, -24]) for (const page of ['calendar', 'clock'] as const) {
        calls.length = 0
        const frame = renderFirmwareNativePage({width: 152, height: 152, page, layoutRevision: 'v1', temperature,
          timestamp: Date.parse('2026-12-31T12:00:00Z') / 1000})
        const status = v1PowerStatusFrame({width: 152, height: 152, nativeUiScale: 1, temperature})
        const date = calls.filter(call => call.y === status.height - 5 && call.x < status.x)
        expect(date.map(call => call.text).join('')).toBe('2026年12月31日')
        expect(date.every(call => call.scale === 256)).toBe(true)
        expect(date.at(-1)!.x + date.at(-1)!.width).toBeLessThanOrEqual(status.x - 2)
        if (process.env.INKTAG_LAYOUT_QA === '1') {
          const png = new PNG({width: 152, height: 152})
          const palette = [[255,255,255],[23,26,24],[200,45,40]]
          frame.pixels.forEach((pixel, i) => {
            palette[pixel]!.forEach((v, c) => { png.data[i * 4 + c] = v })
            png.data[i * 4 + 3] = 255
          })
          mkdirSync(new URL('../../../.local/layout-v1-candidate/', import.meta.url), {recursive: true})
          writeFileSync(new URL(`../../../.local/layout-v1-candidate/qa-152-${page}-${temperature}.png`, import.meta.url), PNG.sync.write(png))
        }
      }
    } finally { spy.mockRestore() }
  })

  it('fits complete Chinese dates and weekdays on one native compact-rect clock row', () => {
    const print=DevicePixelGfx.prototype.print
    const calls:{text:string,x:number,y:number,width:number,scale:number,color:number}[]=[]
    const spy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text){
      calls.push({text,x:this.tx,y:this.ty,width:this.getUTF8Width(text),scale:this.fontScaleQ8,color:this.foreground})
      return print.call(this,text)
    })
    try {
      for(const [width,height] of [[212,104],[250,128],[296,152]]) for(const month of [9,10,12]) for(const temperature of [-128,-127,-24,-9,0,24,127]) {
        calls.length=0
        const frame=renderFirmwareNativePage({width,height,page:'clock',layoutRevision:'v1',temperature,
          timestamp:Date.UTC(2026,month-1,25,14,28)/1000})
        const status=v1PowerStatusFrame({width,height,temperature,nativeUiScale:1})
        const header=calls.filter(call=>call.y===13 && call.x<status.x && !/[℃V]/.test(call.text))
        expect(header.slice(0,6).map(call=>call.text).join('')).toBe(`2026年${month}月25日`)
        expect(header[6]!.text).toBe('星期')
        expect(header[7]!.color).toBe(2)
        expect(header.every(call=>call.scale===256)).toBe(true)
        expect(header[6]!.x-header[5]!.x-header[5]!.width).toBe(6)
        expect(status.x-header.at(-1)!.x-header.at(-1)!.width).toBeGreaterThanOrEqual(2)
        expect(calls.some(call=>/^\d\.\dV$/.test(call.text))).toBe(width!==212)
        writeQaFrame(frame,`${width}-clock-month-${month}-temperature-${temperature}`)
      }
    } finally {spy.mockRestore()}
  })

  it('samples scaled glyphs without the legacy floor/ceil stroke dilation', () => {
    const draw = (scale: number, mode: 'legacy' | 'nearest') => {
      const gfx = new DevicePixelGfx(180, 80)
      gfx.fontRasterMode = mode
      gfx.setFont('wqy9-lunar'); gfx.setFontScale(scale)
      gfx.setCursor(4, 40); gfx.print('2026年9月')
      return {width: gfx.tx - 4, ink: gfx.frame.pixels.filter((pixel) => pixel !== 0).length}
    }
    const normal = draw(256, 'nearest'), large = draw(337, 'nearest'), dilated = draw(337, 'legacy')
    expect(large.width).toBeGreaterThan(normal.width)
    expect(large.ink).toBeGreaterThan(normal.ink)
    expect(large.ink).toBeLessThan(dilated.ink)
  })

  it('uses the same two-pixel edition mark on every large-screen page', () => {
    for (const [width, height] of screens.filter(([width]) => width > 500)) {
      for (const page of ['calendar', 'clock', 'device-info'] as const) {
        const frame = renderFirmwareNativePage({width, height, page, layoutRevision: 'v1', nativeUiScale: 5})
        const points: [number, number][] = []
        for (let y = height - 60; y < height; y++) for (let x = width - 160; x < width; x++) {
          if (frame.pixels[y * width + x] === 2) points.push([x, y])
        }
        expect(Math.max(...points.map(([x]) => x)) - Math.min(...points.map(([x]) => x)) + 1, `${width}/${page}: width`).toBe(71)
        expect(Math.max(...points.map(([, y]) => y)) - Math.min(...points.map(([, y]) => y)) + 1).toBe(20)
      }
    }
  })

  it('does not emit red pixels on monochrome screens', () => {
    for (const [width, height] of screens) for (const page of ['calendar', 'clock', 'device-info'] as const) {
      const frame = renderFirmwareNativePage({width, height, page, colorCount: 1, layoutRevision: 'v1'})
      expect(frame.pixels.some((pixel) => pixel > 1), `${width}/${page}`).toBe(false)
    }
  })
})
