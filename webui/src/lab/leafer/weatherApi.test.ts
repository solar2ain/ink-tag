import { afterEach, describe, expect, it, vi } from 'vitest'
import { fetchWeather } from './weatherApi'
import { demoWeather } from '../../../../packages/inktag-editor-core/src/weather'
const source={provider:'open-meteo' as const,latitude:30.27,longitude:120.15,label:'杭州',timezone:'Asia/Shanghai'}
function raw(){const d=demoWeather();return {current_units:{temperature_2m:'°C',wind_speed_10m:'km/h'},timezone:d.timezone,current:{time:d.observed_at,temperature_2m:d.temperature,apparent_temperature:d.feels_like,relative_humidity_2m:d.humidity,wind_speed_10m:d.wind,weather_code:d.code},hourly:{time:d.hours.map(h=>h.time),temperature_2m:d.hours.map(h=>h.temperature),precipitation_probability:d.hours.map(h=>h.rain)},daily:{time:d.days.map(h=>h.date),temperature_2m_min:d.days.map(h=>h.low),temperature_2m_max:d.days.map(h=>h.high),weather_code:d.days.map(h=>h.code)}}}
afterEach(()=>vi.unstubAllGlobals())
describe('public weather preview',()=>{
  it('normalizes the same snapshot contract and sends only public source fields',async()=>{
    const mock=vi.fn(async()=>new Response(JSON.stringify(raw())))
    vi.stubGlobal('fetch',mock)
    const data=await fetchWeather(source,new AbortController().signal)
    expect(data).toMatchObject({schema:1,label:'杭州',demo:false,temperature:22})
    expect(data.hours).toHaveLength(24)
    expect(data.hours.every(h=>h.humidity===undefined)).toBe(true)
    const [url,options]=mock.mock.calls[0] as unknown as [URL,RequestInit]
    expect(url.origin+url.pathname).toBe('https://api.open-meteo.com/v1/forecast')
    expect(options).toMatchObject({credentials:'omit',referrerPolicy:'no-referrer',redirect:'error'})
    expect(url.searchParams.get('hourly')).toBe('temperature_2m,relative_humidity_2m,precipitation_probability')
    expect([...url.searchParams.keys()].sort()).toEqual(['latitude','longitude','timezone','current','hourly','daily','forecast_days','temperature_unit','wind_speed_unit'].sort())
  })
  it('preserves actual hourly humidity and refuses invalid values',async()=>{
    const data={...raw(),hourly:{...raw().hourly,relative_humidity_2m:demoWeather().hours.map(h=>h.humidity)}}
    vi.stubGlobal('fetch',async()=>new Response(JSON.stringify(data)))
    const result=await fetchWeather(source,new AbortController().signal)
    expect(result.hours.map(h=>h.humidity)).toEqual(demoWeather().hours.map(h=>h.humidity))
    data.hourly.relative_humidity_2m[1]=101
    await expect(fetchWeather(source,new AbortController().signal)).rejects.toThrow('格式无效')
  })
  it.each([{}, {current_units:{}}])('refuses malformed provider data without changing saved state',async value=>{
    vi.stubGlobal('fetch',async()=>new Response(JSON.stringify(value)))
    await expect(fetchWeather(source,new AbortController().signal)).rejects.toThrow('格式无效')
  })
  it('refuses HTTP failures and invalid coordinates',async()=>{
    const mock=vi.fn(async()=>new Response('',{status:503}));vi.stubGlobal('fetch',mock)
    await expect(fetchWeather(source,new AbortController().signal)).rejects.toThrow('HTTP 503')
    await expect(fetchWeather({...source,latitude:100},new AbortController().signal)).rejects.toThrow('有效')
    expect(mock).toHaveBeenCalledTimes(1)
  })
})
