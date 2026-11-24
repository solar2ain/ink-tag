import { expect, it } from 'vitest'
import { findDeviceStatus } from './deviceStatus'
const ready = { connected: true, reporting: true, supported: true, publicOnly: true, pro: true, active: true, enabled: false }
it.each([
  [{}, '可开启', true], [{ enabled: true }, '广播已开启', true],
  [{ pro: false }, '不可用，请先激活 Pro', false], [{ connected: false }, '请先连接设备', false],
  [{ reporting: false }, '正在读取设备状态', false], [{ supported: false, pro: false }, '设备不支持 Find', false],
  [{ publicOnly: false }, '请更新设备固件', false], [{ active: false }, '正在读取 Pro 状态', false],
])('distinguishes eligibility and operational state %j', (patch, label, canEnable) => {
  expect(findDeviceStatus({ ...ready, ...patch })).toEqual({ label, canEnable })
})
