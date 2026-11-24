export class BluetoothConnectionTimeoutError extends Error {
  constructor(public readonly phase: string) {
    super(`蓝牙连接超时：${phase}`)
    this.name = 'BluetoothConnectionTimeoutError'
  }
}

export function withConnectionTimeout<T>(
  operation: Promise<T>,
  timeoutMs: number,
  phase: string,
  onTimeout?: () => void,
) {
  return new Promise<T>((resolve, reject) => {
    const timer = globalThis.setTimeout(() => {
      onTimeout?.()
      reject(new BluetoothConnectionTimeoutError(phase))
    }, Math.max(1, timeoutMs))
    operation.then(
      (value) => { globalThis.clearTimeout(timer); resolve(value) },
      (error) => { globalThis.clearTimeout(timer); reject(error) },
    )
  })
}
