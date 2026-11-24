import { compactTemperatureSources } from './temperatureSources'

export const voltageRequest = new Uint8Array([0x56])
export const temperatureRequest = new Uint8Array([0x54])

export function telemetryFromMessage(message: string, boardId = '') {
  const telemetry: { voltage?: number; temperature?: number | null; mcuTemperature?: number | null; panelTemperature?: number | null } = {}
  const voltage = message.match(/(?:^|\s)v=(\d+)/)
  if (voltage) {
    const millivolts = Number(voltage[1])
    if (millivolts >= 1500 && millivolts <= 6000) telemetry.voltage = millivolts / 1000
  }
  const temperature = message.match(/(?:^|\s)temp=(-?\d+)(?:\s|$)/)
  if (temperature) {
    const validity = message.match(/(?:^|\s)valid=([01])(?:\s|$)/)
    const celsius = Number(temperature[1])
    if (validity?.[1] === '0' || (celsius >= -128 && celsius <= 127)) {
      telemetry.temperature = validity?.[1] === '0' ? null : celsius
      const compact = compactTemperatureSources(telemetry.temperature, boardId)
      const source = (prefix: string) => {
        const valid = message.match(new RegExp(`(?:^|\\s)${prefix}_valid=([01])(?:\\s|$)`))
        const value = message.match(new RegExp(`(?:^|\\s)${prefix}16=(-?\\d+)(?:\\s|$)`))
        const sixteenths = value ? Number(value[1]) : NaN
        return valid?.[1] === '1' && sixteenths >= -2048 && sixteenths <= 2032 ? sixteenths / 16 : null
      }
      const explicit = /(?:^|\s)(?:die|panel)_valid=/.test(message)
      telemetry.mcuTemperature = explicit ? source('die')
        : /(?:^|\s)source=die(?:\s|$)/.test(message) ? telemetry.temperature : compact.mcu
      telemetry.panelTemperature = explicit ? source('panel') : compact.panel
    }
  }
  return telemetry
}
