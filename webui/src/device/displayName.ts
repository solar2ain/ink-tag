export function displayDeviceName(localAlias: string, bluetoothName: string, fallback = '未命名设备') {
  return localAlias.trim() || bluetoothName.trim() || fallback
}
