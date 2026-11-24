import { FeedbackNotice, useFeedbackMessage } from '../../components/FeedbackNotice'
import { useEffect, useId, useRef, useState } from 'react'
import { CloudSun, ChevronDown, Layers, CalendarDays, Thermometer, Droplets, Wind, ChartColumn, MapPin } from 'lucide-react'
import { documentPageMode, type EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { weatherFields, weatherLabels, validWeatherSource, validateWeather, type WeatherData, type WeatherSource } from '../../../../packages/inktag-editor-core/src/weather'
import { weatherWidget, weatherCombination } from './weatherTemplate'
import { fetchWeather } from './weatherApi'
export const defaultWeatherSource: WeatherSource = {provider:'open-meteo',latitude:30.27,longitude:120.15,label:'杭州',timezone:'auto'}
export function WeatherControls({core,act}:{core:EditorCore;act:(fn:()=>unknown)=>void}) {
  const [open,setOpen] = useState(false)
  const id=useId(),trigger=useRef<HTMLButtonElement>(null)
  if(documentPageMode(core.doc)!=='host-dynamic')return null
  const icons={temperature:Thermometer,humidity:Droplets,wind:Wind,location:MapPin,date:CalendarDays,'day-number':CalendarDays,weekday:CalendarDays,'lunar-date':CalendarDays,'temperature-chart':ChartColumn,'rain-chart':ChartColumn}
  return <div className="weather-control-picker" onBlur={event=>{if(!event.currentTarget.contains(event.relatedTarget))setOpen(false)}} onKeyDown={event=>{
    if(event.key==='Escape'&&open){event.stopPropagation();setOpen(false);trigger.current?.focus()}
  }}><button ref={trigger} className="weather-control-trigger" aria-label="天气控件" aria-expanded={open} aria-controls={id} onClick={()=>setOpen(!open)}><CloudSun/><span>天气</span><ChevronDown aria-hidden="true" className={`device-control-chevron${open?' expanded':''}`}/></button>
    {open&&<div id={id} role="group" aria-label="天气数据控件" className="weather-control-menu"><button onClick={()=>{
      act(()=>{const widgets=weatherCombination(core.doc);core.change(doc=>{doc.weatherSource??={...defaultWeatherSource};doc.widgets.push(...widgets)});core.selectMany(widgets.map(w=>w.id))});setOpen(false)
    }}><Layers/><span>天气组合</span></button>{weatherFields.map(field=>{const Icon=icons[field as keyof typeof icons]??CloudSun;return <button key={field} onClick={()=>{
      act(()=>{const widget=weatherWidget(core.doc,field);core.change(doc=>{doc.weatherSource??={...defaultWeatherSource};doc.widgets.push(widget)});core.select(widget.id)});setOpen(false)
    }}><Icon/><span>{weatherLabels[field]}</span></button>})}</div>}</div>
}
export function WeatherPanel({core,act,data,onData}:{core:EditorCore;act:(fn:()=>unknown)=>void;data?:WeatherData;onData:(data?:WeatherData)=>void}) {
  const source = core.doc.weatherSource ?? defaultWeatherSource
  const [draft,setDraft] = useState(source),[message,setMessage,messageRevision] = useFeedbackMessage(''),[busy,setBusy] = useState(false)
  const pending=useRef<AbortController|null>(null),epoch=useRef(0),input=useRef<HTMLInputElement>(null)
  const key=JSON.stringify(source)
  useEffect(()=>{epoch.current++;pending.current?.abort();setBusy(false);setDraft(source);onData(undefined);setMessage(core.doc.weatherSource?'数据源已保存到原稿；当前为示例预览':'');return()=>{epoch.current++;pending.current?.abort()}},[core,key])
  const apply = () => {if(!validWeatherSource(draft)){setMessage('请输入有效地点、经纬度和时区');return}act(()=>core.change(doc=>{doc.weatherSource={...draft}}));setMessage('数据源已保存到原稿；CLI 按任务配置取数')}
  const load = async (file?:File) => {
    if(!file)return
    pending.current?.abort();setBusy(false)
    const current=++epoch.current
    try {if(!file)return;if(file.size>2*1024*1024)throw new Error('天气快照超过 2 MiB');const value=validateWeather(JSON.parse(await file.text()));if(current===epoch.current){onData(value);setMessage('已导入快照，仅用于画板预览，不写入任务原稿')}}
    catch(cause){if(current===epoch.current)setMessage(cause instanceof Error?cause.message:'天气快照无法读取')}
  }
  const refresh = async () => {
    if(busy)return
    if(!validWeatherSource(draft)){setMessage('请输入有效地点和坐标');return}
    if(JSON.stringify(draft)!==key||!core.doc.weatherSource){setMessage('请先应用数据源，再获取天气');return}
    const current=++epoch.current,controller=new AbortController();pending.current=controller;setBusy(true);setMessage('正在获取天气…')
    const timeout=setTimeout(()=>controller.abort(),12000)
    try {const value=await fetchWeather(source,controller.signal);if(current===epoch.current){onData(value);setMessage('天气预览已更新')}}
    catch(cause){if(current===epoch.current)setMessage(controller.signal.aborted?'天气请求超时，请稍后重试':cause instanceof Error?cause.message:'天气请求失败')}
    finally {clearTimeout(timeout);if(current===epoch.current)setBusy(false)}
  }
  return <details className="inspector-section weather-source-panel"><summary>天气数据源与预览</summary>
    <label>地点名称<input aria-label="天气地点名称" maxLength={32} value={draft.label} onChange={e=>setDraft({...draft,label:e.target.value})}/></label>
    <div className="field-pair"><label>纬度<input aria-label="天气纬度" type="number" min={-90} max={90} step="any" value={Number.isFinite(draft.latitude)?draft.latitude:''} onChange={e=>setDraft({...draft,latitude:e.target.value===''?NaN:Number(e.target.value)})}/></label><label>经度<input aria-label="天气经度" type="number" min={-180} max={180} step="any" value={Number.isFinite(draft.longitude)?draft.longitude:''} onChange={e=>setDraft({...draft,longitude:e.target.value===''?NaN:Number(e.target.value)})}/></label></div>
    <label>时区<input aria-label="天气时区" value={draft.timezone} onChange={e=>setDraft({...draft,timezone:e.target.value})} placeholder="auto / Asia/Shanghai"/></label>
    <div className="button-grid"><button onClick={apply}>应用数据源</button><button disabled={busy} onClick={()=>void refresh()}>获取天气预览</button><button onClick={()=>input.current?.click()}>导入天气快照</button><button onClick={()=>{pending.current?.abort();epoch.current++;setBusy(false);onData(undefined);setMessage('已切换为示例数据')}}>使用示例数据</button></div>
    <input hidden ref={input} type="file" accept="application/json,.json" onChange={e=>{void load(e.currentTarget.files?.[0]);e.currentTarget.value=''}}/>
    {message && <FeedbackNotice noticeKey={messageRevision}>{message}</FeedbackNotice>}
    <p role="status">{data ? `${data.demo?'示例数据':'天气快照'} · ${data.label} · ${data.observed_at}`:'当前为示例数据，不是真实天气'}</p>
    <p>联网预览仅向 Open-Meteo 发送上述位置与时区。定时更新由 CLI 运行；字体使用主机本地字体。导出的原稿不包含设备地址或账号。</p>
    <p>天气数据：<a href="https://open-meteo.com/" target="_blank" rel="noopener noreferrer">Open-Meteo</a> · <a href="https://creativecommons.org/licenses/by/4.0/" target="_blank" rel="noopener noreferrer">CC BY 4.0</a>。模板对数据作图形化展示，屏幕画面不添加来源标签。</p>
  </details>
}
