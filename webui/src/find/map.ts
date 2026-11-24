import type { FindReport } from './client'

export type FindMapProvider = 'baidu' | 'amap' | 'osm'
export const FIND_TRACK_WINDOW_HOURS = 24
export const FIND_TRACK_RETENTION_DAYS = 90

export interface FindMapDetails {
  deviceName: string
  address: string
  coordinates: string
  reportTime: string
  accuracyText: string
  urls: Record<FindMapProvider, string>
  openUrls: Record<FindMapProvider, string>
}

function createUrl(base: string, params: Record<string, string>) {
  const url = new URL(base)
  for (const [name, value] of Object.entries(params)) url.searchParams.set(name, value)
  return url.toString().replace(/\+/g, '%20')
}

export function buildFindMapDetails(report: FindReport, address: string, deviceName: string): FindMapDetails {
  const latitude = Number(report.latitude).toFixed(7)
  const longitude = Number(report.longitude).toFixed(7)
  const accuracy = Number(report.horizontalAccuracy)
  const reportTime = new Date(report.timestamp).toLocaleString('zh-CN')
  const accuracyText = Number.isFinite(accuracy) ? `水平精度约 ${accuracy} 米` : '水平精度未知'
  const safeAddress = address || '--'
  const markerTitle = deviceName.trim() || '未命名 InkTag'
  const latitudeNumber = Number(latitude)
  const longitudeNumber = Number(longitude)
  const osmOpen = new URL('https://www.openstreetmap.org/')
  osmOpen.searchParams.set('mlat', latitude)
  osmOpen.searchParams.set('mlon', longitude)
  osmOpen.hash = `map=18/${latitude}/${longitude}`
  return {
    deviceName: markerTitle,
    address: safeAddress,
    coordinates: `${latitude}, ${longitude}`,
    reportTime,
    accuracyText,
    urls: {
      baidu: createUrl('https://api.map.baidu.com/marker', {
        location: `${latitude},${longitude}`,
        title: markerTitle,
        content: `GPS坐标：${latitude},${longitude}`,
        output: 'html',
        coord_type: 'wgs84',
        zoom: '18',
        src: 'webapp.InkTag.Find',
      }),
      amap: createUrl('https://uri.amap.com/marker', {
        position: `${longitude},${latitude}`,
        name: markerTitle,
        src: 'InkTag',
        coordinate: 'wgs84',
        callnative: '0',
      }),
      osm: createUrl('https://www.openstreetmap.org/export/embed.html', {
        bbox: `${(longitudeNumber - 0.005).toFixed(7)},${(latitudeNumber - 0.003).toFixed(7)},${(longitudeNumber + 0.005).toFixed(7)},${(latitudeNumber + 0.003).toFixed(7)}`,
        layer: 'mapnik',
        marker: `${latitude},${longitude}`,
      }),
    },
    openUrls: {
      baidu: createUrl('https://api.map.baidu.com/marker', {
        location: `${latitude},${longitude}`,
        title: markerTitle,
        content: `GPS坐标：${latitude},${longitude}`,
        output: 'html',
        coord_type: 'wgs84',
        zoom: '18',
        src: 'webapp.InkTag.Find',
      }),
      amap: createUrl('https://uri.amap.com/marker', {
        position: `${longitude},${latitude}`,
        name: markerTitle,
        src: 'InkTag',
        coordinate: 'wgs84',
        callnative: '0',
      }),
      osm: osmOpen.toString(),
    },
  }
}

export function findMapProviderName(provider: FindMapProvider) {
  return provider === 'baidu' ? '百度' : provider === 'amap' ? '高德' : 'OSMap'
}

export function findTrackMapKeyLabel(provider: FindMapProvider) {
  return provider === 'baidu' ? '百度地图 AK' : provider === 'amap' ? '高德地图 Key' : ''
}

export function findTrackMapNeedsKey(provider: FindMapProvider) {
  return provider !== 'osm'
}

export function formatFindReportAge(timestamp: string, now = Date.now()) {
  const reportTime = new Date(timestamp).getTime()
  if (!Number.isFinite(reportTime)) return '--'
  const deltaMs = now - reportTime
  const future = deltaMs < 0
  const totalMinutes = Math.max(0, Math.round(Math.abs(deltaMs) / 60_000))
  if (totalMinutes < 1) return future ? '不到 1 分钟后' : '不到 1 分钟前'
  const days = Math.floor(totalMinutes / 1440)
  const hours = Math.floor((totalMinutes % 1440) / 60)
  const minutes = totalMinutes % 60
  const parts = []
  if (days) parts.push(`${days} 天`)
  if (hours) parts.push(`${hours} 小时`)
  if (minutes && !days) parts.push(`${minutes} 分钟`)
  return `${parts.join(' ')}${future ? '后' : '前'}`
}
