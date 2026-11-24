import { describe, expect, it } from 'vitest'
import { boardProfiles, resolveBoardLedCapability } from './boards'
import {
  BLUETOOTH_NAME_MAX_BYTES, bluetoothNameBytes, decodeButtonConfig, decodeDeviceSettingsCapabilities,
  decodeReportedButtonSettings, decodeReportedDeviceLabel, decodeReportedLedSettings, decodeReportedNfcSettings, decodeReportedNfcUrl,
  defaultAuxIoSettings, defaultButtonBehaviorSettings, defaultLedSettings, defaultNfcTapSettings, deviceInputActionIds, encodeBluetoothName, encodeInputAction, encodeLedSettings, validateAuxIoSettings,
  deviceLabelBytes, encodeDeviceLabel, inputActionMask, NFC_CONTROL_URL_MAX_BYTES,
  nfcControlUrlBytes, normalizeBluetoothName, validateBluetoothName, validateDeviceLabel,
  validateNfcControlUrl, calendarDateToTargetDay, decodeDisplaySettings, encodeDisplaySettings,
} from './deviceSettings'

describe('NFC control URL contract', () => {
  it('separates present-but-unqualified NFC hardware from no-NFC boards', () => {
    expect(boardProfiles.om6626_zkc21b.capabilities.nfc).toBe('hardware')
    expect(boardProfiles.om6626_zkc42b.capabilities.nfc).toBe('hardware')
    expect(boardProfiles.gr551x_zkc42vm.capabilities.nfc).toBe('none')
  })

  it('exposes the qualified OM6626 image-store layout to the WebUI', () => {
    expect(boardProfiles.om6626_zkc21b.capabilities.imageStore).toBe(true)
    expect(boardProfiles.om6626_zkc42b.capabilities.imageStore).toBe(true)
  })

  it('keeps exact-board LED hardware separate from indicator configuration support', () => {
    expect(resolveBoardLedCapability('gr551x_zkc42v', false)).toBe('rgb')
    expect(resolveBoardLedCapability('gr551x_zkc42vm', true)).toBe('none')
    expect(resolveBoardLedCapability('unknown-board', true)).toBe('rgb')
    expect(resolveBoardLedCapability('unknown-board', false)).toBe('none')
  })

  it('keeps ZKC42VM expansion roles off until explicitly enabled', () => {
    expect(defaultAuxIoSettings().buttonGpio).toBe(255)
    expect(boardProfiles.gr551x_zkc42vm.auxIoDefaults).toEqual({
      buttonGpio: 26,
      buttonActiveLow: true,
      buttonPull: 'up',
      reserved: [0, 255, 255, 208, 7],
    })
  })

  it.each([
    'https://example.com/#/nfc/device-1',
    'http://inktag.local/device',
    'http://10.0.0.2/device',
    'http://172.31.255.1/device',
    'http://192.168.1.8:8080/device',
    'http://[fd00::12]/device',
  ])('accepts HTTPS and local HTTP: %s', (url) => {
    expect(validateNfcControlUrl(url)).toBe('')
  })

  it.each([
    'http://example.com/device',
    'http://localhost:4173/#/nfc/device-1',
    'http://127.0.0.1/device',
    'http://[::1]/device',
    'ftp://192.168.1.8/device',
    'https://user:password@example.com/device',
    'not a URL',
  ])('rejects unsafe or invalid URLs: %s', (url) => {
    expect(validateNfcControlUrl(url)).not.toBe('')
  })

  it('measures UTF-8 bytes and enforces the fixed payload limit', () => {
    expect(nfcControlUrlBytes('测')).toBe(3)
    const prefix = 'https://example.com/'
    const exact = prefix + 'a'.repeat(NFC_CONTROL_URL_MAX_BYTES - prefix.length)
    expect(nfcControlUrlBytes(exact)).toBe(NFC_CONTROL_URL_MAX_BYTES)
    expect(validateNfcControlUrl(exact)).toBe('')
    expect(validateNfcControlUrl(`${exact}a`)).toContain(String(NFC_CONTROL_URL_MAX_BYTES))
  })
})

describe('device settings protocol v1', () => {
  it('round-trips the shared calendar and clock display settings v1', () => {
    const payload = encodeDisplaySettings({
      weekStart: 1,
      bootRefreshEnabled: true,
      brandVisible: false,
      countdownTargetDay: calendarDateToTargetDay('2026-10-01'),
      countdownName: '国庆',
    })
    expect(Array.from(payload, (byte) => byte.toString(16).padStart(2, '0')).join(''))
      .toBe('02010111f750000006e59bbde5ba86')
    expect(decodeDisplaySettings(payload)).toMatchObject({
      version: 1,
      supported: true,
      complete: true,
      weekStart: 1,
      bootRefreshEnabled: true,
      brandVisible: false,
      countdownName: '国庆',
      countdownDate: '2026-10-01',
    })
  })

  it('does not treat the unreleased short v3 record as writable configuration', () => {
    expect(decodeDisplaySettings(new Uint8Array([
      2, 3, 1, 0,
    ]))).toBeNull()
  })

  it('keeps stable action IDs shared by NFC and buttons', () => {
    expect(deviceInputActionIds).toEqual({
      disabled: 0,
      nextPage: 1,
      showSlot: 2,
      showDeviceInfo: 3,
      showCalendar: 4,
      showClock: 5,
      toggleSlideshow: 6,
      stop: 7,
      controlUrl: 8,
      toggleClearPrevious: 9,
      previousPage: 10,
    })
    expect(inputActionMask('showClock')).toBe(1 << 5)
  })

  it('encodes a bounded ASCII advertising name with a fixed product prefix', () => {
    expect(BLUETOOTH_NAME_MAX_BYTES).toBe(19)
    expect(bluetoothNameBytes('墨水屏')).toBe(9)
    expect(validateBluetoothName('IKT_Desk', 19)).toBe('')
    expect(validateBluetoothName('InkTag_Desk', 19)).toBe('')
    for (const name of ['IKT_客厅', 'InkTag_客厅', 'IKT_〇', 'IKT_é']) {
      expect(validateBluetoothName(name, 19)).toContain('只支持')
      expect(() => encodeBluetoothName(name)).toThrow('只支持')
    }
    expect(validateBluetoothName('InkTag_🏠', 19)).toContain('只支持')
    expect(validateBluetoothName('InkTag_客 厅', 19)).toContain('只支持')
    expect(validateBluetoothName('InkTag_客\u200d厅', 19)).toContain('只支持')
    expect(validateBluetoothName('OM_Desk', 19)).toContain('IKT_')
    expect(validateBluetoothName('InkTag_' + 'a'.repeat(13))).toContain('19')
    expect(normalizeBluetoothName('Desk')).toBe('IKT_Desk')
    expect(Array.from(encodeBluetoothName('IKT_Desk'))).toEqual([
      1, 8, 73, 75, 84, 95, 68, 101, 115, 107,
    ])
  })

  it('writes only printable ASCII labels, allows clearing and reads legacy UTF-8', () => {
    expect(deviceLabelBytes('客厅日历')).toBe(12)
    expect(validateDeviceLabel('InkTag 3A57')).toBe('')
    expect(Array.from(encodeDeviceLabel('Desk'))).toEqual([1, 4, 68, 101, 115, 107])
    expect(validateDeviceLabel('A'.repeat(24))).toBe('')
    expect(validateDeviceLabel('A'.repeat(25))).toContain('24')
    for (const label of ['客厅', 'café', 'A\u007f', 'A\nB']) {
      expect(validateDeviceLabel(label)).not.toBe('')
      expect(() => encodeDeviceLabel(label)).toThrow()
    }
    expect(Array.from(encodeDeviceLabel(''))).toEqual([1, 0])
    expect(decodeReportedDeviceLabel('settings:device name=%E5%AE%A2%E5%8E%85')).toEqual({ label: '客厅' })
    expect(decodeReportedDeviceLabel('settings:device name=')).toEqual({ label: '' })
  })

  it('uses one versioned input-action frame for NFC and buttons', () => {
    expect(Array.from(encodeInputAction('nfc', {
      action: 'showClock', slotId: 0, controlUrl: '',
    }))).toEqual([1, 1, 5, 0])
    expect(Array.from(encodeInputAction('single', {
      action: 'showSlot', slotId: 7,
    }))).toEqual([1, 2, 2, 7])
    expect(Array.from(encodeInputAction('double', {
      action: 'disabled', slotId: 0,
    }))).toEqual([1, 3, 0, 0])
    expect(Array.from(encodeInputAction('long', {
      action: 'toggleClearPrevious', slotId: 0,
    }))).toEqual([1, 4, 9, 0])
    expect(() => encodeInputAction('single', {
      action: 'controlUrl', slotId: 0, controlUrl: 'https://example.com',
    })).toThrow('实体按键不支持控制 URL')
    expect(() => encodeInputAction('nfc', {
      action: 'toggleClearPrevious', slotId: 0,
    })).toThrow('NFC 不支持')
    expect(() => encodeInputAction('nfc', {
      action: 'showClock', slotId: 0, controlUrl: 'https://example.com/device',
    })).toThrow('不支持自定义控制 URL')
    expect(() => encodeInputAction('nfc', {
      action: 'controlUrl', slotId: 0, controlUrl: '',
    })).toThrow('不支持自定义控制 URL')
  })

  it('keeps the automatic device URL separate from the NFC input binding', () => {
    expect(defaultNfcTapSettings()).toEqual({
      action: 'disabled', slotId: 0, controlUrl: '',
    })
  })

  it('keeps the product defaults for the three button gestures', () => {
    expect(defaultButtonBehaviorSettings()).toEqual({
      single: { action: 'nextPage', slotId: 0 },
      double: { action: 'previousPage', slotId: 0 },
      long: { action: 'toggleClearPrevious', slotId: 0 },
    })
  })

  it('decodes device-reported feature and action masks', () => {
    expect(decodeDeviceSettingsCapabilities(
      'caps: v=1 features=0xfd name=19 device=24 nfc=0x1ff button=0x2ff gestures=0x07 device-info=0x07 drivers=10,11,13,20,23',
    )).toEqual({
      protocolVersion: 1,
      screenDriverIds: [0x10, 0x11, 0x13, 0x20, 0x23],
      bluetoothName: true,
      bluetoothNameMaxBytes: 19,
      deviceLabel: true,
      deviceLabelMaxBytes: 24,
      nfcActionMask: 0x1ff,
      nfcNdef: true,
      buttonActionMask: 0x2ff,
      buttonGestureMask: 0x07,
      nativeDeviceInfo: true,
      deviceInfoQr: true,
      deviceInfoLayoutMask: 0x07,
      customButtonGpio: true,
      reservedIo: true,
      statusLedSettings: true,
      runtimePinConfig: false,
      displayActionMask: 0,
      lifecycle: false,
      experimentalTimedSleep: false,
      experimentalFind: false,
      guiAssetUpdate: false,
    })
    expect(decodeDeviceSettingsCapabilities(
      'caps: v=1 features=0x87 name=19 device=24 nfc=0x0ff ndef=0 button=0 gestures=0 device-info=0x07',
    )?.nfcNdef).toBe(false)
    expect(decodeDeviceSettingsCapabilities(
      'caps: v=1 features=4 deviceInfo=7',
    )?.screenDriverIds).toBeNull()
    expect(decodeDeviceSettingsCapabilities('board=om6626_zkc42b')).toBeNull()
  })

  it('validates button GPIOs with opaque reserved bytes', () => {
    expect(validateAuxIoSettings({ ...defaultAuxIoSettings(), buttonGpio: 17,
      reserved: [4, 18, 19, 96, 9] })).toBe('')
  })

  it('decodes V2 button and auxiliary IO blocks', () => {
    expect(decodeButtonConfig(new Uint8Array([
      1, 7, 0xff, 0x06, 1, 0, 10, 0, 9, 0,
    ]))?.settings).toEqual(defaultButtonBehaviorSettings())
    expect(decodeButtonConfig(new Uint8Array([
      1, 7, 0xff, 0x06, 1, 0, 11, 0, 9, 0,
    ]))).toBeNull()
    expect(decodeButtonConfig(new Uint8Array([
      1, 7, 0xff, 0x02, 1, 0, 9, 0, 6, 0,
    ]))).toEqual({
      gestureMask: 7,
      actionMask: 0x02ff,
      settings: {
        single: { action: 'nextPage', slotId: 0 },
        double: { action: 'toggleClearPrevious', slotId: 0 },
        long: { action: 'toggleSlideshow', slotId: 0 },
      },
      nfcActionMask: 0,
      nfcSettings: null,
    })
    expect(decodeButtonConfig(new Uint8Array([
      1, 7, 0xff, 0x02, 1, 0, 9, 0, 6, 0,
      0xff, 0x00, 4, 0,
    ]))).toEqual({
      gestureMask: 7,
      actionMask: 0x02ff,
      settings: {
        single: { action: 'nextPage', slotId: 0 },
        double: { action: 'toggleClearPrevious', slotId: 0 },
        long: { action: 'toggleSlideshow', slotId: 0 },
      },
      nfcActionMask: 0x00ff,
      nfcSettings: { action: 'showCalendar', slotId: 0, controlUrl: '' },
    })
  })

  it('keeps status policy to the three fixed presets in a two-byte payload', () => {
    expect(Array.from(encodeLedSettings(defaultLedSettings()))).toEqual([1, 1])
    expect(decodeReportedLedSettings('settings:led policy=1')).toEqual(defaultLedSettings())
    expect(decodeReportedLedSettings(
      'settings:led policy=1 mask=0x000 scenes=3f002a141c141c3231113f',
    )).toBeNull()
  })

  it('decodes button, NFC and auxiliary settings returned from device Flash', () => {
    expect(decodeReportedButtonSettings(
      'settings:button single=1,0 double=0,0 long=9,0',
    )).toEqual({
      single: { action: 'nextPage', slotId: 0 },
      double: { action: 'disabled', slotId: 0 },
      long: { action: 'toggleClearPrevious', slotId: 0 },
    })
    expect(decodeReportedNfcSettings(
      'nfc:status present=1 action=calendar slot=0 url_bytes=0 ndef_write=1 uid=0011223344556677',
    )).toEqual({ action: 'showCalendar', slotId: 0, controlUrl: '' })
    expect(decodeReportedNfcSettings(
      'nfc:status present=1 action=calendar slot=0 url_bytes=26 ndef_write=1 uid=0011223344556677',
      'https://example.com/device',
    )).toEqual({ action: 'showCalendar', slotId: 0, controlUrl: 'https://example.com/device' })
    expect(decodeReportedNfcSettings(
      'nfc:status present=1 action=url slot=2 url_bytes=24 ndef_write=1 uid=0011223344556677',
      'https://example.com/device',
    )).toEqual({ action: 'controlUrl', slotId: 2, controlUrl: 'https://example.com/device' })
    expect(decodeReportedNfcUrl(
      'settings:nfc-url=https://192.168.1.8:8443/#/device?name=InkTag',
    )).toBe('https://192.168.1.8:8443/#/device?name=InkTag')
  })

  it('rejects malformed device-reported settings', () => {
    expect(decodeReportedButtonSettings('settings:button single=8,0 double=0,0 long=9,0')).toBeNull()
    expect(decodeReportedNfcSettings('nfc:status present=1 action=unknown slot=0')).toBeNull()
    expect(decodeReportedNfcUrl('settings:nfc-url=http://example.com/device')).toBeNull()
  })

  it('rejects malformed button GPIO and reserved-byte arrays before transport', () => {
    expect(validateAuxIoSettings({ ...defaultAuxIoSettings(), buttonGpio: 256 })).toContain('GPIO')
    expect(validateAuxIoSettings({ ...defaultAuxIoSettings(), reserved: [0, 256, 0, 0, 0] })).toContain('预留')
  })
})
