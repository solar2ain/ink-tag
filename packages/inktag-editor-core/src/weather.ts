/** Host weather bindings and drawing primitives. No network, BLE or browser dependency. */
import {drawSegmentNumber} from './segmentDigits'
export const weatherFields = ['location', 'date', 'day-number', 'weekday', 'lunar-date', 'temperature', 'feels-like', 'humidity', 'wind', 'summary', 'updated', 'icon', 'temperature-chart', 'rain-chart', 'forecast', 'attribution'] as const
export type WeatherField = typeof weatherFields[number]
export type WeatherBinding = { field: WeatherField }
export type WeatherSource = { provider: 'open-meteo'; latitude: number; longitude: number; label: string; timezone: string }
export type WeatherData = { schema: 1; label: string; timezone: string; observed_at: string; temperature: number; feels_like: number; humidity: number; wind: number; code: number; demo: boolean; attribution: string;
  hours: { time: string; temperature: number; rain: number; humidity?: number }[]; days: { date: string; low: number; high: number; code: number }[] }
export const weatherLabels: Record<WeatherField, string> = { location: '地点', date:'年月日', 'day-number':'大字日期', weekday:'星期', 'lunar-date':'农历', temperature: '当前温度', 'feels-like': '体感温度', humidity: '相对湿度', wind: '风速', summary: '天气状况', updated: '更新时间', icon: '天气图标', 'temperature-chart': '24小时温湿度', 'rain-chart': '降水概率柱状图', forecast: '未来天气', attribution: '数据来源' }

/** Provider-local calendar date: never the browser/host clock or timezone. */
export function weatherCalendarLabels(d:Pick<WeatherData,'observed_at'>) {
  const [year,month,day]=d.observed_at.slice(0,10).split('-').map(Number)
  const date=new Date(Date.UTC(year!,month!-1,day!,12))
  const parts=new Intl.DateTimeFormat('zh-CN-u-ca-chinese',{month:'long',day:'numeric',timeZone:'UTC'}).formatToParts(date)
  const lunarMonth=parts.find(p=>p.type==='month')!.value
  const lunarDay=Number(parts.find(p=>p.type==='day')!.value)
  const days=['初一','初二','初三','初四','初五','初六','初七','初八','初九','初十','十一','十二','十三','十四','十五','十六','十七','十八','十九','二十','廿一','廿二','廿三','廿四','廿五','廿六','廿七','廿八','廿九','三十']
  return {date:`${year}年${month}月${day}日`,weekday:`星期${'日一二三四五六'[date.getUTCDay()]}`,
    'lunar-date':`${lunarMonth}${days[lunarDay-1]}`}
}
const finite = (n: unknown, low: number, high: number): n is number => typeof n === 'number' && Number.isFinite(n) && n >= low && n <= high
export function validWeatherSource(source: unknown): source is WeatherSource {
  const s = source as WeatherSource
  return Boolean(s && typeof s === 'object' && !Array.isArray(s) && s.provider === 'open-meteo' && finite(s.latitude, -90, 90) && finite(s.longitude, -180, 180)
    && typeof s.label === 'string' && s.label.length > 0 && s.label.length <= 32 && !/[\x00-\x1f\x7f]/.test(s.label)
    && typeof s.timezone === 'string' && s.timezone.length > 0 && s.timezone.length <= 64 && /^[A-Za-z0-9_+\-/]+$/.test(s.timezone))
}
export function validateWeather(value: unknown): WeatherData {
  const d = value as WeatherData
  const date = (s: unknown) => typeof s === 'string' && /^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2}(:\d{2})?)?$/.test(s) && Number.isFinite(Date.parse(s))
  if (!d || d.schema !== 1 || typeof d.label !== 'string' || d.label.length > 32 || !d.label.length || /[\x00-\x1f\x7f]/.test(d.label) || !date(d.observed_at)
    || !finite(d.temperature, -100, 100) || !finite(d.feels_like, -100, 100) || !finite(d.humidity, 0, 100) || !finite(d.wind, 0, 500) || !finite(d.code, 0, 99)
    || !Array.isArray(d.hours) || d.hours.length < 6 || d.hours.length > 25 || !Array.isArray(d.days) || !d.days.length || d.days.length > 5
    || d.hours.some((h, i) => !h || !date(h.time) || !finite(h.temperature, -100, 100) || !finite(h.rain, 0, 100) || (h.humidity !== undefined && !finite(h.humidity,0,100)) || (i > 0 && h.time <= d.hours[i-1].time))
    || d.days.some(h => !h || !date(h.date) || !finite(h.low, -100, 100) || !finite(h.high, h.low, 100) || !finite(h.code, 0, 99))
    || typeof d.attribution !== 'string' || d.attribution.length > 100 || typeof d.demo !== 'boolean') throw new Error('天气数据不完整或格式无效')
  return d
}
export function demoWeather(): WeatherData {
  const temps = [22,23,24,25,26,27,27,27,26,25,23,22,21,20,19,18,18,17,17,17,18,18,19,20]
  return { schema: 1, label: '杭州', timezone: 'Asia/Shanghai', observed_at: '2026-10-18T08:00', temperature: 22, feels_like: 23, humidity: 68, wind: 11, code: 2, demo: true, attribution: 'Open-Meteo · CC BY 4.0',
    hours: temps.map((temperature,i) => ({ time: new Date(Date.UTC(2026,9,18,8+i)).toISOString().slice(0,16), temperature, humidity:[68,64,60,56,52,48,46,50,54,62,72,78,82,84,86,88,86,84,82,80,78,76,72,70][i], rain: [10,10,5,5,5,10,15,25,45,65,80,70,50,35,25,20,15,10,10,10,10,15,20,15][i] })),
    days: [2,61,3,1,0].map((code,i) => ({date: `2026-10-${18+i}`, low: [17,18,16,15,16][i], high: [27,25,23,24,26][i], code})) }
}
export function weatherSummary(code: number) {
  return code === 0 ? '晴' : code <= 2 ? '晴间多云' : code === 3 ? '阴' : code <= 48 ? '雾' : code <= 67 ? '雨' : code <= 77 ? '雪' : code <= 82 ? '阵雨' : code <= 86 ? '阵雪' : '雷雨'
}
export type WeatherPrimitive = { kind: 'text'; x: number; y: number; width: number; height: number; text: string; size: number; color: string; bold?: boolean; align?: 'left' | 'center' | 'right' }
  | { kind: 'line'; x: number; y: number; x2: number; y2: number; color: string; thickness: number }
  | { kind: 'rect'; x: number; y: number; width: number; height: number; color: string }
type Box = { width: number; height: number; style: {fontSize: number; color: string; bold?: boolean; align?: 'left'|'center'|'right'}; hostWeather?: WeatherBinding }
/** Coordinates are relative to the editable widget. Browser and CLI consume the same plan. */
export function weatherPrimitives(w: Box, d: WeatherData): WeatherPrimitive[] {
  const field = w.hostWeather?.field, width = w.width, height = w.height, color = w.style.color, size = w.style.fontSize
  const text = (value: string, x=0, y=0, ww=width, hh=height, fontSize=size, align=w.style.align): WeatherPrimitive => ({kind:'text',x,y,width:ww,height:hh,text:value,size:fontSize,color,bold:w.style.bold,align})
  const line = (x:number,y:number,x2:number,y2:number,ink=color,thickness=1): WeatherPrimitive => ({kind:'line',x,y,x2,y2,color:ink,thickness})
  const summary=weatherSummary(d.code)
  const calendar=weatherCalendarLabels(d)
  if(field==='day-number') {
    const day=Number(d.observed_at.slice(8,10)),count=String(day).length
    const scale=Math.max(1,Math.floor(Math.min((width/count-2)/11,(height-2)/20,size/20)))
    const left=Math.floor((width-count*(11*scale+2))/2),top=Math.floor((height-(20*scale+2))/2),parts:WeatherPrimitive[]=[]
    drawSegmentNumber({drawFastHLine:(x,y,w,ink)=>{if(ink===color)parts.push({kind:'rect',x,y,width:w,height:1,color})},drawFastVLine:(x,y,h,ink)=>{if(ink===color)parts.push({kind:'rect',x,y,width:1,height:h,color})}},day,left,top,scale,color,'transparent',count)
    return parts
  }
  if(field==='date') {
    // Editable date binding with separate semantic ink for numerals and units.
    const [year,month,day]=d.observed_at.slice(0,10).split('-').map(Number)
    const runs=[String(year),'年',String(month),'月',String(day),'日']
    const units=runs.map((run,i)=>run.length*(i%2?1:.58)),total=units.reduce((a,b)=>a+b,0)
    const fontSize=Math.min(size,width/total,height/1.4),span=total*fontSize
    let x=(width-span)*(w.style.align==='right'?1:w.style.align==='center'?.5:0)
    return runs.map((run,i)=>{const ww=units[i]*fontSize,p={...text(run,x,0,ww,height,fontSize,'center'),color:i%2?'#000000':color};x+=ww;return p})
  }
  const values = {location:d.label,temperature:`${Math.round(d.temperature)}°`, 'feels-like':`体感 ${Math.round(d.feels_like)}°`,humidity:`湿度 ${Math.round(d.humidity)}%`,wind:`风速 ${Math.round(d.wind)} km/h`,summary:summary==='晴间多云'&&width<size*4?'多云':summary,updated:`${d.observed_at.slice(11,16)}${width>=size*6?' 更新':''}`,attribution:d.attribution}
  if(field && field in calendar)return [text(calendar[field as keyof typeof calendar])]
  if (field && field in values) {
    const value=values[field as keyof typeof values]
    return [text(value,0,0,width,height,field==='temperature'?Math.min(size,width/(value.length*.65)):size)]
  }
  if (field === 'forecast') {
    const future=d.days.filter(day=>day.date>d.observed_at.slice(0,10)),available=future.length?future:d.days
    if(width>=size*8&&height>=size*5){
      const days=available.slice(0,Math.max(1,Math.min(3,Math.floor(width/(size*3.4))))),step=width/days.length
      return days.flatMap((day,i)=>{
        const x=i*step,iconSize=Math.min(step*.65,height-size*4.2),iconX=x+(step-iconSize)/2,iconY=size*1.5
        const label=weatherSummary(day.code),short=label==='晴间多云'?'多云':label
        const icon=weatherPrimitives({width:iconSize,height:iconSize,style:w.style,hostWeather:{field:'icon'}},{...d,code:day.code})
          .map(p=>p.kind==='line'?{...p,x:p.x+iconX,y:p.y+iconY,x2:p.x2+iconX,y2:p.y2+iconY}:p)
        const name=i===0?'明天':i===1?'后天':`周${'日一二三四五六'[new Date(day.date+'T12:00:00Z').getUTCDay()]}`
        return [text(name,x,0,step,size*1.4,size,'center'),...icon,
          text(`${Math.round(day.low)}–${Math.round(day.high)}°`,x,height-size*2.8,step,size*1.4,size,'center'),
          text(short,x,height-size*1.4,step,size*1.4,size,'center')]
      })
    }
    if(height>width*.65&&height>=size*6){
      const days=available.slice(0,Math.min(3,Math.floor(height/(size*2.8)))),step=height/days.length
      return days.flatMap((day,i)=>[text(day.date.slice(5).replace('-','/'),0,i*step,width*.42,size*1.4,size),
        text(weatherSummary(day.code),width*.42,i*step,width*.58,size*1.4,size,'right'),
        text(`${Math.round(day.low)} / ${Math.round(day.high)}°`,0,i*step+size*1.4,width,size*1.4,size,'right')])
    }
    const days = available.slice(0, Math.max(1,Math.min(5,Math.floor(width/(size*5.5))))), step=width/days.length
    return days.flatMap((day,i) => [text(day.date.slice(5).replace('-','/'),i*step,0,step,height/3,size,'center'),
      text(weatherSummary(day.code),i*step,height/3,step,height/3,size,'center'), text(`${Math.round(day.low)} / ${Math.round(day.high)}°`,i*step,height*2/3,step,height/3,size,'center')])
  }
  if (field === 'temperature-chart' || field === 'rain-chart') {
    const rain=field==='rain-chart', humid=!rain&&d.hours.every(h=>h.humidity!==undefined)
    const labelSize=Math.max(12,Math.min(size,Math.floor(height/6))), left=Math.ceil(labelSize*2), right=width-(humid?left:6), top=labelSize*1.7, bottom=height-labelSize*1.5
    if (right <= left || bottom <= top) return [text('扩大图表区域',0,0,width,height,6)]
    const readings=d.hours, data=readings.map(h=>rain?h.rain:h.temperature), low=rain?0:Math.floor(Math.min(...data)-2), high=rain?100:Math.ceil(Math.max(...data)+2), range=Math.max(1,high-low)
    const x=(i:number)=>left+i*(right-left)/(data.length-1), y=(v:number)=>bottom-(v-low)/range*(bottom-top)
    const title=rain?'降水概率 %':width<220?`24h °C${humid?' / %':''}`:`24h 气温 °C${humid?' · 湿度 %':''}`
    const parts: WeatherPrimitive[]=[text(title,0,0,width,labelSize+2,labelSize),text(String(high),0,top-labelSize*.5,left-3,labelSize,labelSize,'right'),text(String(low),0,bottom-labelSize,left-3,labelSize,labelSize,'right'),line(left,bottom,right,bottom,'#000000')]
    const humidityInk=color.toLowerCase()==='#000000'?'#b32025':'#000000'
    if(!rain&&width>=220){
      parts[0]=text('未来24小时',0,0,width*.43,labelSize+2,labelSize)
      parts.push(text('气温 °C',width*.44,0,width*.26,labelSize+2,labelSize),
        ...(humid?[{...text('湿度 %',width*.72,0,width*.28,labelSize+2,labelSize,'right'),color:humidityInk}]:[]))
    }
    const barWidth=Math.max(1,(right-left)/data.length*.42)
    for (let i=0;i<data.length;i++) {
      parts.push({kind:'rect',x:Math.min(right-barWidth,Math.max(left,x(i)-barWidth/2)),y:y(data[i]),width:barWidth,height:Math.max(1,bottom-y(data[i])),color})
    }
    if(humid){
      parts.push(text('100',right+2,top-labelSize*.5,left-2,labelSize,labelSize,'left'),text('0',right+2,bottom-labelSize,left-2,labelSize,labelSize,'left'))
      const hy=(i:number)=>bottom-readings[i].humidity!/100*(bottom-top)
      // White halo keeps the humidity line visible across monochrome temperature bars.
      for(const [ink,thickness] of [['#ffffff',4],[humidityInk,2]] as const)
        for(let i=1;i<data.length;i++) parts.push(line(x(i-1),hy(i-1),x(i),hy(i),ink,thickness))
    }
    const timeWidth=labelSize*3, ticks=right-left<timeWidth*2?[0,data.length-1]:[0,Math.floor((data.length-1)/2),data.length-1]
    for (const i of ticks) parts.push(text(readings[i].time.slice(11,16),Math.max(0,Math.min(width-timeWidth,x(i)-timeWidth/2)),bottom+3,timeWidth,labelSize,labelSize,'center'))
    return parts
  }
  if (field === 'icon') {
    // Original, code-drawn weather symbols. No external assets or font glyph dependency.
    const s=Math.min(width,height), ox=(width-s)/2, oy=(height-s)/2, parts:WeatherPrimitive[]=[]
    const path=(points:number[][])=>{for(let i=1;i<points.length;i++)parts.push(line(ox+points[i-1][0]*s,oy+points[i-1][1]*s,ox+points[i][0]*s,oy+points[i][1]*s,color,Math.max(1,s/40)))}
    const curve=(p:number[][])=>path(Array.from({length:13},(_,i)=>{
      const t=i/12,u=1-t
      return [0,1].map(axis=>u*u*u*p[0][axis]+3*u*u*t*p[1][axis]+3*u*t*t*p[2][axis]+t*t*t*p[3][axis])
    }))
    if(d.code<=2) {
      const [cx,cy,r]=d.code===0?[.5,.48,.22]:[.70,.26,.13]
      path(Array.from({length:49},(_,i)=>{
        const a=d.code===0?i*Math.PI/24:Math.PI+i*Math.PI/32
        return [cx+Math.cos(a)*r,cy+Math.sin(a)*r]
      }))
      for(const a of d.code===0?Array.from({length:8},(_,i)=>i*Math.PI/4):[-Math.PI/2,-Math.PI/4,0,Math.PI/4])
        path([[cx+Math.cos(a)*(r+.06),cy+Math.sin(a)*(r+.06)],[cx+Math.cos(a)*(r+.13),cy+Math.sin(a)*(r+.13)]])
    }
    if(d.code>0){
      curve([[.22,.73],[.02,.73],[.02,.46],[.22,.45]])
      curve([[.22,.45],[.21,.19],[.61,.17],[.66,.45]])
      curve([[.66,.45],[.93,.36],[1.0,.73],[.76,.73]])
      path([[.76,.73],[.22,.73]])
    }
    if(d.code>=95) path([[.58,.7],[.4,.83],[.56,.83],[.42,.98]])
    else if((d.code>=71&&d.code<=77)||d.code===85||d.code===86) for(let i=0;i<3;i++) {
      const x=.28+i*.22,y=.83
      path([[x-.05,y],[x+.05,y]]);path([[x,y-.06],[x,y+.06]]);path([[x-.04,y-.04],[x+.04,y+.04]])
    }
    else if(d.code>=51) for(let i=0;i<3;i++)path([[.32+i*.2,.76],[.27+i*.2,.9]])
    else if(d.code>=40) {path([[.2,.78],[.8,.78]]);path([[.25,.9],[.75,.9]])}
    return parts
  }
  return []
}
