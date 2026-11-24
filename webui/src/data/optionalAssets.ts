import assets from 'virtual:inktag-optional-assets'

type AssetName = keyof typeof assets
const pending = new Map<AssetName, Promise<string>>()
const registry = () => (globalThis as typeof globalThis & { __inktagOptionalAssets?: Record<string, string> }).__inktagOptionalAssets

/** Optional, same-directory resources. No network service or device data. */
export function loadOptionalAsset(name: AssetName): Promise<string> {
  const { id, path } = assets[name]
  const cached = registry()?.[id]
  if (cached) return Promise.resolve(cached)
  const existing = pending.get(name)
  if (existing) return existing
  const request = new Promise<string>((resolve, reject) => {
    const script = document.createElement('script')
    const finish = (error?: Error) => {
      clearTimeout(timer)
      script.remove()
      const value = registry()?.[id]
      if (error || !value) reject(error ?? new Error(`Optional resource missing: ${name}`))
      else resolve(value)
    }
    const timer = setTimeout(() => finish(new Error(`Optional resource timed out: ${name}`)), 8000)
    script.onload = () => finish()
    script.onerror = () => finish(new Error(`Optional resource unavailable: ${name}`))
    script.src = new URL(path, document.baseURI).href
    document.head.append(script)
  }).catch(error => { pending.delete(name); throw error })
  pending.set(name, request)
  return request
}
