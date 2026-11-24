// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import type { FindIdentity, FindSessionState, FindTrackHistory } from '../find/client'
import { useRuntimeStore } from '../server/runtime'
import { useDeviceStore } from '../store/device'
import { FindPage } from './FindPage'
import { setPreferences } from '../preferences'

const api = vi.hoisted(() => ({ status: vi.fn(), selectIdentity: vi.fn(),
  trackingHistory: vi.fn(), trackingMap: vi.fn(), query: vi.fn(), configureTracking: vi.fn() }))
vi.mock('../find/client', () => ({ findClient: api }))
vi.mock('../protocol/client', () => ({ inktagClient: {} }))
vi.mock('../find/BrowserFindBackups', () => ({ BrowserFindBackups: () => null }))
vi.mock('../find/FindTransferTools', () => ({ FindTransferTools: () => null }))
vi.mock('../find/FindDeleteGuard', () => ({ FindDeleteGuard: () => null }))
vi.mock('../find/OsmTrackMap', () => ({
  orderOsmTrackPoints: (points: unknown[]) => points,
  OsmTrackMap: ({ points }: { points: { latitude: number }[] }) =>
    <output data-map-latitude={points[0]?.latitude} />,
}))

const A = 'a'.repeat(64), B = 'b'.repeat(64)
const identity = (keyFingerprint: string, suffix: string): FindIdentity => ({
  keyFingerprint, deviceId: `ble:AA:BB:CC:DD:00:${suffix}`, address: `DA:BB:CC:DD:00:${suffix}`,
  advertisementKey: '', deviceName: `标签 ${suffix}`, bluetoothName: `IKT_TL_00${suffix}`,
  findNote: '', trackingEnabled: false, lastAutoQueryAt: null, lastAutoError: null,
  trackPointCount: 1, createdAt: '2026-11-01T00:00:00Z', lastQueriedAt: null,
  lastReportAt: '2026-11-06T00:00:00Z', selected: keyFingerprint === A,
  derivationVersion: 2, currentSlot: 1,
})
let state: FindSessionState, host: HTMLDivElement, root: Root
const point = (fingerprint: string) => ({ timestamp: '2026-11-06T00:00:00Z',
  receivedAt: '2026-11-06T00:00:01Z', latitude: fingerprint === A ? 31 : 32,
  longitude: 120, horizontalAccuracy: 15, confidence: 1, status: 0 })
const history = (fingerprint: string): FindTrackHistory => ({ ok: true,
  keyFingerprint: fingerprint, total: 1, windowHours: 0, retentionDays: 90,
  points: [point(fingerprint)] })
const row = (fingerprint: string) => host.querySelector(`code[title="${fingerprint}"]`)!
  .closest<HTMLElement>('article')!
const mapLatitude = () => host.querySelector('[data-map-latitude]')?.getAttribute('data-map-latitude')
const selectRow = async (fingerprint: string) => {
  await act(async () => row(fingerprint).querySelector<HTMLElement>('.find-identity-list-meta')!.click())
}
async function mount() {
  await act(async () => root.render(<MemoryRouter><FindPage /></MemoryRouter>))
}
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.clearAllMocks()
  setPreferences({ findMapProvider: 'osm', findMapKeys: { baidu: '', amap: '', osm: '' } })
  vi.stubGlobal('fetch', vi.fn(() => Promise.reject(new Error('Unexpected network request'))))
  useDeviceStore.getState().resetDevice()
  useRuntimeStore.setState({ mode: 'server', capabilities: { apiVersion: 1,
    serverVersion: '1.0.0', features: ['find-query'], collections: [],
    limits: { jsonBytes: 1000, assetBytes: 1000 } } })
  state = { ok: true, loggedIn: true, queryReady: true, needs2fa: false,
    appleAccountUsername: '', sessionStored: true, sessionRestored: false,
    methods: [], activeMethodIndex: null, address: identity(A, '0A').address,
    keyFingerprint: A, advertisementKey: '', selectedKeyFingerprint: A,
    sessionProtection: 'aes256-local-key', trackingIntervalMinutes: 60,
    identities: [identity(A, '0A'), identity(B, '0B')] }
  api.status.mockImplementation(async () => structuredClone(state))
  api.selectIdentity.mockImplementation(async (fingerprint: string) => {
    state = { ...state, selectedKeyFingerprint: fingerprint, keyFingerprint: fingerprint,
      address: state.identities.find(i => i.keyFingerprint === fingerprint)!.address,
      identities: state.identities.map(i => ({ ...i, selected: i.keyFingerprint === fingerprint })) }
    return structuredClone(state)
  })
  api.trackingHistory.mockImplementation(async (fingerprint: string) => history(fingerprint))
  api.query.mockImplementation(async ({ fingerprint }: { fingerprint: string }) =>
    ({ ok: true, found: true, ...point(fingerprint) }))
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount()); host.remove()
  useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals()
})

it('selects a saved row and switches its latest local position without querying Apple', async () => {
  await mount(); expect(mapLatitude()).toBe('31')
  await selectRow(B)
  expect(row(B).classList.contains('selected')).toBe(true)
  expect(row(A).classList.contains('selected')).toBe(false)
  expect(host.querySelector('.find-query-target strong')?.textContent).toContain('0B')
  expect(mapLatitude()).toBe('32')
  expect(api.selectIdentity).toHaveBeenCalledExactlyOnceWith(B)
  expect(api.trackingHistory).toHaveBeenLastCalledWith(B, 1, 0, undefined, undefined, true)
  expect(api.query).not.toHaveBeenCalled()
  expect(api.configureTracking).not.toHaveBeenCalled()
})

it('uses native selection buttons for the row and device name, without duplicate selection', async () => {
  await mount()
  await act(async () => row(B).querySelector<HTMLButtonElement>('.find-identity-list-main')!.click())
  expect(api.selectIdentity).toHaveBeenCalledTimes(1)
  expect(row(B).querySelector('button')?.getAttribute('aria-pressed')).toBe('true')
  await act(async () => row(A).closest('.find-device-group')!
    .querySelector<HTMLButtonElement>('.find-device-group-title')!.click())
  expect(api.selectIdentity).toHaveBeenCalledTimes(2)
  expect(mapLatitude()).toBe('31')
})

it('hides the previous map when the newly selected tag has no saved point', async () => {
  api.trackingHistory.mockImplementation(async (fp: string) => ({ ...history(fp),
    points: fp === B ? [] : [point(fp)], total: fp === B ? 0 : 1 }))
  await mount(); expect(mapLatitude()).toBe('31')
  await selectRow(B)
  expect(row(B).classList.contains('selected')).toBe(true)
  expect(mapLatitude()).toBeUndefined()
})

it('ignores a late position response from the previously selected tag', async () => {
  let resolveOld!: (value: FindTrackHistory) => void
  api.trackingHistory.mockImplementation((fp: string) => fp === A
    ? new Promise<FindTrackHistory>(resolve => { resolveOld = resolve }) : Promise.resolve(history(fp)))
  await mount(); await selectRow(B); expect(mapLatitude()).toBe('32')
  await act(async () => resolveOld(history(A)))
  expect(mapLatitude()).toBe('32')
})

it('preserves the original selection on failure and allows retry', async () => {
  await mount(); api.selectIdentity.mockRejectedValueOnce(new Error('local storage unavailable'))
  await selectRow(B)
  expect(row(A).classList.contains('selected')).toBe(true)
  expect(mapLatitude()).toBe('31')
  expect(document.querySelector('[role=alert]')?.textContent).toContain('无法选中标签')
  await selectRow(B); expect(mapLatitude()).toBe('32')
})

it('does not treat delete/query controls as extra row-selection clicks', async () => {
  await mount()
  await act(async () => row(B).querySelector<HTMLButtonElement>('button[title="删除标签和轨迹"]')!.click())
  expect(api.selectIdentity).not.toHaveBeenCalled()
  await act(async () => Array.from(row(B).querySelectorAll('button'))
    .find(button => button.textContent === '查询最新')!.click())
  expect(api.selectIdentity).toHaveBeenCalledExactlyOnceWith(B)
  expect(api.query).toHaveBeenCalledTimes(1)
})

it('selects locally saved history even without a query key or a ready Apple session', async () => {
  state.queryReady = false; state.identities[1].historyOnly = true
  await mount(); await selectRow(B)
  expect(mapLatitude()).toBe('32')
  expect(api.query).not.toHaveBeenCalled()
})

it('uses the preferred map for the current point and falls back to OSM for keyless history', async () => {
  setPreferences({ findMapProvider: 'baidu' })
  await mount()
  expect(host.querySelector<HTMLIFrameElement>('.find-map-frame iframe')?.src).toContain('api.map.baidu.com/marker')
  const trackButton = Array.from(host.querySelectorAll<HTMLButtonElement>('.find-group-link')).find(button => button.textContent?.includes('轨迹'))!
  await act(async () => trackButton.click())
  expect(host.querySelector('.find-track-map-frame [data-map-latitude]')?.getAttribute('data-map-latitude')).toBe('31')
  expect(host.textContent).toContain('未设置百度地图 AK，轨迹暂用 OSMap')
  const notice = host.querySelector<HTMLElement>('.find-map-fallback')!
  expect(notice.getAttribute('role')).toBe('status')
  expect(notice.nextElementSibling?.classList.contains('find-track-map-frame')).toBe(true)
  expect(notice.textContent).toContain('可在上方填写')
  expect(host.querySelector('.find-track-map-config input[type=password]')).not.toBeNull()
  expect(api.trackingMap).not.toHaveBeenCalled()
})

it('keeps focus and scroll position when the embedded location preview requests focus', async () => {
  setPreferences({ findMapProvider: 'baidu' })
  await mount()
  const button = row(A).querySelector<HTMLButtonElement>('.find-identity-list-main')!
  const frame = host.querySelector<HTMLIFrameElement>('.find-map-frame iframe')!
  const scrollTo = vi.spyOn(window, 'scrollTo').mockImplementation(() => {})
  try {
    button.focus()
    frame.focus()
    expect(document.activeElement).toBe(frame)
    window.dispatchEvent(new Event('blur'))
    expect(document.activeElement).toBe(button)
    window.dispatchEvent(new Event('scroll'))
    expect(scrollTo).toHaveBeenCalledExactlyOnceWith({ left: 0, top: 0, behavior: 'instant' })
    window.dispatchEvent(new Event('scroll'))
    expect(scrollTo).toHaveBeenCalledTimes(1)
  } finally {
    scrollTo.mockRestore()
  }
})

it('draws history with the preferred provider when its key is configured', async () => {
  setPreferences({ findMapProvider: 'amap', findMapKeys: { baidu: '', amap: 'fixture-amap-key', osm: '' } })
  api.trackingMap.mockResolvedValue({ imageDataUrl: 'data:image/png;base64,fixture' })
  await mount()
  const trackButton = Array.from(host.querySelectorAll<HTMLButtonElement>('.find-group-link')).find(button => button.textContent?.includes('轨迹'))!
  await act(async () => trackButton.click())
  expect(api.trackingMap).toHaveBeenCalledWith(A, 'amap', 'fixture-amap-key', 24, undefined, undefined, true)
  expect(host.querySelector('.find-track-map-frame img')?.getAttribute('src')).toBe('data:image/png;base64,fixture')
  expect(host.textContent).not.toContain('轨迹暂用 OSMap')
})
