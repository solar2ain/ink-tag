import { Monitor, Moon, Sun } from 'lucide-react'
import { useSyncExternalStore } from 'react'
import { getThemeMode, setThemeMode, subscribeTheme, type ThemeMode } from '../theme'
import { useLanguage } from '../preferences'

const modes: ThemeMode[] = ['system', 'light', 'dark']
const labels = { system: '跟随系统', light: '浅色模式', dark: '深色模式' }
const icons = { system: Monitor, light: Sun, dark: Moon }

export function ThemeSwitcher({ className = '' }: { className?: string }) {
  const en = useLanguage() === 'en'
  const mode = useSyncExternalStore(subscribeTheme, getThemeMode, getThemeMode)
  const Icon = icons[mode]
  const next = modes[(modes.indexOf(mode) + 1) % modes.length]
  const names = en ? { system: 'System', light: 'Light', dark: 'Dark' } : labels
  return <button className={`theme-switcher icon-button ${className}`} title={en ? `Theme: ${names[mode]}; switch to ${names[next]}` : `主题：${names[mode]}；点击切换到${names[next]}`} aria-label={en ? `Theme: ${names[mode]}` : `主题：${names[mode]}`} onClick={() => setThemeMode(next)}><Icon /></button>
}
