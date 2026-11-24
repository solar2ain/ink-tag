import { describe, expect, it, vi } from 'vitest'
import { readFileSync } from 'node:fs'
import { createContext, runInContext } from 'node:vm'
import { createHash } from 'node:crypto'
import { renderFirmwareNativePage, v1CalendarColumnBounds, v1PowerStatusFrame } from './nativePageRenderer'
import { DevicePixelGfx } from './devicePixelGfx'

const context = createContext({})
const asset = (name: string) => readFileSync(new URL(`../../../cli/src/inktag_cli/_assets/${name}`, import.meta.url), 'utf8')
for (const name of ['nativePreviewWasm.js', 'nativePreview.js']) runInContext(asset(name), context)
const api = (context as unknown as {InkTagNativePreview:{create():{render(screen:unknown,page:string,unused:undefined,labels:number[][]):{pixels:number[]}}}}).InkTagNativePreview.create()
const digest = (pixels:ArrayLike<number>) => createHash('sha256').update(Uint8Array.from(pixels)).digest('hex')
const labels = ['InkTag','IKT_TL_CA15','tlsr8359_hsel','TLSR8359','BWR'].map(t=>Array.from(new TextEncoder().encode(t)))
const dates = [
  // Monday start makes November six rows. Sunday start makes May six rows,
  // and May 9 is a workday: exercise the dot and selected-day background too.
  {month:11,day:30,weekStart:1,rows:6},
  {month:5,day:9,weekStart:0,rows:6},
  {month:10,day:1,weekStart:1,rows:5},
  {month:10,day:10,weekStart:1,rows:5},
]

describe('calendar production C/WASM and browser geometry',()=>{
  it('retains the original bold header fonts and reserves the actual temperature edge',()=>{
    const glyph=DevicePixelGfx.prototype.drawGlyph,pixel=DevicePixelGfx.prototype.drawPixel
    const print=DevicePixelGfx.prototype.print
    const runs:{char:string;left:number;right:number;origin:number;overprinted?:boolean;font:string}[]=[]
    let active:typeof runs[number]|undefined
    let inTemperature=false,temperatureLeft=Infinity
    const prints=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text){
      const previous=inTemperature
      inTemperature=text==='24℃'
      const result=print.call(this,text)
      inTemperature=previous
      return result
    })
    const glyphs=vi.spyOn(DevicePixelGfx.prototype,'drawGlyph').mockImplementation(function(this:DevicePixelGfx,x,y,encoding){
      const status=v1PowerStatusFrame({width:this.width,height:this.height,page:'calendar',nativeUiScale:0,temperature:24})
      const previous=active
      const dateGlyph=(encoding>=48&&encoding<=57)?this.foreground===2:[0x5e74,0x6708,0x65e5].includes(encoding)
      active=dateGlyph&&(this.width===152||this.width===200||this.width===240)&&y<status.height&&x<status.x-2
        ? {char:String.fromCodePoint(encoding),left:Infinity,right:-Infinity,origin:x,font:this.currentFontId}:undefined
      const result=glyph.call(this,x,y,encoding)
      if(active&&active.right>=active.left)runs.push(active)
      active=previous
      return result
    })
    const pixels=vi.spyOn(DevicePixelGfx.prototype,'drawPixel').mockImplementation(function(this:DevicePixelGfx,x,y,color){
      if(active&&color===this.foreground){active.left=Math.min(active.left,x);active.right=Math.max(active.right,x)}
      if(inTemperature&&color===this.foreground)temperatureLeft=Math.min(temperatureLeft,x)
      return pixel.call(this,x,y,color)
    })
    try{
      for(const width of [152,200,240]){
        runs.length=0
        temperatureLeft=Infinity
        renderFirmwareNativePage({width,height:width,page:'calendar',temperature:24,voltage:3000,
          timestamp:Date.UTC(2026,10,30,12)/1000})
        const merged:typeof runs=[]
        for(const run of runs){
          const base=merged.find(r=>!r.overprinted&&r.char===run.char&&r.origin+1===run.origin)
          if(base){base.left=Math.min(base.left,run.left);base.right=Math.max(base.right,run.right);base.overprinted=true}
          else merged.push({...run})
        }
        expect(merged.map(r=>r.char).join('')).toBe('2026年11月30日')
        const digits=merged.filter(r=>/^\d$/.test(r.char))
        expect(digits.every(r=>r.font===(width===152?'wqy9-lunar':'wqy12-lunar'))).toBe(true)
        expect(digits.every(r=>!!r.overprinted===(width!==152))).toBe(true)
        if(width===200)for(const [a,b] of [[3,4],[4,5],[6,7],[7,8],[9,10]])
          expect(merged[b].left-merged[a].right-1,`${width}: date group gap`).toBeGreaterThanOrEqual(2)
        if(width===152)expect(merged[7].left-merged[6].right-1,'152: month unit gap').toBe(1)
        expect(temperatureLeft-merged[10].right-1,`${width}: date and power gap`).toBeGreaterThanOrEqual(2)

      }
    }finally{glyphs.mockRestore();pixels.mockRestore();prints.mockRestore()}
  })
  it('distributes every remainder pixel symmetrically across seven contiguous columns',()=>{
    for(let width=14;width<=1200;width++){
      const columns=Array.from({length:7},(_,i)=>v1CalendarColumnBounds(width,i))
      const widths=columns.map(c=>c.width)
      expect(columns[0].x).toBe(0)
      expect(columns[6].x+columns[6].width).toBe(width)
      expect(Math.max(...widths)-Math.min(...widths)).toBeLessThanOrEqual(1)
      expect(widths).toEqual([...widths].reverse())
      for(let i=1;i<7;i++)expect(columns[i].x).toBe(columns[i-1].x+columns[i-1].width)
    }
  })

  it('moves 240/296 holiday dots one pixel further inside their own columns',()=>{
    const rounded=DevicePixelGfx.prototype.fillRoundRect
    const dots:{x:number;width:number}[]=[]
    const spy=vi.spyOn(DevicePixelGfx.prototype,'fillRoundRect').mockImplementation(function(this:DevicePixelGfx,x,y,width,height,radius,color){
      if(width===4&&height===3)dots.push({x,width})
      return rounded.call(this,x,y,width,height,radius,color)
    })
    try{
      for(const [width,height] of [[240,240],[296,152]])for(const fixture of dates.filter(d=>d.month===5||d.month===10)){
        dots.length=0
        renderFirmwareNativePage({width,height,page:'calendar',weekStart:fixture.weekStart,
          timestamp:Date.UTC(2026,fixture.month-1,fixture.day,12)/1000})
        expect(dots.length).toBeGreaterThan(0)
        const gridX=width===240?2:3,gridWidth=width===240?236:196
        for(const dot of dots){
          const column=Array.from({length:7},(_,i)=>v1CalendarColumnBounds(gridWidth,i)).find(c=>dot.x>=gridX+c.x&&dot.x<gridX+c.x+c.width)!
          expect(gridX+column.x+column.width-dot.x-dot.width).toBe(3)
        }
      }
    }finally{spy.mockRestore()}
  })

  it('preserves fitted numeral strokes after lunar text and holiday markers have been drawn',()=>{
    const print=DevicePixelGfx.prototype.print
    const runs:{text:string;x:number;y:number;color:number;font:'wqy9-lunar'|'wqy12-lunar'}[]=[]
    const spy=vi.spyOn(DevicePixelGfx.prototype,'print').mockImplementation(function(this:DevicePixelGfx,text){
      const inGrid=this.width===200?this.ty>44&&this.ty<177:this.width===296&&this.tx<196&&this.ty>13&&this.ty<134
      if(inGrid&&/^\d{1,2}$/.test(text)) {
        expect(this.fontScaleQ8).toBe(this.width===200?240:256)
        runs.push({text,x:this.tx,y:this.ty,color:this.foreground,font:this.width===200?'wqy12-lunar':'wqy9-lunar'})
      }
      return print.call(this,text)
    })
    try {
      for(const [width,height] of [[200,200],[296,152]])for(const fixture of dates){
        runs.length=0
        const frame=renderFirmwareNativePage({width,height,page:'calendar',weekStart:fixture.weekStart,edition:'pro',
          timestamp:Date.UTC(2026,fixture.month-1,fixture.day,12)/1000})
        expect(runs).toHaveLength(new Date(Date.UTC(2026,fixture.month,0)).getUTCDate())
        for(const run of runs){
          let advance=0
          for(const char of run.text){
            const native=new DevicePixelGfx(40,32)
            native.fontRasterMode='nearest'
            native.setFont(run.font);native.setFontScale(256);native.setTextColor(1,0);native.setFontMode(1);native.setCursor(2,16)
            print.call(native,char)
            const scale=width===200?240:256, round=(n:number)=>Math.floor(n*scale/256+0.5)
            for(let row=0;row<32;row++)for(let column=0;column<40;){
              if(native.frame.pixels[row*40+column]!==1){column++;continue}
              const start=column
              while(column<40&&native.frame.pixels[row*40+column]===1)column++
              const left=round(start-2),top=round(row-16)
              for(let yy=top;yy<Math.max(top+1,round(row-15));yy++)
                for(let xx=left;xx<Math.max(left+1,round(column-2));xx++)
                  expect(frame.pixels[(run.y+yy)*width+run.x+advance+xx],`${width}/${run.text}/${char}: stroke at ${run.x+advance+xx},${run.y+yy}; origin ${run.x},${run.y}; advance ${advance}`).toBe(run.color)
            }
            advance+=Math.trunc(native.getUTF8Width(char)*scale/256)
          }

        }
      }
    }finally{spy.mockRestore()}
  })
  for(const [width,height] of [[152,152],[200,200],[240,240],[296,152],[400,300]]) {
    for(const fixture of dates) it(`${width}x${height} month ${fixture.month}/${fixture.day}, ${fixture.rows} rows`,()=>{
      const first=(new Date(Date.UTC(2026,fixture.month-1,1)).getUTCDay()-fixture.weekStart+7)%7
      expect(Math.max(5,Math.ceil((first+new Date(Date.UTC(2026,fixture.month,0)).getUTCDate())/7))).toBe(fixture.rows)
      for(const temperature of [-128,24]) for(const nativeUiScale of [0,3]) for(const color of [1,2]) {
        const screen={width,height,color,temperature,voltage:3000,pro:true,nativeUiScale,weekStart:fixture.weekStart,
          timestamp:Date.UTC(2026,fixture.month-1,fixture.day,12)/1000}
        const firmware=api.render(screen,'calendar',undefined,labels)
        const browser=renderFirmwareNativePage({...screen,page:'calendar',edition:'pro',colorCount:color,
          bluetoothName:'IKT_TL_CA15',boardName:'tlsr8359_hsel',screenInfo:'BWR'})
        expect(digest(browser.pixels),JSON.stringify(screen)).toBe(digest(firmware.pixels))
      }
    })
  }
})
