import type { DeviceAction } from '../types'

export function canRunManualImageOperation(action: DeviceAction) {
  return action === 'idle' || action === 'slideshow'
}
