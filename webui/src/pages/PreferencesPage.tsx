import { useState, useSyncExternalStore } from 'react'
import { Languages, MapPinned, Monitor, Settings2 } from 'lucide-react'
import { PageHeader } from '../components/PageHeader'
import { getThemeMode, setThemeMode, subscribeTheme, type ThemeMode } from '../theme'
import { setPreferences, useLanguage, usePreferences, type FindMapPreference, type LanguageMode } from '../preferences'

export function PreferencesPage() {
  const en = useLanguage() === 'en'
  return <div className="page preferences-page"><PageHeader title={en ? 'Preferences' : '偏好'} /><ClientPreferences /></div>
}

export function ClientPreferences() {
  const preferences = usePreferences()
  const language = useLanguage()
  const theme = useSyncExternalStore(subscribeTheme, getThemeMode, getThemeMode)
  const [unsaved, setUnsaved] = useState(false)
  const en = language === 'en'
  return <>
    <section className="panel browser-preferences">
      <div className="panel-heading"><h2><Settings2 />{en ? 'Preferences' : '偏好'}</h2></div>
      <p>{en ? 'These preferences apply only to this browser and website. They never change or connect your device.' : '这些选项仅影响当前浏览器与网址，自动保存在浏览器中，不连接设备，也不修改设备配置。'}</p>
      <div className="preference-row"><div><h3><Languages />{en ? 'Language' : '语言'}</h3><p>{en ? 'English currently covers navigation, preferences and the tutorial. Detailed device text keeps its original language.' : '英文当前覆盖导航、偏好与教程，设备详情暂保留原文。'}</p></div>
        <select aria-label={en ? 'Language' : '语言'} value={preferences.language} onChange={(event) => setUnsaved(!setPreferences({ language: event.target.value as LanguageMode }))}>
          <option value="browser">{en ? 'Follow browser' : '跟随浏览器'}</option><option value="zh-CN">简体中文</option><option value="en">English</option>
        </select></div>
      <div className="preference-row"><div><h3><MapPinned />{en ? 'Default Find map' : 'Find 默认地图'}</h3><p>{en ? 'Used for current locations and trajectories. System default: Baidu for Chinese, OpenStreetMap for English. Without a key, trajectories use OpenStreetMap.' : '用于当前位置和轨迹。跟随系统时，中文用百度、英文用 OSMap；轨迹未配置 Key 时使用 OSMap。'}</p></div>
        <select aria-label={en ? 'Default Find map' : 'Find 默认地图'} value={preferences.findMapProvider} onChange={(event) => setUnsaved(!setPreferences({ findMapProvider: event.target.value as FindMapPreference }))}>
          <option value="system">{en ? 'Follow system' : '跟随系统'}</option><option value="osm">OpenStreetMap</option><option value="baidu">{en ? 'Baidu Maps' : '百度地图'}</option><option value="amap">{en ? 'AMap' : '高德地图'}</option>
        </select></div>
      <div className="preference-row"><div><h3><Monitor />{en ? 'Appearance' : '外观'}</h3><p>{en ? 'System follows your operating system’s light/dark choice.' : '跟随系统会自动响应系统的亮色或暗色设置。'}</p></div>
        <select aria-label={en ? 'Appearance' : '外观'} value={theme} onChange={(event) => setThemeMode(event.target.value as ThemeMode)}>
          <option value="system">{en ? 'Follow system' : '跟随系统'}</option><option value="light">{en ? 'Light' : '亮色模式'}</option><option value="dark">{en ? 'Dark' : '暗色模式'}</option>
        </select></div>
      <p className="muted">{en ? 'Clearing this site’s browser data resets these preferences. Other hosts or ports have separate settings.' : '清除此网站的浏览器数据会重置偏好；不同主机名或端口的设置相互独立。'}</p>
      {unsaved && <p role="status">{en ? 'Browser storage is unavailable. Changes apply to this session only.' : '浏览器存储不可用，当前选择仅在本次打开期间有效。'}</p>}
    </section>
  </>
}
