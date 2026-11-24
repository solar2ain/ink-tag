export type ThemeMode = 'system' | 'light' | 'dark'

const STORAGE_KEY = 'inktag-theme'
const listeners = new Set<() => void>()
let mode: ThemeMode = 'system'
let systemDark = false

function readStoredMode() {
  try {
    return window.localStorage.getItem(STORAGE_KEY)
  } catch {
    return null
  }
}

function storeMode(next: ThemeMode) {
  try {
    window.localStorage.setItem(STORAGE_KEY, next)
  } catch {
    // Theme selection still applies for this session when storage is unavailable.
  }
}

export function parseThemeMode(value: string | null): ThemeMode {
  return value === 'light' || value === 'dark' ? value : 'system'
}

export function resolveTheme(theme: ThemeMode, prefersDark: boolean) {
  return theme === 'system' ? (prefersDark ? 'dark' : 'light') : theme
}

function applyTheme() {
  document.documentElement.dataset.theme = mode
  document.documentElement.dataset.resolvedTheme = resolveTheme(mode, systemDark)
}

export function initializeTheme() {
  const media = window.matchMedia('(prefers-color-scheme: dark)')
  mode = parseThemeMode(readStoredMode())
  systemDark = media.matches
  applyTheme()
  media.addEventListener('change', (event) => {
    systemDark = event.matches
    if (mode === 'system') {
      applyTheme()
      listeners.forEach((listener) => listener())
    }
  })
}

export function getThemeMode() { return mode }
export function subscribeTheme(listener: () => void) {
  listeners.add(listener)
  return () => listeners.delete(listener)
}

export function setThemeMode(next: ThemeMode) {
  mode = next
  storeMode(next)
  applyTheme()
  listeners.forEach((listener) => listener())
}
