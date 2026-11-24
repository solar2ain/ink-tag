import type { Widget, TextRun } from '../../../../packages/inktag-editor-core/src/index'
import { isNativeTextOverprint, type NativeScenePart, type NativeTextCommand } from '../../device-gui/nativeScene'
import { DevicePixelGfx } from '../../device-gui/devicePixelGfx'
import { resolveLabText } from './richTextRenderer'

/** One-time recipe import. The resulting text has no native page dependency. */
export function migrateText(w: Widget, part: NativeScenePart): Widget {
  const commands=part.commands.filter((c):c is NativeTextCommand=>c.type==='text')
  if(!commands.length || part.commands.some(c=>c.type!=='text')) return w
  const result:Widget={...w,kind:'text',native:undefined,sample:{...w.native?.sample,bluetoothName:'GR_3A57'},style:{...w.style,align:'left',fontId:'wqy9-lunar'}}
  const key=part.element.key.split(':')[0], gfx=new DevicePixelGfx(1,1)
  const value=(id:string)=>resolveLabText(result,`{{${id}}}`)
  const token=(id:string)=>`{{${id}}}`
  let previousEnd=commands[0].x
  const runs:TextRun[]=[]
  for(let i=0;i<commands.length;i++) {
    const c=commands[i], previous=commands[i-1]
    if(isNativeTextOverprint(c,previous)) {
      runs[runs.length-1].style={...runs[runs.length-1].style,bold:true}; continue
    }
    let text=c.text
    if(key==='temperature') text=token('temperature')
    else if(key==='voltage') text=token('voltage')
    else if(key==='bluetooth') {
      const name=text.replace(/^\[/,'').replace(/\]$/,'')
      result.sample={...result.sample,bluetoothName:name}
      text=text.startsWith('[')?`[${token('bluetoothName')}]`:token('bluetoothName')
    } else if(key==='countdown') {
      if(text===value('countdownName')) text=token('countdownName')
      else if(/^\d+$/.test(text)) text=token('countdownDays')
    } else if(key==='week-number' && /^\d+$/.test(text)) text=token('week')
    else {
      const next=isNativeTextOverprint(commands[i+1]??c,c)?commands[i+2]?.text:commands[i+1]?.text
      if(/^\d+$/.test(text) && ['date','day'].includes(key)) {
        const id=next==='年'?'year':next==='月'?'month':next==='日'||key==='day'?'day':undefined
        if(id) text=token(id)
      }
      if(['weekday','lunar-year','lunar-date','date'].includes(key)) {
        for(const id of ['lunarYearName','lunarZodiac','lunarDay','lunarMonth']) if(text===value(id)) { text=token(id); break }
        if(c.text===value('weekdayName') && (previous?.text==='星期'||key==='weekday')) text=token('weekdayName')
      }
    }
    gfx.setFont(c.font);gfx.setFontScale(c.scale)
    const gap=c.x-previousEnd
    runs.push({text,fontId:c.font,scale:c.scale,raster:c.raster,gap,baselineShift:c.y-commands[0].y,
      style:{color:c.foreground===2?'#dc3026':'#000000',fontSize:Math.round((c.font==='wqy9-lunar'?12:c.font==='wqy12-lunar'?16:c.font.includes('18')?24:20)*c.scale/256),bold:false}})
    previousEnd=c.x+gfx.getUTF8Width(c.text)
  }
  result.cells=[[{text:runs.map(r=>r.text).join(''),richText:{runs,padding:0,verticalAlign:'middle'}}]]
  return result
}
