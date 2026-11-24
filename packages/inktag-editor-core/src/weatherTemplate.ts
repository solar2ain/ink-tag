import type { Document, Widget } from './index'
import { weatherLabels, type WeatherField } from './weather'

export function weatherWidget(doc: Document, field: WeatherField): Widget {
  let id=`weather-${field}`, i=1
  while(doc.widgets.some(w=>w.id===id)) id=`weather-${field}-${i++}`
  return { id, name: weatherLabels[field], kind:'text', x:8, y:8, width:Math.min(doc.width-16,200), height:Math.min(doc.height-34, ['temperature-chart','rain-chart'].includes(field)?100:36),
    style:{fontSize:Math.max(8,Math.round(Math.min(doc.width,doc.height)*.055)),color:'#000000',bold:false,align:'left'},cells:[[{text:''}]],hostWeather:{field} }
}

/** Size-specific compositions, made entirely from ordinary editable controls. */
export function weatherDocument(width:number,height:number,variant='paper'): Document {
  if(variant==='date-card')return weatherDateCardDocument(width,height)
  const doc:Document={version:1,width,height,pageMode:'host-dynamic',colorMode:2,footerInset:0,widgets:[],weatherSource:{provider:'open-meteo',latitude:30.27,longitude:120.15,label:'杭州',timezone:'auto'}}
  const edge=Math.min(width,height),inset=Math.max(6,Math.round(edge*.04)),content=width-inset*2
  const small=Math.max(12,Math.round(edge*.045)),gap=Math.max(4,Math.round(small*.35)),row=Math.ceil(small*1.4)
  const bottom=height-Math.max(18,inset),accent='#b32025',ink='#000000'
  const put=(field:WeatherField,x:number,y:number,w:number,h:number,size=small,color='#000000',bold=false,align:'left'|'center'|'right'='left')=>{
    const item=weatherWidget(doc,field)
    Object.assign(item,{x:Math.round(x),y:Math.round(y),width:Math.max(1,Math.floor(w)),height:Math.max(1,Math.floor(h)),style:{fontSize:size,color,bold,align}})
    doc.widgets.push(item)
  }
  const rule=(y:number,x=inset,w=content)=>doc.widgets.push({id:`rule-${doc.widgets.length}`,name:'分隔线',kind:'line',x:Math.round(x),y:Math.round(y),width:Math.round(w),height:1,style:{fontSize:small,color:variant==='cards'?accent:'#000000',bold:false},cells:[[{text:''}]]})
  const header=()=>{
    if(width>=360){
      put('location',inset,inset,content*.14,row,small,accent)
      put('date',inset+content*.14,inset,content*.45,row,small,accent)
      put('weekday',inset+content*.59,inset,content*.17,row)
      put('lunar-date',inset+content*.76,inset,content*.24,row,small,accent,false,'right')
      rule(inset+row+gap)
      return inset+row+gap*2
    }
    if(width<300){
      put('date',inset,inset,content,row,small,accent)
      put('weekday',inset,inset+row,content*.42,row)
      put('lunar-date',inset+content*.42,inset+row,content*.58,row,small,accent,false,'right')
    }else{
      put('location',inset,inset,content*.3,row,small+2,accent)
      put('date',inset+content*.31,inset,content*.69,row,small,accent,false,'right')
      put('weekday',inset,inset+row,content*.35,row)
      put('lunar-date',inset+content*.35,inset+row,content*.65,row,small,accent,false,'right')
    }
    rule(inset+row*2+gap)
    return inset+row*2+gap*2
  }
  const y=header()
  if(height<=176&&width>height){
    // Shelf labels: readable current conditions rather than a miniature chart.
    const h=bottom-y,numberW=content*.43,iconW=content*.23,x=inset+content*.69
    put('temperature',inset,y,numberW,h,Math.round(h*.94),ink,true)
    put('icon',inset+numberW,y,iconW,h)
    put('summary',x,y,content*.31,row,small,accent)
    put('humidity',x,y+Math.min(row,h-row),content*.31,row)
    if(h>=row*3)put('wind',x,y+row*2,content*.31,row)
    return doc
  }
  if(width<=240&&(width===height||height<240)){
    const h=Math.min(content*.42,bottom-y-row-gap),numberW=content*.60
    put('temperature',inset,y,numberW,h,Math.round(h),ink,true)
    put('icon',inset+numberW,y,content-numberW,h*.68)
    put('summary',inset+numberW,y+h*.72,content-numberW,row,small,accent,false,'center')
    const metrics=Math.max(y+h,y+h*.72+row)+gap
    if(metrics+row<=bottom){
      put('location',inset,metrics,content*.4,row)
      put('humidity',inset+content*.4,metrics,content*.6,row,small,'#000000',false,'right')
    }
    if(metrics+row*2<=bottom)put('wind',inset,metrics+row,content,row)
    return doc
  }
  let chartY:number,forecastAbove=false
  if(width<220){
    // Tall labels use a vertical hierarchy, not a cropped desktop composition.
    const numberH=Math.min(content*.61,height*.19)
    put('temperature',inset,y,content,numberH,Math.round(numberH*.98),ink,true,'center')
    const iconY=y+numberH+gap,iconH=Math.min(content*.48,height*.15)
    put('icon',inset,iconY,content,iconH)
    const statusY=iconY+iconH+gap
    put('location',inset,statusY,content,row,small,accent,false,'center')
    put('summary',inset,statusY+row,content,row,small,'#000000',false,'center')
    put('humidity',inset,statusY+row*2+gap,content,row)
    put('wind',inset,statusY+row*3+gap,content,row)
    chartY=statusY+row*4+gap*3
  }else{
    const portrait=height>width,heroH=Math.round(Math.min(height*(portrait?.25:.24),content*.31))
    const iconW=content*(portrait?.29:.20),numberX=inset+iconW+gap
    put('icon',inset,y,iconW,heroH)
    put('temperature',numberX,y,content*(portrait?.66:.42),heroH,Math.round(heroH*.91),ink,true)
    if(!portrait){
      const x=inset+content*.65
      put('forecast',x,y,content*.35,heroH+row,Math.max(12,Math.round(small*.84)))
      put('summary',numberX,y+heroH,content*.42,row,small+1,accent)
      forecastAbove=true
    }else{
      put('summary',inset,y+heroH+gap,content*.5,row,small+2,accent)
    }
    const metricsY=y+heroH+row+gap*2
    put('feels-like',inset,metricsY,content*.31,row)
    put('humidity',inset+content*.32,metricsY,content*.31,row)
    put('wind',inset+content*.64,metricsY,content*.36,row,small,'#000000',false,'right')
    rule(metricsY+row+gap)
    chartY=metricsY+row+gap*2
  }
  const minChart=Math.max(72,small*4.5),forecastH=Math.ceil(small*4.2)
  const available=bottom-chartY,showForecast=!forecastAbove&&available>=minChart+forecastH+gap*3
  const chartH=available-(showForecast?forecastH+gap*3:0)
  if(chartH>=minChart)put('temperature-chart',inset,chartY,content,chartH)
  if(showForecast){
    const forecastY=bottom-forecastH
    rule(forecastY-gap*2)
    put('forecast',inset,forecastY,content,forecastH)
  }
  return doc
}

/** Insert as a single editable/undoable group without replacing existing work. */
export function weatherCombination(doc:Document):Widget[] {
  const model={...doc,widgets:[...doc.widgets]},out:Widget[]=[]
  let groupId='weather-group',i=1
  while(doc.widgets.some(w=>w.groupId===groupId))groupId=`weather-group-${i++}`
  const x=8,y=8,w=Math.min(300,doc.width-16),h=Math.min(140,doc.height-34),s=Math.max(10,Math.round(Math.min(w/14,h/8))),row=s*1.5
  const add=(field:WeatherField,xx:number,yy:number,ww:number,hh:number,size=s)=>{
    const item=weatherWidget(model,field);Object.assign(item,{groupId,x:Math.round(x+xx),y:Math.round(y+yy),width:Math.max(1,Math.round(ww)),height:Math.max(1,Math.round(hh)),style:{fontSize:size,color:'#000000',bold:field==='temperature'}})
    model.widgets.push(item);out.push(item)
  }
  add('location',0,0,w,row)
  add('icon',0,row,w*.28,h-row*2)
  add('temperature',w*.32,row,w*.68,(h-row*2)*.65,Math.round((h-row*2)*.65))
  add('summary',w*.32,h-row*2,w*.68,row)
  add('humidity',0,h-row,w*.5,row);add('wind',w*.5,h-row,w*.5,row)
  return out
}

/** Paper date placard: device-clock drawn day, local calendar and weather. */
export function weatherDateCardDocument(width:number,height:number):Document {
  const doc:Document={version:1,width,height,pageMode:'host-dynamic',colorMode:2,footerInset:0,weatherSource:{provider:'open-meteo',latitude:30.27,longitude:120.15,label:'杭州',timezone:'auto'},widgets:[]}
  const edge=Math.min(width,height),inset=Math.max(6,Math.round(edge*.04)),cw=width-inset*2,bottom=height-Math.max(18,inset),s=Math.max(12,Math.round(edge*.045)),row=Math.ceil(s*1.45),gap=Math.max(4,Math.round(s*.4)),accent='#b32025'
  const put=(field:WeatherField,x:number,y:number,w:number,h:number,size=s,color='#000000',align:'left'|'center'|'right'='left')=>{
    const item=weatherWidget(doc,field);Object.assign(item,{x:Math.round(x),y:Math.round(y),width:Math.max(1,Math.floor(w)),height:Math.max(1,Math.floor(h)),style:{fontSize:field==='day-number'?Math.min(240,Math.round(Math.min(w/1.22,h*1.15))):size,color,bold:field==='temperature'||field==='day-number',align}});doc.widgets.push(item)
  }
  const rule=(y:number)=>doc.widgets.push({id:`rule-${doc.widgets.length}`,name:'分隔线',kind:'line',x:inset,y:Math.round(y),width:cw,height:1,style:{fontSize:s,color:'#000000',bold:false},cells:[[{text:''}]]})
  put('date',inset,inset,width>=300?cw*.78:cw,row,s,accent)
  if(width>=300)put('location',inset+cw*.78,inset,cw*.22,row,s,'#000000','right')
  rule(inset+row+gap)
  const top=inset+row+gap*2,available=bottom-top,compact=edge<=240
  if(compact&&width>=height){
    const left=cw*.47,x=inset+left+gap,right=cw-left-gap
    put('day-number',inset,top,left,available,48,accent)
    if(available<row*3+24){
      put('temperature',x,top,right,available*.6,Math.floor(available*.5))
      put('summary',x,top+available*.6,right,available*.4,Math.max(12,Math.min(s,Math.floor(available*.3))))
      return doc
    }
    put('weekday',x,top,right,row)
    put('lunar-date',x,top+row,right,row,Math.max(12,s-1),accent)
    const hero=Math.max(24,Math.min(available-row*3,row*2.2))
    put('temperature',x,top+row*2,right,hero,Math.round(hero*.85))
    put('summary',x,top+row*2+hero,right,row,Math.max(12,s-1))
    if(available>=row*4+hero)put('humidity',x,top+row*3+hero,right,row,Math.max(12,s-1))
    return doc
  }
  if(width>=height){
    const forecastH=row*3,bodyH=available-forecastH-gap*3,left=cw*.45,x=inset+left+gap*2,right=cw-left-gap*2
    put('day-number',inset,top,left,bodyH,80,accent)
    put('weekday',x,top,right,row,s+2)
    put('lunar-date',x,top+row,right,row,s+1,accent)
    const iconH=Math.max(36,bodyH-row*4-gap*2),iconW=Math.min(right*.35,iconH)
    put('icon',x,top+row*2+gap,iconW,iconH)
    put('temperature',x+iconW+gap,top+row*2+gap,right-iconW-gap,iconH,Math.round(iconH*.8))
    put('summary',x,top+bodyH-row*2,right,row)
    put('humidity',x,top+bodyH-row,right,row)
    const fy=bottom-forecastH;rule(fy-gap);put('forecast',inset,fy,cw,forecastH,Math.max(12,s-1))
  }else{
    const dayH=Math.min(cw*.63,available*.38)
    put('day-number',inset,top,cw,dayH,80,accent)
    const cy=top+dayH+gap
    put('weekday',inset,cy,cw*.45,row,s+1)
    put('lunar-date',inset+cw*.45,cy,cw*.55,row,s,accent,'right')
    const wy=cy+row+gap,hero=Math.min(cw*.28,Math.max(30,(bottom-wy-row*4-gap*2)))
    put('icon',inset,wy,cw*.3,hero)
    put('temperature',inset+cw*.34,wy,cw*.66,hero,Math.round(hero*.85))
    put('summary',inset,wy+hero+gap,cw*.5,row)
    put('humidity',inset+cw*.5,wy+hero+gap,cw*.5,row,s,'#000000','right')
    const fy=wy+hero+gap+row+gap*2
    if(bottom-fy>=row*2.5){rule(fy-gap);put('forecast',inset,fy,cw,bottom-fy,Math.max(12,s-1))}
  }
  return doc
}
