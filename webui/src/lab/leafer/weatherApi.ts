import { validateWeather, validWeatherSource, type WeatherData, type WeatherSource } from '../../../../packages/inktag-editor-core/src/weather'

/** Public coordinates only. Never accepts device/account data or arbitrary endpoints. */
export async function fetchWeather(source: WeatherSource, signal: AbortSignal): Promise<WeatherData> {
  if (!validWeatherSource(source)) throw new Error('请填写有效的天气地点和坐标')
  const url = new URL('https://api.open-meteo.com/v1/forecast')
  const params = { latitude: String(source.latitude), longitude: String(source.longitude), timezone: source.timezone,
    current: 'temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m',
    hourly: 'temperature_2m,relative_humidity_2m,precipitation_probability', daily: 'weather_code,temperature_2m_max,temperature_2m_min',
    forecast_days: '5', temperature_unit: 'celsius', wind_speed_unit: 'kmh' }
  Object.entries(params).forEach(([key,value]) => url.searchParams.set(key,value))
  const response = await fetch(url, { signal, credentials: 'omit', referrerPolicy: 'no-referrer', redirect: 'error' })
  if (!response.ok) throw new Error(`天气服务暂不可用（HTTP ${response.status}）`)
  const reader = response.body?.getReader()
  if (!reader) throw new Error('无法读取天气服务响应')
  const chunks: Uint8Array[] = []; let size = 0
  try { for (;;) { const next = await reader.read(); if (next.done) break; size += next.value.length; if (size > 2*1024*1024) throw new Error('天气响应过大'); chunks.push(next.value) } }
  finally { await reader.cancel() }
  const bytes = new Uint8Array(size); let offset = 0
  for (const chunk of chunks) { bytes.set(chunk,offset); offset += chunk.length }
  try {
    const raw = JSON.parse(new TextDecoder().decode(bytes)), c = raw.current, h = raw.hourly, d = raw.daily
    if (raw.current_units?.temperature_2m !== '°C' || raw.current_units?.wind_speed_10m !== 'km/h') throw new Error('units')
    const start = c.time.slice(0,13)+':00', end = new Date(Date.parse(start+'Z')+24*3600000).toISOString().slice(0,16)
    return validateWeather({ schema:1, label:source.label, timezone:raw.timezone, observed_at:c.time, temperature:c.temperature_2m,
      feels_like:c.apparent_temperature, humidity:c.relative_humidity_2m, wind:c.wind_speed_10m, code:c.weather_code,
      demo:false, attribution:'Open-Meteo · CC BY 4.0',
      hours:h.time.map((time:string,i:number)=>({time,temperature:h.temperature_2m[i],rain:h.precipitation_probability[i],...(h.relative_humidity_2m?{humidity:h.relative_humidity_2m[i]}:{})})).filter((item:{time:string})=>item.time>=start&&item.time<end),
      days:d.time.slice(0,5).map((date:string,i:number)=>({date,low:d.temperature_2m_min[i],high:d.temperature_2m_max[i],code:d.weather_code[i]})) })
  } catch { throw new Error('天气数据不完整或格式无效，保留当前预览') }
}
