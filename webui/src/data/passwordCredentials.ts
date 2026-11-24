/** Origin-local bearer credentials, never part of Find/preferences exports. */
import { parsePasswordQrLink } from '../protocol/devicePassword'
const prefix = 'inktag-device-password-v1:'
let qrCandidate: ReturnType<typeof parsePasswordQrLink> | null = null
const validId = (id: string) => /^[0-9a-fA-F]{16}$/.test(id) && !/^0+$/.test(id)
export function getPasswordCredential(id: string): Uint8Array | null {
  if (!validId(id)) return null
  let value = ''
  try { value = localStorage.getItem(prefix + id.toUpperCase()) ?? '' } catch { return null }
  return /^[0-9a-fA-F]{64}$/.test(value) && !/^0+$/.test(value)
    ? Uint8Array.from(value.match(/../g)!, (v) => parseInt(v, 16)) : null
}
export function savePasswordCredential(id: string, value: Uint8Array | null) {
  if (!validId(id)) throw new Error('设备标识无效。')
  if (value && (value.length !== 32 || !value.some(Boolean))) throw new Error('控制凭据无效。')
  const key = prefix + id.toUpperCase()
  try {
    if (!value) localStorage.removeItem(key)
    else localStorage.setItem(key, Array.from(value, (v) => v.toString(16).padStart(2, '0')).join(''))
    return true
  } catch { return false }
}
export function consumePasswordQrLocation() {
  if (!location.hash.includes('auth=')) return
  const original = location.href
  // Scrub before routing, telemetry, reporting or navigation.
  history.replaceState(null, '', location.pathname + location.search + '#/')
  try { qrCandidate = parsePasswordQrLink(original) } catch { qrCandidate = null }
}
export function takePasswordQrCredential(id: string) {
  if (qrCandidate?.deviceId !== id.toUpperCase()) return null
  const credential = qrCandidate.credential
  qrCandidate = null
  return credential
}
