import { describe, expect, it } from 'vitest'
import { displayDeviceName } from './displayName'

describe('device display name', () => {
  it('prefers the local device name', () => {
    expect(displayDeviceName('客厅日历', 'InkTag_OM_B892')).toBe('客厅日历')
  })

  it('falls back to the Bluetooth name after the local name is cleared', () => {
    expect(displayDeviceName('', 'InkTag_OM_B892')).toBe('InkTag_OM_B892')
    expect(displayDeviceName('   ', 'InkTag_OM_B892')).toBe('InkTag_OM_B892')
  })

  it('uses the requested fallback only when both names are empty', () => {
    expect(displayDeviceName('', '', 'InkTag')).toBe('InkTag')
  })
})
