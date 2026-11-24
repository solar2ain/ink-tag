import { describe, expect, it } from 'vitest'
import { buildFindMapDetails, FIND_TRACK_RETENTION_DAYS, FIND_TRACK_WINDOW_HOURS, findTrackMapKeyLabel, findTrackMapNeedsKey, formatFindReportAge } from './map'

const report = {
  found: true as const,
  timestamp: '2026-09-03T04:00:00.000Z',
  latitude: 40.0833306,
  longitude: 116.2536576,
  horizontalAccuracy: 44,
  confidence: 1,
  status: 0,
}

describe('Find map presentation', () => {
  it('marks WGS-84 coordinates and keeps details in the shared overlay', () => {
    const details = buildFindMapDetails(report, 'F2:92:D8:F9:7E:4D', 'Hanshow 1.54R 9903')
    const baidu = new URL(details.urls.baidu)
    const amap = new URL(details.urls.amap)
    const osm = new URL(details.urls.osm)
    expect(baidu.searchParams.get('coord_type')).toBe('wgs84')
    expect(baidu.searchParams.get('title')).toBe('Hanshow 1.54R 9903')
    expect(baidu.searchParams.get('content')).toBe('GPS坐标：40.0833306,116.2536576')
    expect(baidu.searchParams.get('content')).not.toContain('F2:92:D8:F9:7E:4D')
    expect(details.urls.baidu).not.toContain('+')
    expect(details.urls.amap).not.toContain('+')
    expect(amap.searchParams.get('coordinate')).toBe('wgs84')
    expect(amap.searchParams.get('position')).toBe('116.2536576,40.0833306')
    expect(amap.searchParams.get('name')).toBe('Hanshow 1.54R 9903')
    expect(osm.hostname).toBe('www.openstreetmap.org')
    expect(osm.searchParams.get('marker')).toBe('40.0833306,116.2536576')
    expect(details.openUrls.osm).toContain('#map=18/40.0833306/116.2536576')
  })

  it('formats report age without depending on the wall clock', () => {
    expect(formatFindReportAge(report.timestamp, new Date('2026-09-03T06:05:00.000Z').getTime())).toBe('2 小时 5 分钟前')
  })

  it('uses keyless OpenStreetMap as the bounded trajectory fallback', () => {
    expect(FIND_TRACK_WINDOW_HOURS).toBe(24)
    expect(FIND_TRACK_RETENTION_DAYS).toBe(90)
    expect(findTrackMapKeyLabel('baidu')).toBe('百度地图 AK')
    expect(findTrackMapKeyLabel('amap')).toBe('高德地图 Key')
    expect(findTrackMapKeyLabel('osm')).toBe('')
    expect(findTrackMapNeedsKey('baidu')).toBe(true)
    expect(findTrackMapNeedsKey('amap')).toBe(true)
    expect(findTrackMapNeedsKey('osm')).toBe(false)
  })
})
