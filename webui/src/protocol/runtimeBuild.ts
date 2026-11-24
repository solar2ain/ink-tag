export interface RuntimeBuildState {
  build: number
  appBytes: number
  swapResult: number
  swapState: number
  swapGeneration: number
  startupConfirm: number
}

/** OM's schema-1 tail follows the three bounded DEVICE_INFO strings. */
export function parseRuntimeBuildInfo(payload: Uint8Array): RuntimeBuildState {
  const end = 23 + (payload[1] ?? 0) + (payload[2] ?? 0) + (payload[3] ?? 0)
  if (payload.length < 23 || payload[0] !== 1 || end + 16 > payload.length || payload[end] !== 1) {
    throw new Error('设备未提供有效的 v1 Recovery 状态，不能确认更新结果')
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  return {
    build: view.getUint32(end + 4, true),
    appBytes: view.getUint32(15, true),
    swapResult: payload[end + 1]!,
    swapState: payload[end + 2]!,
    swapGeneration: view.getUint32(end + 8, true),
    startupConfirm: view.getUint32(end + 12, true),
  }
}

export function assertOnMicroOtaBaseline(state: RuntimeBuildState) {
  if (![0, 4].includes(state.swapState)) {
    throw new Error(`设备当前需要固定 Recovery 处理（state=${state.swapState}），请重连 _OTA 设备后继续升级`)
  }
  return state.swapGeneration
}

export function assertOnMicroOtaAttestation(
  state: RuntimeBuildState,
  expectedBuild: number,
  expectedAppBytes: number,
  previousGeneration?: number,
) {
  if (state.build !== expectedBuild || state.appBytes !== expectedAppBytes) {
    throw new Error(
      `设备重连后运行的固件与升级包不一致（build=${state.build}, app_bytes=${state.appBytes}；` +
      `期望 build=${expectedBuild}, app_bytes=${expectedAppBytes}）`,
    )
  }
  if (state.swapState !== 4 || state.swapGeneration < 1) {
    throw new Error(
      `升级包已传完，但设备没有完成单应用试运行确认（state=${state.swapState}, ` +
      `gen=${state.swapGeneration}），不能判定升级成功`,
    )
  }
  if (previousGeneration !== undefined && state.swapGeneration <= previousGeneration) {
    throw new Error(
      `设备已重连，但本次升级没有推进 Recovery 代次（gen=${state.swapGeneration}, ` +
      `升级前=${previousGeneration}），不能判定升级成功`,
    )
  }
}
