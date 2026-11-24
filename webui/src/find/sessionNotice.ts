const KEY = 'inktag.find.restored-notice.v1'
let shown = false

// Only a UI acknowledgement, with no account or authentication data stored.
export function claimRestoredSessionNotice(): boolean {
  if (shown) return false
  shown = true
  try {
    if (window.sessionStorage.getItem(KEY) === '1') return false
    window.sessionStorage.setItem(KEY, '1')
  } catch { /* Restricted storage still gets one notice per page lifetime. */ }
  return true
}
