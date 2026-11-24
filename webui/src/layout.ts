type NavigatorHints = Pick<Navigator, 'userAgent' | 'platform' | 'maxTouchPoints'> & {
  userAgentData?: { mobile?: boolean }
}

export const MOBILE_LAYOUT_QUERY = '(max-width: 799px)'

export function isMobileDevice(hints: NavigatorHints) {
  if (typeof hints.userAgentData?.mobile === 'boolean') return hints.userAgentData.mobile
  if (/Android|iPhone|iPod|IEMobile|Opera Mini|Mobile/i.test(hints.userAgent)) return true
  return hints.platform === 'MacIntel' && hints.maxTouchPoints > 1
}

export function shouldUseMobileLayout(hints: NavigatorHints, narrowViewport: boolean) {
  return isMobileDevice(hints) || narrowViewport
}

export function initializeLayoutMode() {
  const narrow = window.matchMedia(MOBILE_LAYOUT_QUERY)
  const apply = () => document.documentElement.classList.toggle(
    'mobile-layout',
    shouldUseMobileLayout(navigator, narrow.matches),
  )
  apply()
  narrow.addEventListener('change', apply)
}
