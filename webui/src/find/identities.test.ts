import { describe, expect, it } from 'vitest'
import type { FindIdentity } from './client'
import {
  compactFindFingerprint,
  compactFindBluetoothName,
  distinctFindIdentityNote,
  findDeviceDisplayName,
  findIdentityBelongsToDevice,
  findIdentityDisplayName,
  findIdentityUsage,
  groupFindIdentities,
  identitiesForDevice,
  selectedFindIdentity,
  stableFindDeviceId,
} from './identities'

function identity(keyFingerprint: string, deviceId: string, createdAt: string, advertisementKey = keyFingerprint): FindIdentity {
  return {
    keyFingerprint, deviceId, advertisementKey, createdAt,
    address: '', deviceName: '', bluetoothName: '', findNote: '', trackingEnabled: false,
    lastAutoQueryAt: null, lastAutoError: null, trackPointCount: 0,
    lastQueriedAt: null, lastReportAt: null, selected: false,
    derivationVersion: 1, periodSeconds: 900, currentSlot: 0,
  }
}

describe('Find physical device identity', () => {
  it('shortens current and installed-legacy factory prefixes without renaming custom labels', () => {
    expect(compactFindBluetoothName('IKT_GR_42D8')).toBe('GR_42D8')
    expect(compactFindBluetoothName('InkTag_GR_42D8')).toBe('GR_42D8')
    expect(compactFindBluetoothName('Desk IKT_42D8')).toBe('Desk IKT_42D8')
  })
  it('prefers the firmware activation identity over host-specific Bluetooth ids', () => {
    expect(stableFindDeviceId({ activationId: '0123456789abcdef', bleAddress: 'AA:BB', browserDeviceId: 'browser' }))
      .toBe('activation:0123456789ABCDEF')
    expect(stableFindDeviceId({ bleAddress: 'aa:bb' })).toBe('ble:AA:BB')
  })

  it('keeps rotated keys under one physical device and chooses the newest for reuse', () => {
    const items = [
      identity('old', 'activation:DEVICE', '2026-08-01T00:00:00Z'),
      identity('other', 'activation:OTHER', '2026-09-01T00:00:00Z'),
      identity('new', 'activation:DEVICE', '2026-09-02T00:00:00Z'),
    ]
    expect(identitiesForDevice(items, 'activation:DEVICE').map((item) => item.keyFingerprint)).toEqual(['new', 'old'])
    const groups = groupFindIdentities(items, 'activation:DEVICE')
    expect(groups[0].current).toBe(true)
    expect(groups[0].identities.map((item) => item.keyFingerprint)).toEqual(['new', 'old'])
    expect(findIdentityUsage(groups[0].identities, 'new')).toBe('current')
    expect(findIdentityUsage(groups[0].identities, 'old')).toBe('history')
    expect(groups).toHaveLength(2)
  })

  it('does not confuse the selected query identity or tracking toggle with the device identity in use', () => {
    const old = { ...identity('old', 'activation:DEVICE', '2026-08-01T00:00:00Z'), selected: true, trackingEnabled: true }
    const current = { ...identity('new', 'activation:DEVICE', '2026-09-02T00:00:00Z'), selected: false, trackingEnabled: false }
    expect(findIdentityUsage([old, current], 'new')).toBe('current')
    expect(findIdentityUsage([old, current], 'old')).toBe('history')
  })

  it('distinguishes the selected query identity from the connected device identity', () => {
    const connected = { ...identity('connected', 'activation:DEVICE', '2026-09-02T00:00:00Z', 'DEVICE_PUBLIC'), selected: false }
    const selected = { ...identity('selected', 'activation:OTHER', '2026-09-03T00:00:00Z', 'OTHER_PUBLIC'), selected: true }
    expect(selectedFindIdentity([connected, selected])).toBe(selected)
    expect(findIdentityBelongsToDevice(selected, 'activation:DEVICE', 'DEVICE_PUBLIC')).toBe(false)
    expect(findIdentityBelongsToDevice(connected, 'activation:DEVICE', '')).toBe(true)
    expect(findIdentityBelongsToDevice(connected, '', 'DEVICE_PUBLIC')).toBe(true)
  })

  it('uses one stable device heading and removes repeated names from identity rows', () => {
    const item = {
      ...identity('a3a4a3d1e1b09606', 'activation:DEVICE', '2026-09-02T00:00:00Z'),
      deviceName: '书房价签',
      bluetoothName: 'InkTag_GR_42D8',
      findNote: '书房价签',
    }
    expect(compactFindBluetoothName(item.bluetoothName)).toBe('GR_42D8')
    expect(findDeviceDisplayName([item])).toBe('书房价签')
    expect(distinctFindIdentityNote(item, '书房价签')).toBe('')
    expect(findIdentityDisplayName(item)).toBe('书房价签')

    const bleOnly = { ...item, deviceName: '', findNote: '' }
    expect(findDeviceDisplayName([bleOnly])).toBe('GR_42D8')

    const named = { ...item, findNote: '书房', deviceName: '桌面价签' }
    expect(findDeviceDisplayName([named])).toBe('书房')
  })

  it('shortens identity fingerprints while preserving enough digits to distinguish them', () => {
    expect(compactFindFingerprint('333c0a053ea3ef7e6180d546cef458e0')).toBe('333c0a05…58e0')
    expect(compactFindFingerprint('1234567890abcdef')).toBe('12345678…cdef')
    expect(compactFindFingerprint('12345678')).toBe('12345678')
  })
})
