/** Only reported device capabilities establish support; never infer it from a model name. */
export function findDeviceStatus(input: {
  connected: boolean; reporting: boolean; supported: boolean; publicOnly: boolean
  pro: boolean; active: boolean; enabled: boolean
}) {
  if (!input.connected) return { label: '请先连接设备', canEnable: false }
  if (!input.reporting) return { label: '正在读取设备状态', canEnable: false }
  if (!input.supported) return { label: '设备不支持 Find', canEnable: false }
  if (!input.publicOnly) return { label: '请更新设备固件', canEnable: false }
  if (!input.active) return { label: '正在读取 Pro 状态', canEnable: false }
  if (!input.pro) return { label: '不可用，请先激活 Pro', canEnable: false }
  return { label: input.enabled ? '广播已开启' : '可开启', canEnable: true }
}
