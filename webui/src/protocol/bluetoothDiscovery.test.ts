import { describe, expect, it } from 'vitest'
import { buildBluetoothRequestOptions, compareInkTagDeviceNames, isKnownInkTagName } from './bluetoothDiscovery'

const services = ['inktag-service']

describe('Bluetooth discovery', () => {
  it('shows every nearby device when no product filter is requested', () => {
    expect(buildBluetoothRequestOptions(services)).toEqual({
      acceptAllDevices: true,
      optionalServices: services,
    })
  })

  it('uses known InkTag prefixes only when the user enables the filter', () => {
    const options = buildBluetoothRequestOptions(services, { onlyInkTag: true })
    expect(options.acceptAllDevices).toBeUndefined()
    expect(options.filters).toEqual([{ namePrefix: 'IKT_' }, { namePrefix: 'InkTag_' }])
    expect(options.optionalServices).toEqual(services)
  })

  it('lets an explicit name search take precedence over the preset filter', () => {
    expect(buildBluetoothRequestOptions(services, { onlyInkTag: true, namePrefix: 'Desk' }).filters).toEqual([
      { namePrefix: 'IKT_Desk' },
      { namePrefix: 'InkTag_Desk' },
    ])
    expect(isKnownInkTagName('IKT_OM_1234')).toBe(true)
    expect(isKnownInkTagName('InkTag_OM_1234')).toBe(true)
    expect(isKnownInkTagName('OM_1234')).toBe(false)
  })

  it('keeps a raw search prefix when the product filter is disabled', () => {
    expect(buildBluetoothRequestOptions(services, { onlyInkTag: false, namePrefix: 'Sensor' }).filters).toEqual([
      { namePrefix: 'Sensor' },
    ])
  })

  it('keeps other InkTag devices visible while preferring a remembered name', () => {
    expect(buildBluetoothRequestOptions(services, {
      onlyInkTag: true,
      preferredName: 'InkTag_TL_9903',
    }).filters).toEqual([
      { name: 'InkTag_TL_9903' },
      { namePrefix: 'IKT_' },
      { namePrefix: 'InkTag_' },
    ])
  })

  it('prioritizes InkTag devices without changing the order inside each group', () => {
    expect(['Sensor_A', 'InkTag_OM_2', 'IKT_GR_1', 'Beacon'].sort(compareInkTagDeviceNames)).toEqual([
      'InkTag_OM_2', 'IKT_GR_1', 'Sensor_A', 'Beacon',
    ])
  })
})
