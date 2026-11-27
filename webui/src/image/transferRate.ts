export function formatImageRate(bytesPerSecond: number) {
  return `${(bytesPerSecond / 1000).toFixed(2)} KB/s`
}
