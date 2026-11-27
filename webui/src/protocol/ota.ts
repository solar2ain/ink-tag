import installNordic from '../ota-drivers/nordic_dfu.js?ota-adapter'
import installSignedPreflight from '../ota-drivers/signed_preflight.js?ota-adapter'
import installRejection from '../ota-drivers/ota_rejection.js?ota-adapter'
import installGoodix from '../ota-drivers/goodix_ota.js?ota-adapter'
import installOnMicro from '../ota-drivers/onmicro_ota.js?ota-adapter'
import installDialog from '../ota-drivers/dialog_suota.js?ota-adapter'
import installTelink from '../ota-drivers/telink_ota.js?ota-adapter'
import { inktagClient, ProtocolV2Error, type RuntimeImageInfo } from './client'
import type { DeviceCapabilities } from '../types'
import { useDeviceStore } from '../store/device'
import { PROTOCOL_V2_VERSION, StatusV2 } from './generatedCommandsV2'
import {
  assertOnMicroOtaAttestation,
  assertOnMicroOtaBaseline,
} from './runtimeBuild'
import { canonicalBoardId, firmwareFamilyProfile, TLSR_FIRMWARE_FAMILY } from './boards'

type OtaType = DeviceCapabilities['ota']
type OtaAdapter = Record<string, (...args: any[]) => any>

declare global {
  interface Window {
    NordicDfu?: OtaAdapter
    GoodixOta?: OtaAdapter
    OnMicroOta?: OtaAdapter
    DialogSuota?: OtaAdapter
    TelinkOta?: OtaAdapter
  }
}

export interface ParsedOtaPackage {
  type: Exclude<OtaType, 'none'>
  payload: any
  summary: string
}

export interface OtaProgress {
  percent: number
  message: string
}

export async function syncClockAfterVerifiedOta(
  sync: () => Promise<void> = async () => {
    await inktagClient.waitForPasswordAuthorization()
    await inktagClient.syncTime()
  },
) {
  try {
    await sync()
    return null
  } catch (error) {
    return error instanceof Error ? error.message : String(error)
  }
}

export function assertDialogOtaAttestation(
  runtime: RuntimeImageInfo | null,
  payload: { codeSize: number; crc32: number },
) {
  if (!runtime || runtime.appBytes !== payload.codeSize || runtime.checksum !== payload.crc32) {
    throw new Error(
      `DA14585 重连后的应用与升级包不一致（app_bytes=${runtime?.appBytes ?? 0}, ` +
      `crc32=${runtime?.checksum ?? 0}；期望 app_bytes=${payload.codeSize}, ` +
      `crc32=${payload.crc32}）`,
    )
  }
}

export function assertGoodixOtaAttestation(
  runtime: RuntimeImageInfo | null,
  payload: { manifest: any; application: any },
) {
  const expectedEpoch = Number(payload.manifest.compatibilityEpoch)
  const expectedBytes = Number(payload.application.size)
  const expectedChecksum = Number(payload.application.checksum)
  if (!runtime || !Number.isInteger(expectedBytes) || expectedBytes <= 0 || runtime.version !== expectedEpoch ||
      runtime.appBytes !== expectedBytes || runtime.checksum !== expectedChecksum) {
    throw new Error(
      `GR 重连后的应用与升级包不一致（epoch=${runtime?.version ?? -1}, ` +
      `app_bytes=${runtime?.appBytes ?? 0}, checksum=${runtime?.checksum ?? 0}；` +
      `期望 epoch=${expectedEpoch}, app_bytes=${expectedBytes}, checksum=${expectedChecksum}）`,
    )
  }
}

function loadAdapters() {
  if (!('InkTagSignedOta' in window)) installSignedPreflight(window)
  if (!('InkTagOtaRejection' in window)) installRejection(window)
  if (!window.NordicDfu) installNordic(window)
  if (!window.GoodixOta) installGoodix(window)
  if (!window.OnMicroOta) installOnMicro(window)
  if (!window.DialogSuota) installDialog(window)
  if (!window.TelinkOta) installTelink(window)
}

export function otaFileHint(type: OtaType) {
  if (type === 'nordic') return 'Nordic application-only OTA ZIP；未签名包仅适用于允许它的开源 nRF52811 Recovery'
  if (type === 'goodix') return '与当前 GR 主板匹配并带发布签名的 InkTag OTA ZIP'
  if (type === 'onmicro') return '与当前 OM6626 主板匹配并带发布签名的单槽 Recovery OTA ZIP'
  if (type === 'dialog') return 'DA14585 SDK6 pQ SUOTA .img'
  if (type === 'telink') return 'TLSR8359 单槽 Recovery OTA ZIP（旧版裸 .bin 仍可用于调试）'
  return '连接设备后识别更新格式'
}

export function assertApplicationPackageFamily(type: 'goodix' | 'onmicro', identity: string, target: string) {
  const current = canonicalBoardId(identity)
  const family = firmwareFamilyProfile(current)
  const legacy = type === 'goodix'
    ? target === 'gr551x_zkc42v' && ['gr551x_zkc42vm', 'gr551x_zkc42v'].includes(current)
    : target === 'om6626_zkcxxb' && ['om6626_zkc21b', 'om6626_zkc42b', 'om6626_zkc21v'].includes(current)
  if (!current || (family ? family.ota !== type || family.packageTarget !== target : current !== target && !legacy)) {
    throw new Error('固件与当前设备的固件系列不匹配')
  }
}

export function assertTelinkPackageTarget(boardId: string, manifest?: { board?: string; compatible_boards?: string[] }) {
  const current = canonicalBoardId(boardId)
  if (current === TLSR_FIRMWARE_FAMILY) {
    if (manifest?.board !== TLSR_FIRMWARE_FAMILY) throw new Error('固件与当前 TLSR8359 固件系列不匹配')
    return
  }
  // Preserve only the existing installed-target path; do not infer a PCB or
  // widen compatibility merely from a TLSR/HS9118-looking product prefix.
  if (boardId && !new Set(['hs_nowa154', 'hs_nebular420']).has(current)) throw new Error(`当前设备 ${boardId} 不是已识别的 TLSR8359 固件目标`)
  const target = manifest?.board
  const compatible = (manifest?.compatible_boards || []).map(canonicalBoardId)
  if (boardId && target && canonicalBoardId(target) !== current &&
      !(target === TLSR_FIRMWARE_FAMILY && compatible.includes(current))) {
    throw new Error(`固件目标为 ${target}，当前设备固件标识为 ${current}`)
  }
}

export function assertNordicPackageTarget(boardId: string, signedBoard: string) {
  if (!boardId) return // An offline inspection has no selected hardware.
  const current=canonicalBoardId(boardId==='nrf52811_aesl'?'aesl22_nrf':boardId)
  if (!['aesl22_nrf','nrf52832_priceetag'].includes(current) || current!==signedBoard)
    throw new Error('固件与当前 Nordic 主板不匹配，请选择对应主板的 OTA 包')
}

export async function parseOtaPackage(type: OtaType, file: File, boardId: string): Promise<ParsedOtaPackage> {
  loadAdapters()
  if (type === 'none') throw new Error('当前设备不支持 Web OTA')
  if (type === 'nordic') {
    const payload = await window.NordicDfu!.parsePackage(file)
    assertNordicPackageTarget(boardId,payload.verified?.board)
    return { type, payload, summary: `${file.name} · 应用 ${payload.firmware.length} bytes · ${payload.verified?.publisherVerified ? '签名' : '未签名'}初始化包 ${payload.initPacket.length} bytes` }
  }
  if (type === 'goodix') {
    const payload = await window.GoodixOta!.parsePackage(file)
    if (boardId) assertApplicationPackageFamily('goodix', boardId, payload.manifest.board)
    return {
      type,
      payload,
      summary: `${file.name} · ${payload.manifest.chip} / ${payload.manifest.board} · ` +
        `版本 ${payload.manifest.release} (${payload.manifest.releaseVersion}) · ` +
        `兼容纪元 ${payload.manifest.compatibilityEpoch} · 应用 ${payload.application.size} bytes · ` +
        (payload.signatureStatus === 'unsigned' ? '未签名 base 固件（CRC 已校验）' : `已验证签名密钥 ${payload.manifest.signature.key_id}`),
    }
  }
  if (type === 'onmicro') {
    const payload = await window.OnMicroOta!.parsePackage(file)
    if (boardId) assertApplicationPackageFamily('onmicro', boardId, payload.manifest.board)
    return { type, payload, summary: `${file.name} · ${payload.manifest.chip} / ${payload.manifest.board} · 应用 ${payload.application.size} bytes · ${payload.signatureStatus === 'unsigned' ? '未签名 base 固件（CRC 已校验）' : `已验证签名密钥 ${payload.manifest.signature.key_id}`}` }
  }
  if (type === 'telink') {
    const payload = await window.TelinkOta!.parsePackage(file)
    assertTelinkPackageTarget(boardId, payload.manifest)
    return { type, payload, summary: `${file.name} · TLSR8359 单应用 ${payload.firmware.length} bytes · CRC32 ${payload.crc32.toString(16).padStart(8, '0').toUpperCase()}` }
  }
  const dialogBoard = boardId === 'da14585_aesl' || boardId === 'aesl42_da' ||
    boardId === 'aesl29_da'
  if (boardId && !dialogBoard) throw new Error(`当前主板 ${boardId} 不是 DA14585 AESL 目标`)
  const payload = await window.DialogSuota!.parsePackage(file)
  return { type, payload, summary: `${file.name} · SDK6 pQ ${payload.version} · 应用 ${payload.codeSize} bytes` }
}

async function reconnectApplication(
  device: BluetoothDevice,
  attempts: number,
  onProgress: (progress: OtaProgress) => void,
  ownership?: { before: () => void; after: () => void; releaseRecovery: () => Promise<void> },
  timeoutMs = 45000,
) {
  const deadline = Date.now() + timeoutMs
  for (let attempt = 0; attempt < attempts; attempt++) {
    const remaining = deadline - Date.now()
    if (remaining <= 0) break
    await new Promise((resolve) => setTimeout(resolve, attempt === 0 ? 1800 : 900))
    ownership?.before()
    onProgress({ percent: 100, message: `设备正在重启，尝试自动重连 ${attempt + 1}/${attempts}` })
    try {
      await inktagClient.reconnectDevice(device, Math.min(10000, remaining))
      ownership?.after()
      if (inktagClient.getOtaRecoveryType() === null) return true
      // A successful GATT reconnect to Recovery is not an application return.
      if (ownership) await ownership.releaseRecovery()
      else await inktagClient.disconnectAndWait()
    } catch {
      ownership?.before() // User cancellation is terminal, not another reconnect.
      /* The application may still be rebooting. */
    }
  }
  return false
}

export async function verifyGoodixRecovery(server: BluetoothRemoteGATTServer) {
  loadAdapters()
  await window.GoodixOta!.queryRecovery(server)
}

export async function verifyTelinkRecovery(server: BluetoothRemoteGATTServer) {
  loadAdapters()
  return await window.TelinkOta!.queryRecovery(server)
}

function formatTelinkRecoveryStatus(result: { status: number; raw: Uint8Array }) {
  return `Recovery 状态 ${result.status} · ${Array.from(result.raw, value => value.toString(16).padStart(2, '0')).join(' ')}`
}

// Explicit read-only UI action. Reuse only the exact pre-entry grant; never
// turn a failed discovery into another enter/reset/firmware request.
export async function inspectTelinkRecovery(onPhase: (message: string) => void) {
  const state = useDeviceStore.getState()
  if (state.action !== 'idle') throw new Error('请等待当前操作结束')
  const current = inktagClient.getDevice()
  const verified = inktagClient.getOtaRecoveryType() === 'telink' ? inktagClient.getVerifiedRecoveryName() : ''
  const liveServer = verified && current ? inktagClient.getGattServer() : null
  if (liveServer?.connected) {
    state.setAction('ota', 0, '正在读取 Recovery 状态')
    try {
      loadAdapters()
      const result = await window.TelinkOta!.queryRecovery(liveServer, onPhase)
      return formatTelinkRecoveryStatus(result)
    } finally { useDeviceStore.getState().setAction('idle') }
  }
  const expected = inktagClient.getExpectedRecoveryIdentity() || (current && verified
    ? { deviceId: current.id, recoveryName: verified } : null)
  if (!expected || !/^(?:IKT|InkTag)_TL_[0-9A-F]{4}_OTA$/i.test(expected.recoveryName)) {
    throw new Error('没有本页已核对的 TLSR Recovery 交接身份；请先通过正常入口进入 Recovery')
  }
  const retained = inktagClient.getRecoveryHandoffDevice()
  const bluetooth = navigator.bluetooth as (Bluetooth & { getDevices?: () => Promise<BluetoothDevice[]> }) | undefined
  const devices: BluetoothDevice[] = current?.id === expected.deviceId ? [current]
    : retained?.id === expected.deviceId ? [retained]
    : await bluetooth?.getDevices?.() || []
  const device = devices.find(candidate => candidate.id === expected.deviceId)
  if (!device) throw new Error('原设备授权不可用，请从正常蓝牙入口只读确认同一设备')
  state.setAction('ota', 0, '正在读取 Recovery 状态')
  try {
    // Release the ordinary client before the adapter owns this short read.
    const latest = inktagClient.getExpectedRecoveryIdentity() || (verified ? expected : null)
    if (latest?.deviceId !== expected.deviceId || latest.recoveryName !== expected.recoveryName) {
      throw new Error('Recovery 交接身份已变化，已停止读取')
    }
    if (inktagClient.getExpectedRecoveryIdentity()) await inktagClient.releaseForRecovery(expected)
    else await inktagClient.disconnectAndWait(900)
    if (inktagClient.getDevice()) throw new Error('设备连接已变化，已停止读取')
    loadAdapters()
    const result = await window.TelinkOta!.queryRecoveryDevice(device, { onPhase })
    return formatTelinkRecoveryStatus(result)
  } finally { useDeviceStore.getState().setAction('idle') }
}

export async function inspectNordicRecovery(onPhase: (message: string) => void) {
  const state = useDeviceStore.getState(), device = inktagClient.getDevice(), server = inktagClient.getGattServer()
  if (state.action !== 'idle') throw new Error('请等待当前操作结束')
  if (!device || !server?.connected || inktagClient.getOtaRecoveryType() !== 'nordic' ||
      !/^(?:IKT|InkTag)_NRF_[0-9A-F]{4}_OTA$/i.test(inktagClient.getVerifiedRecoveryName())) {
    throw new Error('请先从正常蓝牙入口连接并核对同一台 Nordic Recovery')
  }
  state.setAction('ota', 0, '正在读取 Recovery 状态')
  try {
    loadAdapters()
    const result = await window.NordicDfu!.queryRecoveryState(server, { onPhase })
    const show = (value: { offset: number; crc: number; maxSize: number }) =>
      `offset=${value.offset} CRC=${value.crc.toString(16).toUpperCase().padStart(8, '0')} max=${value.maxSize}`
    return `Recovery 只读状态 · INIT ${show(result.command)} · DATA ${show(result.data)}`
  } finally { useDeviceStore.getState().setAction('idle') }
}

export async function verifyOnMicroRecovery(server: BluetoothRemoteGATTServer) {
  loadAdapters()
  return await window.OnMicroOta!.queryRecovery(server)
}

export async function verifyDialogRecovery(server: BluetoothRemoteGATTServer) {
  loadAdapters()
  try { return await window.DialogSuota!.queryRecovery(server) as { safeResume: boolean } }
  catch { return { safeResume: false } } // Read failure is never capability proof.
}

export async function verifyNordicRecovery(server: BluetoothRemoteGATTServer) {
  loadAdapters()
  try { return await window.NordicDfu!.queryRecovery(server) as { safeResume: boolean } }
  catch { return { safeResume: false } }
}

// A route/dialog remount is not permission to replay entry on the same
// unconfirmed connection. A newly read App clears the client's handoff.
const requestedRecoveryEntries = new WeakSet<BluetoothRemoteGATTServer>()
async function requestAndReleaseFixedRecovery(
  type: Exclude<OtaType, 'none'>, device: BluetoothDevice, server: BluetoothRemoteGATTServer,
) {
  if (requestedRecoveryEntries.has(server) && inktagClient.getExpectedRecoveryIdentity()) {
    throw new Error('进入 Recovery 结果尚未确认；请只读重新连接确认，不要重复进入或复位')
  }
  requestedRecoveryEntries.add(server)
  const state = useDeviceStore.getState()
  // Existing ordinary AUTH and exact ID4/grant handoff; no CREATE/DATA,
  // package write, forced reset, chooser injection or fallback entry.
  try { await inktagClient.enterOtaRecovery() }
  catch (error) {
    // A matching explicit refusal is not an unknown write result. Permit a
    // later user-confirmed attempt after ordinary AUTH/availability changes;
    // timeouts, transport errors and other ambiguous results remain sealed.
    if (error instanceof ProtocolV2Error && ([StatusV2.UNKNOWN_COMMAND, StatusV2.UNSUPPORTED,
      StatusV2.INVALID_REQUEST, StatusV2.INVALID_STATE, StatusV2.BUSY, StatusV2.DENIED,
      StatusV2.LOCKED, StatusV2.LOW_POWER] as number[]).includes(error.status)) {
      requestedRecoveryEntries.delete(server)
    }
    throw error
  }
  const expected = inktagClient.getExpectedRecoveryIdentity()
  if (!expected || expected.deviceId !== device.id) throw new Error('Recovery 交接身份尚未确认；不要重复进入或复位')
  const requireHandoff = () => {
    const current = inktagClient.getExpectedRecoveryIdentity()
    if (!current || current.deviceId !== expected.deviceId || current.recoveryName !== expected.recoveryName) {
      throw new Error('Recovery 交接已取消或身份已改变；请只读确认设备状态，不要重复进入或复位')
    }
  }
  requireHandoff()
  if (type === 'goodix') {
    loadAdapters()
    await window.GoodixOta!.enterRecovery(server, {
      isCurrent: () => {
        const current = inktagClient.getExpectedRecoveryIdentity()
        return useDeviceStore.getState().action === 'ota' && current?.deviceId === expected.deviceId &&
          current.recoveryName === expected.recoveryName
      },
      onStage: (message: string) => state.addLog(message),
    })
    requireHandoff()
  }
  state.addLog('设备已接受进入 Recovery，正在等候角色切换')
  // Some qualified Recoveries wait for the central to leave. Release our
  // session after the accepted entry, retaining only its exact grant/identity.
  await inktagClient.releaseForRecovery(expected)
  requireHandoff()
  return { expected, requireHandoff }
}

function recoverySelectionRequired(cause?: unknown) {
  return Object.assign(new Error('尚未连上 Recovery，固件尚未传输。', { cause }), {
    code: 'RECOVERY_SELECT_REQUIRED',
  })
}

async function reconnectFixedRecovery(
  device: BluetoothDevice, type: Exclude<OtaType, 'none'>,
  { expected, requireHandoff }: Awaited<ReturnType<typeof requestAndReleaseFixedRecovery>>,
) {
  const state = useDeviceStore.getState()
  const deadline = Date.now() + 45000
  let lastError: unknown
  for (let attempt = 0; attempt < 9 && Date.now() < deadline; attempt++) {
    await new Promise(resolve => setTimeout(resolve, attempt === 0 ? 1800 : 900))
    requireHandoff()
    try {
      await inktagClient.reconnectDevice(device, Math.min(10000, Math.max(1, deadline - Date.now())), true)
      const verifiedName = inktagClient.getVerifiedRecoveryName()
      if (inktagClient.getDevice()?.id === device.id &&
          inktagClient.getOtaRecoveryType() === type &&
          (verifiedName === expected.recoveryName || verifiedName === expected.recoveryName.replace(/^IKT_/, 'InkTag_'))) {
        state.addLog('同一设备的 Recovery 角色已核对；可更新固件或按设备能力安全退出', 'success')
        return
      }
      throw new Error('连接已恢复，但没有核对到期望的 Recovery 角色')
    } catch (error) { lastError = error }
  }
  throw new Error(`进入请求已接受，但 Recovery 回连尚未确认；请从附近设备选择 ${expected.recoveryName} 只读确认，不要重复进入或复位${lastError ? `：${String(lastError)}` : ''}`, { cause: lastError })
}

export async function enterFixedRecovery(expectedDeviceId: string) {
  const state = useDeviceStore.getState()
  const device = inktagClient.getDevice(), server = inktagClient.getGattServer()
  const type = state.capabilities.ota
  if (!server?.connected || !device || state.connection !== 'connected' ||
      state.deviceId !== expectedDeviceId || device.id !== expectedDeviceId ||
      state.firmwareVersion === 'Recovery' || state.protocolVersion !== PROTOCOL_V2_VERSION || type === 'none') {
    throw new Error('请先连接并确认同一台支持 Recovery 的 v1 应用设备')
  }
  if (state.action !== 'idle') throw new Error('请等待当前操作结束')
  if (requestedRecoveryEntries.has(server) && inktagClient.getExpectedRecoveryIdentity()) {
    throw new Error('进入 Recovery 结果尚未确认；请只读重新连接确认，不要重复进入或复位')
  }
  state.setAction('ota', 0, '正在进入 Recovery')
  try {
    const { expected, requireHandoff } = await requestAndReleaseFixedRecovery(type, device, server)
    await reconnectFixedRecovery(device, type, { expected, requireHandoff })
  } finally { useDeviceStore.getState().setAction('idle') }
}

const unconfirmedRecoveryExits = new WeakSet<BluetoothRemoteGATTServer>()
type RecoveryExitOptions = { syncTime?: boolean; onPhase?: (message: string) => void }
async function exitGuardedRecovery(type: 'goodix' | 'telink' | 'onmicro' | 'dialog' | 'nordic', options: RecoveryExitOptions = {}) {
  return inktagClient.withoutAutomaticClockSync(() => exitGuardedRecoveryWork(type, options))
}
async function exitGuardedRecoveryWork(type: 'goodix' | 'telink' | 'onmicro' | 'dialog' | 'nordic', options: RecoveryExitOptions) {
  const device = inktagClient.getDevice()
  const server = inktagClient.getGattServer()
  if (!device || !server?.connected || inktagClient.getOtaRecoveryType() !== type) {
    throw new Error('请先连接对应的固定 Recovery')
  }
  if (useDeviceStore.getState().action !== 'idle') throw new Error('请等待当前操作结束')
  if ((type === 'dialog' || type === 'nordic') && !inktagClient.getRecoverySafeResumeSupported()) throw new Error('该 Recovery 不支持安全退出')
  if (unconfirmedRecoveryExits.has(server)) {
    throw Object.assign(new Error('退出 Recovery 结果未知；请只读确认状态，不要重复退出或复位'), { code: 'RECOVERY_RESULT_UNKNOWN' })
  }
  loadAdapters()
  const adapter = type === 'goodix' ? window.GoodixOta! : type === 'telink' ? window.TelinkOta! : type === 'dialog' ? window.DialogSuota! : type === 'nordic' ? window.NordicDfu! : window.OnMicroOta!
  const original = (type === 'dialog' || type === 'nordic') ? inktagClient.getRecoveryOriginalImage() : null
  // Rejection is terminal; no fallback reset, erase or mode switch.
  options.onPhase?.('正在退出 Recovery')
  try {
    if (type === 'dialog' || type === 'nordic') await adapter.resumeApplication(server, { deviceId: device.id, recoveryName: inktagClient.getVerifiedRecoveryName() })
    else await adapter.resumeApplication(server)
  } catch (error) {
    if ((error as { code?: string })?.code === 'RECOVERY_RESULT_UNKNOWN') unconfirmedRecoveryExits.add(server)
    throw error
  }
  useDeviceStore.getState().addLog('Recovery 已接受安全退出，正在等待原应用', 'info')
  options.onPhase?.('等待 App 重连')
  await inktagClient.disconnectAndWait()
  if (!await reconnectApplication(device, 20, () => undefined, undefined, 90000)) {
    throw new Error('退出请求已接受，但原应用尚未重连；请重新选择设备确认，不要重复退出或复位')
  }
  if (type === 'dialog' || type === 'nordic') {
    const runtime = inktagClient.getRuntimeImageInfo()
    // Use the freshly read App identity, never a GAP prefix or image length.
    const family = canonicalBoardId(useDeviceStore.getState().boardId || '')
    const appLimit = type === 'dialog' ? 95648 : family === 'nrf52832_priceetag' ? 163840 : 131072
    if (!runtime || !runtime.appBytes || runtime.appBytes > appLimit ||
        (original && (original.appBytes !== runtime.appBytes || original.checksum !== runtime.checksum))) {
      throw new Error('已重连，但原应用读回尚未核验；不要重复退出或复位')
    }
  }
  options.onPhase?.('正在核对 App')
  inktagClient.verifyReturnedApplication(device)
  // Web Bluetooth may reuse the GATT object after a verified return to App.
  const appServer = inktagClient.getGattServer()
  if (appServer) requestedRecoveryEntries.delete(appServer)
  if (options.syncTime !== false) {
    useDeviceStore.getState().addLog('原应用已核验，正在校时', 'info')
    options.onPhase?.('正在校时')
    try { await inktagClient.syncTimeAndVerify() }
    catch (error) { throw new Error(`App 已返回，校时未完成：${error instanceof Error ? error.message : String(error)}。未重复退出 Recovery 或校时。`) }
  }
  useDeviceStore.getState().addLog(options.syncTime === false ? '已退出 Recovery；已按选择跳过校时' : '已退出 Recovery，原应用已重连并校时', 'success')
}

export const exitGoodixRecovery = (options?: RecoveryExitOptions) => exitGuardedRecovery('goodix', options)
export const exitTelinkRecovery = (options?: RecoveryExitOptions) => exitGuardedRecovery('telink', options)
export const exitOnMicroRecovery = (options?: RecoveryExitOptions) => exitGuardedRecovery('onmicro', options)
export const exitDialogRecovery = (options?: RecoveryExitOptions) => exitGuardedRecovery('dialog', options)
export const exitNordicRecovery = (options?: RecoveryExitOptions) => exitGuardedRecovery('nordic', options)

let goodixTransferGeneration = 0
export async function transferOta(pkg: ParsedOtaPackage, onProgress: (progress: OtaProgress) => void) {
  loadAdapters()
  const server = inktagClient.getGattServer()
  const applicationDevice = inktagClient.getDevice()
  if (!server?.connected || !applicationDevice) throw new Error('请先连接应用设备')
  const store = useDeviceStore.getState()
  if (store.action !== 'idle') throw new Error('请等待当前操作结束')
  const onmicroRecovery = pkg.type === 'onmicro' && inktagClient.getOtaRecoveryType() === 'onmicro'
  const dialogRecovery = pkg.type === 'dialog' && inktagClient.getOtaRecoveryType() === 'dialog'
  const telinkRecovery = pkg.type === 'telink' && inktagClient.getOtaRecoveryType() === 'telink'
  const goodixRecovery = pkg.type === 'goodix' && inktagClient.getOtaRecoveryType() === 'goodix'
  const nordicRecovery = pkg.type === 'nordic' && inktagClient.getOtaRecoveryType() === 'nordic'
  const goodixGeneration = pkg.type === 'goodix' ? ++goodixTransferGeneration : 0
  let goodixHandoff: { deviceId: string; recoveryName: string } | undefined
  let goodixAppGeneration: number | undefined
  const requireGoodixAction = () => {
    if (goodixGeneration !== goodixTransferGeneration || useDeviceStore.getState().action !== 'ota') {
      throw new DOMException('GR 升级操作已取消或变化', 'AbortError')
    }
  }
  const requireGoodixHandoff = () => {
    requireGoodixAction()
    const current = inktagClient.getExpectedRecoveryIdentity()
    if (!goodixHandoff || current?.deviceId !== goodixHandoff.deviceId || current.recoveryName !== goodixHandoff.recoveryName) {
      throw new DOMException('GR 升级交接已取消或身份变化', 'AbortError')
    }
  }
  const requireGoodixApp = () => {
    requireGoodixAction()
    if (inktagClient.getDevice()?.id !== applicationDevice.id || !inktagClient.getGattServer()?.connected ||
        inktagClient.getOtaRecoveryType() !== null || inktagClient.getConnectionGeneration() !== goodixAppGeneration) {
      throw new DOMException('GR 应用连接已取消或变化，未继续验身或校时', 'AbortError')
    }
  }
  let onmicroBaselineGeneration: number | undefined
  let nordicFinalAckUnknown = false
  const transferNordic = async (device: BluetoothDevice) => {
    const board = canonicalBoardId(store.boardId === 'nrf52811_aesl' ? 'aesl22_nrf' : store.boardId || '')
    try {
      await window.NordicDfu!.transferDevice(device, pkg.payload, progress, {
        expectedBoard: board,
        // Match CLI direct-Recovery admission: a retained nRF11 board label
        // is not this transfer's live App-family proof for the 200-byte pilot.
        // Keep both qualified live-App paths and existing nRF32 behavior.
        maxPacketSize: nordicRecovery && board === 'aesl22_nrf' ? 20
          : ['nrf52832_priceetag', 'aesl22_nrf'].includes(board) ? 200 : 20,
        onPhase: (message: string) => {
          const state = useDeviceStore.getState()
          state.addLog(message)
          onProgress({ percent: (state.actionProgress || 0) * 100, message })
        },
      })
    } catch (error) {
      if (!(error instanceof Error) || (error as Error & { code?: string }).code !== 'NORDIC_FINAL_EXECUTE_ACK_UNKNOWN') throw error
      // A missing final response is not success. Never resend EXECUTE or the
      // transfer; only the existing same-device V2 image gate below can pass.
      nordicFinalAckUnknown = true
    }
  }
  store.setAction('ota', 0)
  store.setOtaPhase(inktagClient.getOtaRecoveryType() ? 'recovery-transfer' : 'connecting-recovery')
  const progress = (done: number, total: number, started: number) => {
    const percent = total ? done * 100 / total : 0
    const seconds = Math.max((performance.now() - started) / 1000, .001)
    store.setAction('ota', percent / 100)
    // A payload callback follows the adapter's live Recovery channel checks.
    // This is display state only; never mark the App connection authenticated.
    if (done > 0 && total > 0) store.setOtaPhase('recovery-transfer')
    onProgress({ percent, message: `正在传输 ${done}/${total} bytes · ${(done / seconds / 1024).toFixed(1)} KiB/s` })
  }
  try {
    if (pkg.type === 'nordic') assertNordicPackageTarget(store.boardId,pkg.payload.verified?.board)
    if ((pkg.type === 'goodix' && !goodixRecovery) || (pkg.type === 'onmicro' && !onmicroRecovery)) {
      assertApplicationPackageFamily(pkg.type, useDeviceStore.getState().boardId, pkg.payload.manifest.board)
    }
    if (pkg.type === 'onmicro' && !onmicroRecovery) {
      onProgress({ percent: 0, message: '正在读取升级前的 Recovery 状态' })
      try {
        const baseline = await inktagClient.queryRuntimeBuild()
        onmicroBaselineGeneration = assertOnMicroOtaBaseline(baseline)
      } catch (error) {
        if (error instanceof Error && error.message.includes('固定 Recovery')) throw error
        const reason = error instanceof Error ? error.message : String(error)
        throw new Error(`无法读取升级前的 Recovery 状态：${reason}。尚未发送升级包，请先检查连接与设备日志`)
      }
    }
    if (pkg.type === 'goodix') {
      onProgress({ percent: 0, message: goodixRecovery
        ? '已连接 GR 固定 Recovery，正在重新发送完整应用'
        : '正在切换到 Goodix 固定恢复模式' })
      const generation = goodixGeneration
      const handoff = goodixRecovery
        ? inktagClient.takeGoodixRecoveryForOta(applicationDevice.id)
        : (await requestAndReleaseFixedRecovery('goodix', applicationDevice, server)).expected
      goodixHandoff = handoff
      const connectionGeneration = inktagClient.getConnectionGeneration()
      const isCurrent = () => {
        const current = inktagClient.getExpectedRecoveryIdentity()
        return generation === goodixTransferGeneration &&
          connectionGeneration === inktagClient.getConnectionGeneration() &&
          useDeviceStore.getState().action === 'ota' &&
          current?.deviceId === handoff.deviceId && current.recoveryName === handoff.recoveryName
      }
      const onStage = (message: string) => {
        useDeviceStore.getState().addLog(message)
        onProgress({ percent: (useDeviceStore.getState().actionProgress ?? 0) * 100, message })
      }
      // Acquire verifies and retains the actual Recovery transport. Transfer
      // consumes that same opaque handle; it never reconnects or enters again.
      const session = await window.GoodixOta!.acquireRecoverySession(
        { device: applicationDevice, generation, isCurrent, onStage }, pkg.payload)
      let failed = false
      try {
        await window.GoodixOta!.transferRecoverySession(session, pkg.payload, progress)
      } catch (error) { failed = true; throw error }
      finally {
        try { await window.GoodixOta!.releaseRecoverySession(session) }
        catch (error) {
          if (!failed) throw error
          store.addLog(`GR Recovery 清理未确认（保留原始失败）：${String(error)}`, 'warning')
        }
      }
    } else if (pkg.type === 'onmicro' || pkg.type === 'dialog') {
      const type = pkg.type
      const label = type === 'onmicro' ? 'OM6626' : 'DA14585'
      const alreadyInRecovery = type === 'onmicro' ? onmicroRecovery : dialogRecovery
      onProgress({ percent: 0, message: alreadyInRecovery
        ? `已连接 ${label} 固定 Recovery，正在重新发送完整应用`
        : `正在切换到 ${label} 固定 Recovery` })
      if (!alreadyInRecovery) {
        const handoff = await requestAndReleaseFixedRecovery(type, applicationDevice, server)
        try { await reconnectFixedRecovery(applicationDevice, type, handoff) }
        catch (error) {
          handoff.requireHandoff()
          if (inktagClient.getDevice()) throw error
          store.addLog(String(error), 'warning')
          throw recoverySelectionRequired(error)
        }
      }
      const recoveryServer = inktagClient.getGattServer()
      if (!recoveryServer?.connected || inktagClient.getDevice()?.id !== applicationDevice.id ||
          inktagClient.getOtaRecoveryType() !== type) {
        throw new Error('未确认原设备的 Recovery 连接，固件尚未传输')
      }
      const adapter = type === 'onmicro' ? window.OnMicroOta! : window.DialogSuota!
      await adapter.transferServer(recoveryServer, pkg.payload, progress, {
        onPhase: (message: string) => {
          if (type === 'onmicro') store.addLog(message)
          else onProgress({ percent: 0, message })
        },
      })
    } else if (pkg.type === 'telink') {
      const adapter = window.TelinkOta!
      const selectRecovery = (error: unknown) => {
        const cause = error instanceof Error ? error.cause ?? error : error
        if (!adapter.isTransientGattError(cause)) throw error
        store.addLog(`TLSR Recovery 连接中断：${String(cause)}`, 'warning')
        throw recoverySelectionRequired(error)
      }
      if (telinkRecovery) {
        onProgress({ percent: 0, message: '已连接 TLSR8359 固定 Recovery，正在重新发送完整应用' })
      } else {
        onProgress({ percent: 0, message: '正在切换到 TLSR8359 固定 Recovery' })
        const entry = await requestAndReleaseFixedRecovery('telink', applicationDevice, server)
        try { await reconnectFixedRecovery(applicationDevice, 'telink', entry) }
        catch (error) {
          entry.requireHandoff()
          selectRecovery(error)
        }
      }
      const handoff = inktagClient.takeTelinkRecoveryForOta(applicationDevice.id)
      const generation = inktagClient.getConnectionGeneration()
      const current = () => {
        const expected = inktagClient.getExpectedRecoveryIdentity()
        return inktagClient.getConnectionGeneration() === generation &&
          expected?.deviceId === handoff.deviceId && expected.recoveryName === handoff.recoveryName
      }
      const handle = await adapter.acquireRecoverySession({
        device: applicationDevice, generation, isCurrent: current,
        onDiagnostics: (metrics: unknown) => useDeviceStore.getState().addLog(`TLSR OTA 统计：${JSON.stringify(metrics)}`),
        onStage: (message: string) => {
          useDeviceStore.getState().addLog(message)
          onProgress({ percent: 0, message })
        },
      }, pkg.payload).catch((error: unknown) => {
        if (!current()) throw error
        return selectRecovery(error)
      })
      let failure: unknown
      try { await window.TelinkOta!.transferRecoverySession(handle, pkg.payload, progress) }
      catch (error) { failure = error; throw error }
      finally {
        try { await window.TelinkOta!.releaseRecoverySession(handle) }
        catch (error) { if (!failure) throw error }
      }
    } else {
      const adapter = window.NordicDfu!
      if (inktagClient.getOtaRecoveryType() === 'nordic') {
        onProgress({ percent: 0, message: '已连接 nRF Secure DFU，正在发送完整应用' })
        await transferNordic(applicationDevice)
      } else {
        const identity = useDeviceStore.getState()
        const dfuName = adapter.bootloaderName('', identity.bleAddress)
        onProgress({ percent: 0, message: `正在切换到 ${dfuName}` })
        const entry = await requestAndReleaseFixedRecovery('nordic', applicationDevice, server)
        const handoff = entry.expected
        if (handoff.recoveryName !== dfuName) {
          throw new Error('无法确认升级前的 nRF 设备身份；不要重复请求进入 Recovery')
        }
        const recoveryStatus = (message: string) => {
          useDeviceStore.getState().addLog(message)
          onProgress({ percent: 0, message })
        }
        const device = await adapter.findGrantedBootloaderDevice(dfuName, handoff, applicationDevice, recoveryStatus)
        entry.requireHandoff()
        if (!device) throw recoverySelectionRequired()
        await transferNordic(device)
      }
    }
    onProgress({ percent: 100, message: nordicFinalAckUnknown
      ? '末尾 ACK 未知，不重发；正在核对同设备运行镜像，尚不能判定成功'
      : pkg.type === 'dialog'
      ? '数据传输结束，正在等待应用验身；断连不代表升级成功'
      : '升级包已提交，正在等待应用重新启动' })
    store.setOtaPhase('reconnecting-app')
    const reconnected = await reconnectApplication(
      applicationDevice,
      pkg.type === 'onmicro' || pkg.type === 'telink' ? 12 : 9,
      onProgress,
      pkg.type === 'goodix' ? {
        before: requireGoodixHandoff,
        after: () => {
          requireGoodixAction()
          if (inktagClient.getDevice()?.id !== applicationDevice.id) throw new DOMException('GR 连接身份已变化', 'AbortError')
        },
        releaseRecovery: () => inktagClient.releaseForRecovery(goodixHandoff!),
      } : undefined,
    )
    if (pkg.type === 'goodix' && reconnected) {
      goodixAppGeneration = inktagClient.getConnectionGeneration()
      requireGoodixApp()
    }
    if (reconnected) {
      store.setOtaPhase('attesting-app')
      await inktagClient.waitForPasswordAuthorization()
      if (pkg.type === 'goodix') requireGoodixApp()
    }
    if (pkg.type === 'goodix') {
      if (!reconnected) throw new Error('升级包已传完，但 GR 应用未重新上线，不能判定升级成功')
      assertApplicationPackageFamily('goodix', useDeviceStore.getState().boardId, pkg.payload.manifest.board)
      onProgress({ percent: 100, message: '设备已重连，正在核对运行固件' })
      const runtime = inktagClient.getRuntimeImageInfo()
      // Release-v1 GR DEVICE_INFO identifies the active image, not journal state.
      // Match the shared CLI: never send a retired raw byte or invent a gen.
      assertGoodixOtaAttestation(runtime, pkg.payload)
      onProgress({ percent: 100, message: '运行固件与升级包一致，正在恢复设备时间' })
    } else if (pkg.type === 'onmicro') {
      if (!reconnected) throw new Error('升级包已传完，但设备未重新上线，不能判定升级成功')
      assertApplicationPackageFamily('onmicro', useDeviceStore.getState().boardId, pkg.payload.manifest.board)
      onProgress({ percent: 100, message: '设备已重连，正在核对运行固件与 Recovery 状态' })
      const runtime = await inktagClient.queryRuntimeBuild()
      assertOnMicroOtaAttestation(
        runtime,
        Number(pkg.payload.manifest.releaseVersion),
        window.OnMicroOta!.applicationBodySize(pkg.payload),
        onmicroBaselineGeneration,
      )
      onProgress({ percent: 100, message: '单应用试运行已确认，正在恢复设备时间' })
    } else if (pkg.type === 'telink') {
      if (!reconnected) throw new Error('升级包已传完，但 TLSR8359 应用未重新上线，不能判定升级成功')
      onProgress({ percent: 100, message: '设备已重连，正在核对 TLSR8359 应用大小与 CRC32' })
      const identity = await inktagClient.queryV2ApplicationIdentity()
      if (identity.size !== pkg.payload.declaredSize || identity.checksum !== pkg.payload.crc32) {
        throw new Error(`TLSR8359 应用核验不一致：设备 ${identity.size}/${identity.checksum.toString(16)}，固件 ${pkg.payload.declaredSize}/${pkg.payload.crc32.toString(16)}`)
      }
      requestedRecoveryEntries.delete(server)
      onProgress({ percent: 100, message: '单应用试运行已确认，应用大小与 CRC32 一致' })
    } else if (pkg.type === 'dialog') {
      if (!reconnected) throw new Error('升级包已传完，但 DA14585 应用未重新上线，不能判定升级成功')
      const runtime = inktagClient.getRuntimeImageInfo()
      assertDialogOtaAttestation(runtime, pkg.payload)
      onProgress({ percent: 100, message: '应用长度与 CRC32 已核对，试运行已确认' })
    } else if (pkg.type === 'nordic') {
      if (!reconnected) throw new Error('升级包已传完，但 nRF 应用未重新上线，不能判定升级成功')
      // parsePackage computes firmwareCrc from the exact submitted BIN. A live
      // GATT/App reconnect alone is not proof that this application is running.
      const identity = await inktagClient.queryV2ApplicationIdentity()
      if (!pkg.payload.firmware?.length || identity.size !== pkg.payload.firmware.length ||
          identity.checksum !== pkg.payload.firmwareCrc) {
        throw new Error(`nRF 应用核验不一致：设备 ${identity.size}/${identity.checksum.toString(16)}，固件 ${pkg.payload.firmware?.length ?? 0}/${pkg.payload.firmwareCrc?.toString(16) ?? 'unknown'}`)
      }
      onProgress({ percent: 100, message: '应用大小与 CRC32 已核对' })
    }
    if (reconnected) {
      onProgress({ percent: 100, message: '固件验身完成，正在校准设备时间' })
      const syncError = await syncClockAfterVerifiedOta(pkg.type === 'goodix' ? async () => {
        requireGoodixApp()
        await inktagClient.waitForPasswordAuthorization()
        requireGoodixApp()
        await inktagClient.syncTime()
      } : undefined)
      if (pkg.type === 'goodix') requireGoodixApp()
      if (syncError) {
        useDeviceStore.getState().addLog(
          `OTA 已完成，但设备时间校准失败：${syncError}`,
          'warning',
        )
        onProgress({ percent: 100, message: 'OTA 已完成；设备时间未校准，可稍后手动同步' })
      } else {
        onProgress({ percent: 100, message: 'OTA 完成，应用已验身、重连并校时' })
      }
    } else {
      onProgress({ percent: 100, message: 'OTA 已提交，请手动连接设备确认版本并同步时间' })
    }
  } finally {
    if (pkg.type !== 'goodix' || (goodixGeneration === goodixTransferGeneration &&
        useDeviceStore.getState().action === 'ota' &&
        (!inktagClient.getDevice() || inktagClient.getDevice()?.id === applicationDevice.id))) {
      useDeviceStore.getState().setAction('idle')
    }
  }
}
