// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { PreferencesPage } from './PreferencesPage'
import { getFindMapProvider, getLanguage, initializePreferences, parsePreferences, PREFERENCES_KEY, resolveBrowserLanguage, setPreferences } from '../preferences'
import { getThemeMode, setThemeMode } from '../theme'

describe('browser-only preferences', () => {
  let host: HTMLDivElement, root: Root, cleanup: (() => void) | undefined
  let values: Map<string, string>
  beforeEach(() => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
    values = new Map()
    vi.stubGlobal('localStorage', { getItem: (key: string) => values.get(key) ?? null, setItem: (key: string, value: string) => values.set(key, value) })
    setPreferences({ language: 'zh-CN', findMapProvider: 'osm', findMapKeys: { baidu: '', amap: '', osm: '' } })
    setThemeMode('system')
    host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  })
  afterEach(async () => {
    cleanup?.(); cleanup = undefined
    await act(async () => root.unmount()); host.remove(); vi.unstubAllGlobals()
  })
  const select = async (label: string, value: string) => act(async () => {
    const node = host.querySelector<HTMLSelectElement>(`select[aria-label="${label}"]`)!
    node.value = value; node.dispatchEvent(new Event('change', { bubbles: true }))
  })
  it('rejects corrupt and unsupported stored values', () => {
    expect(parsePreferences('bad')).toMatchObject({ language: 'browser', findMapProvider: 'system' })
    expect(parsePreferences('{"language":"fr","motion":"invalid"}')).toMatchObject({ language: 'browser', findMapProvider: 'system' })
    expect(parsePreferences('{"language":"en","motion":"full"}')).toMatchObject({ language: 'en' })
  })
  it('respects browser language preference order with English fallback', () => {
    expect(resolveBrowserLanguage(['zh-TW', 'en'])).toBe('zh-CN')
    expect(resolveBrowserLanguage(['en-GB', 'zh-CN'])).toBe('en')
    expect(resolveBrowserLanguage(['fr'])).toBe('en')
  })
  it('saves language, changes rendered controls, and restores after initialization', async () => {
    await act(async () => root.render(<PreferencesPage />))
    expect(host.querySelector('h1')?.textContent).toBe('偏好')
    expect(host.querySelector('h2')?.textContent).toBe('偏好')
    await select('语言', 'en')
    expect(host.querySelector('h1')?.textContent).toBe('Preferences')
    expect(JSON.parse(values.get(PREFERENCES_KEY)!)).toMatchObject({ language: 'en' })
    await act(async () => { cleanup = initializePreferences() })
    expect(getLanguage()).toBe('en')
    expect(document.documentElement.lang).toBe('en')
  })
  it('keeps theme in sync with the existing toolbar preference', async () => {
    await act(async () => root.render(<PreferencesPage />))
    await select('外观', 'dark')
    expect(getThemeMode()).toBe('dark')
    expect(values.get('inktag-theme')).toBe('dark')
    await act(async () => setThemeMode('light'))
    expect(host.querySelector<HTMLSelectElement>('select[aria-label="外观"]')?.value).toBe('light')
  })
  it('shows only the default map preference and retains keys entered in Find', async () => {
    expect(parsePreferences('{"findMapProvider":"invalid","findMapKeys":{"baidu":5}}'))
      .toMatchObject({ findMapProvider: 'system', findMapKeys: { baidu: '', amap: '', osm: '' } })
    await act(async () => root.render(<PreferencesPage />))
    await select('Find 默认地图', 'baidu')
    expect(host.textContent).toContain('轨迹未配置 Key 时使用 OSMap')
    await act(async () => setPreferences({ findMapKeys: { baidu: 'fixture-baidu-key', amap: '', osm: '' } }))
    expect(host.querySelector('input')).toBeNull()
    await select('Find 默认地图', 'amap')
    expect(host.querySelector('input')).toBeNull()
    await act(async () => { cleanup = initializePreferences() })
    expect(JSON.parse(values.get(PREFERENCES_KEY)!)).toMatchObject({ findMapProvider: 'amap', findMapKeys: { baidu: 'fixture-baidu-key' } })
  })
  it('follows system language by default and preserves explicit map choices', async () => {
    expect(parsePreferences(null).findMapProvider).toBe('system')
    vi.stubGlobal('navigator', { languages: ['zh-CN'], language: 'zh-CN' })
    setPreferences({ findMapProvider: 'system' })
    cleanup = initializePreferences()
    expect(getFindMapProvider()).toBe('baidu')
    vi.stubGlobal('navigator', { languages: ['en-US'], language: 'en-US' })
    window.dispatchEvent(new Event('languagechange'))
    expect(getFindMapProvider()).toBe('osm')
    setPreferences({ findMapProvider: 'amap' })
    vi.stubGlobal('navigator', { languages: ['zh-CN'], language: 'zh-CN' })
    window.dispatchEvent(new Event('languagechange'))
    expect(getFindMapProvider()).toBe('amap')
  })
  it('has neither an animation preference nor a duplicate tutorial entry', async () => {
    document.documentElement.dataset.motion = 'full'
    setPreferences({ language: 'zh-CN', findMapProvider: 'osm', findMapKeys: { baidu: '', amap: '', osm: '' } })
    await act(async () => root.render(<PreferencesPage />))
    expect(host.querySelectorAll('select')).toHaveLength(3)
    expect(host.textContent).not.toContain('界面动画')
    expect(document.documentElement.dataset.motion).toBeUndefined()
    expect(Array.from(host.querySelectorAll('button')).some((node) => /教程|tutorial/.test(node.textContent ?? ''))).toBe(false)
    expect(host.querySelector('[role=dialog]')).toBeNull()
  })
  it('storage failure retains the session preference and displays a warning', async () => {
    vi.stubGlobal('localStorage', { getItem: () => { throw new Error('blocked') }, setItem: () => { throw new Error('blocked') } })
    await act(async () => root.render(<PreferencesPage />))
    await select('语言', 'en')
    expect(getLanguage()).toBe('en')
    expect(host.querySelector('[role=status]')?.textContent).toContain('session only')
  })
})
