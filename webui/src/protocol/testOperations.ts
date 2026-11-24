/** LIST_TESTS is independent of settings schema and broad hardware capabilities. */
export function decodeTestOperations(payload: Uint8Array): number[] {
  if (payload.length < 2 || payload[0] !== 1 || payload[1] !== payload.length - 2) {
    throw new Error('设备返回的测试项目清单无效')
  }
  const operations = Array.from(payload.subarray(2))
  if (new Set(operations).size !== operations.length) throw new Error('设备返回的测试项目清单重复')
  return operations
}
