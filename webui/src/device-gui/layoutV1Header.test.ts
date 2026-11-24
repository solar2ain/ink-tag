import { describe, expect, it, vi } from 'vitest'
import { DevicePixelGfx } from './devicePixelGfx'
import { renderFirmwareNativePage, resolveV1Countdown, v1PowerStatusFrame } from './nativePageRenderer'
import { firmwareIsoWeek } from './lunarFirmware'
import golden from './layoutV1CandidateGolden.generated.json'

const wide = [[400,300],[600,448],[752,568],[800,480]] as const
const stamp = (date: string) => Date.parse(date + 'Z') / 1000
type TextRun = {text: string; font: string; scale: number; color: number; x: number; y: number; width: number; left: number; right: number; top: number; bottom: number}
function capture(options: Parameters<typeof renderFirmwareNativePage>[0]) {
  const runs: TextRun[] = []
  const print = DevicePixelGfx.prototype.print, pixel = DevicePixelGfx.prototype.drawPixel
  let active: TextRun | undefined
  const pixels = vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this: DevicePixelGfx,x,y,color) {
    if (active && color) {
      active.left = Math.min(active.left,x); active.right = Math.max(active.right,x)
      active.top = Math.min(active.top,y); active.bottom = Math.max(active.bottom,y)
    }
    return pixel.call(this,x,y,color)
  })
  const text = vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this: DevicePixelGfx,value) {
    active = {text:value,font:(this as unknown as {fontId:string}).fontId,scale:this.fontScaleQ8,color:this.foreground,
      x:this.tx,y:this.ty,width:this.getUTF8Width(value),left:Infinity,right:-Infinity,top:Infinity,bottom:-Infinity}
    const result = print.call(this,value); runs.push(active); active = undefined
    return result
  })
  try { return {frame:renderFirmwareNativePage({...options,layoutRevision:'v1'}),runs} }
  finally { text.mockRestore(); pixels.mockRestore() }
}

describe('V1 wide header and configurable countdown', () => {
  it.each([[152,152],[200,200],[240,240],...wide] as const)('shares calendar/clock header pixels at %ix%i', (width,height) => {
    for (const nativeUiScale of [1,2,3,4,5]) for (const temperature of [-24,24,99]) {
      const options={width,height,nativeUiScale,temperature,timestamp:stamp('2026-12-31T14:28:00')}
      const calendar=renderFirmwareNativePage({...options,page:'calendar'})
      const clock=renderFirmwareNativePage({...options,page:'clock'})
      const power=v1PowerStatusFrame({...options,page:'calendar'})
      expect(v1PowerStatusFrame({...options,page:'clock'})).toEqual(power)
      // Calendar's weekday strip follows the shared wide header; only clock
      // draws the final horizontal rule, so compare the rows above that rule.
      const rows=width<350?power.height:power.height+(width===400&&height===300?21:25)
      expect(clock.pixels.slice(0,width*rows),`${width}/${nativeUiScale}/${temperature}`)
        .toEqual(calendar.pixels.slice(0,width*rows))
    }
  })

  it('keeps double-digit month/day ink apart and clear of the week and power region', () => {
    for (const [width,height] of wide) for (const nativeUiScale of [1,4,2,5,3]) for (const month of [9,10,12]) {
      const timestamp=Date.UTC(2026,month-1,month===9?30:31,14,28)/1000
      const {runs}=capture({width,height,nativeUiScale,page:'calendar',timestamp})
      const power=v1PowerStatusFrame({width,height,nativeUiScale})
      const header=runs.filter(run=>run.x<power.x && run.y<power.height)
      expect(header.map(run=>run.text)).toEqual(['2026','年',String(month),'月',month===9?'30':'31','日','[',String(firmwareIsoWeek(2026,month,month===9?30:31)),'周',']'])
      expect(new Set(header.map(run=>run.y)).size).toBe(1)
      expect(header.at(-1)!.right).toBeLessThan(power.x-8)
      for (let i=1;i<6;i++) expect(header[i]!.left-header[i-1]!.right-1,`${width}/${month}/${i}: ink gap`).toBeGreaterThanOrEqual(2)
      for (const i of [0,2,4]) {
        expect(header[i]!.font).toBe(width===400?'helv-bold-14-num':'helv-bold-18-num')
        expect(header[i]!.color).toBe(2); expect(header[i]!.scale).toBe(256)
        expect(header[i]!.bottom-header[i]!.top).toBeGreaterThan(header[i+1]!.bottom-header[i+1]!.top)
      }
      expect(header[7]!.color).toBe(2); expect(header[8]!.color).toBe(1)
      expect(header[7]!.font).toBe(width===400?'wqy9-lunar':'helv-bold-14-num')
      const closingGap=header[9]!.left-header[8]!.right-1
      expect(closingGap).toBeGreaterThanOrEqual(1)
      expect(closingGap).toBeLessThanOrEqual(3)
    }
  })

  it('uses native regular WQY for the large weekday/lunar subrow at every requested scale', () => {
    for (const [width,height] of wide) for (const nativeUiScale of [1,4,2,5,3]) {
      const {runs}=capture({width,height,nativeUiScale,page:'clock'})
      const power=v1PowerStatusFrame({width,height,nativeUiScale})
      const lunar=runs.filter(run=>run.y>power.height && run.y<power.height+26 && run.x<power.x)
      expect(lunar.some(run=>run.text==='星期')).toBe(true)
      for (const run of lunar) { expect(run.scale).toBe(256); expect(run.font).toBe(width===400?'wqy9-lunar':'wqy12-lunar') }
    }
  })

  it('counts civil days, keeps today at zero and falls back on unset/expired/invalid targets', () => {
    const targetTimestamp=stamp('2026-10-01T00:00:00')
    expect(resolveV1Countdown(stamp('2026-09-25T23:59:59'),{name:' 国庆 ',targetTimestamp})).toEqual({name:'国庆',days:6,source:'custom'})
    expect(resolveV1Countdown(stamp('2026-10-01T23:59:59'),{name:'国庆',targetTimestamp})?.days).toBe(0)
    const after=stamp('2026-10-02T00:00:00')
    for (const target of [undefined,{name:'国庆',targetTimestamp},{name:' ',targetTimestamp:stamp('2027-01-01T00:00:00')},
      {name:'国庆',targetTimestamp:0},{name:'国庆',targetTimestamp:NaN},{name:'国庆',targetTimestamp:2**32}]) {
      expect(resolveV1Countdown(after,target)).toEqual(resolveV1Countdown(after))
      expect(resolveV1Countdown(after,target)?.source).toBe('solar-term')
    }
    expect(resolveV1Countdown(stamp('2028-02-28T23:59:59'),{name:'春分',targetTimestamp:stamp('2028-03-01T00:00:00')})?.days).toBe(2)
    expect(resolveV1Countdown(stamp('2026-12-31T23:59:59'))).toEqual({name:'小寒',days:5,source:'solar-term'})
  })

  it('emphasizes the target in red, leaves days black, and fits long names on UTF-8 boundaries', () => {
    for (const [width,height] of wide) for (const name of ['国庆','小寒'.repeat(16)]) {
      const {runs}=capture({width,height,page:'calendar',countdown:{name,targetTimestamp:stamp('2027-10-01T00:00:00')}})
      const index=runs.findIndex(run=>run.text==='离'), parts=runs.slice(index,index+4)
      expect(parts.map(run=>run.color)).toEqual([1,2,1,1])
      expect(parts[1]!.text.length).toBeGreaterThan(0)
      expect(name.startsWith(parts[1]!.text)).toBe(true)
      expect(parts[1]!.font).toBe('wqy12-lunar')
      expect(parts[1]!.bottom-parts[1]!.top).toBeGreaterThan(parts[2]!.bottom-parts[2]!.top)
      const power=v1PowerStatusFrame({width,height,nativeUiScale:1,page:'calendar'})
      expect(parts.every(run=>run.y===power.height+(width===400&&height===300?16:19))).toBe(true)
      // Custom glyph descenders leave at least three blank rows before the clock rule.
      // The default solar-term label leaves four; calendar's strip starts one row later.
      expect(power.height+(width===400&&height===300?22:25)-Math.max(...parts.map(run=>run.bottom))-1).toBeGreaterThanOrEqual(3)
      const left=runs.filter(run=>run.y>power.height && run.y<power.height+26 && run.x<parts[0]!.x)
      expect(Math.max(...left.map(run=>run.right))).toBeLessThan(parts[0]!.x-5)
    }
  })

  it('matches independent C renders for custom targets, cross-year ISO weeks and fallback dates', async () => {
    expect(golden.countdowns).toHaveLength(20)
    for (const item of golden.countdowns) {
      const frame=renderFirmwareNativePage({...item,page:'calendar',layoutRevision:'v1'})
      const digest=await crypto.subtle.digest('SHA-256',Uint8Array.from(frame.pixels).buffer)
      expect(Buffer.from(digest).toString('hex'),`${item.width}/${item.case}`).toBe(item.pixelSha256)
    }
  })

  it('shows a visible placeholder instead of silently losing unsupported label glyphs', () => {
    const {runs}=capture({width:400,height:300,page:'calendar',countdown:{name:'生日',targetTimestamp:stamp('2026-10-01T00:00:00')}})
    const index=runs.findIndex(run=>run.text==='离')
    expect(runs[index+1]!.text).toBe('?日')
  })

  it('leaves compact pages and device information unaffected by custom countdown data', () => {
    const countdown={name:'国庆',targetTimestamp:stamp('2026-10-01T00:00:00')}
    const unchanged=(options: Parameters<typeof renderFirmwareNativePage>[0]) => {
      const before=renderFirmwareNativePage(options), after=renderFirmwareNativePage({...options,countdown})
      expect(after.pixels.every((pixel,index)=>pixel===before.pixels[index])).toBe(true)
    }
    for (const [width,height] of [[152,152],[200,200],[212,104],[250,128],[296,152],[400,300],[600,448],[752,568],[800,480]]) {
      for (const page of ['calendar','clock','device-info'] as const) {
        if (width<350 || page==='device-info') {
          const options={width,height,page,layoutRevision:'v1' as const}
          unchanged(options)
        }
      }
    }
  })
})
