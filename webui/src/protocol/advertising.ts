export type AdvertisingConfig = { mode: 'always' | 'delayed'; timeoutSeconds: number }

export function encodeAdvertisingConfig(config: AdvertisingConfig): Uint8Array {
  if (!['always', 'delayed'].includes(config.mode) || !Number.isInteger(config.timeoutSeconds) ||
      config.timeoutSeconds < 1 || config.timeoutSeconds > 65535) throw new Error('广播延时需为 1–65535 秒的整数')
  return Uint8Array.of(1, config.mode === 'delayed' ? 1 : 0, config.timeoutSeconds & 255, config.timeoutSeconds >> 8)
}

export function decodeAdvertisingConfig(wire: Uint8Array): AdvertisingConfig {
  if (wire.length !== 4 || wire[0] !== 1 || wire[1] > 1 || !(wire[2] | wire[3])) throw new Error('设备广播设置响应无效')
  return { mode: wire[1] === 1 ? 'delayed' : 'always', timeoutSeconds: wire[2] | wire[3] << 8 }
}
