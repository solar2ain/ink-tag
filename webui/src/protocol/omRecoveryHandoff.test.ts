import { beforeEach, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn() } } }))
vi.mock('./ota', () => ({ verifyOnMicroRecovery: vi.fn() }))
import { db } from '../data/db'
import { verifyOnMicroRecovery } from './ota'
import { InkTagClient } from './client'
import { useDeviceStore } from '../store/device'

const service = '000001bf-5f00-0000-0000-4f4d2d4f5441'
const preference = { id: 'b892-grant', lastConnectedName: 'IKT_OM_B892',
  lastConnectedBoardId: 'om6626_zkcxxb', lastConnectedAt: 123, reconnectHidden: false }
beforeEach(() => {
  vi.resetAllMocks()
  Object.defineProperty(globalThis, 'window', { configurable: true, value: globalThis })
  useDeviceStore.getState().resetDevice()
  vi.mocked(verifyOnMicroRecovery).mockResolvedValue(undefined)
})
function fixture() {
  const server = { connected: true, getPrimaryService: vi.fn(async (uuid: string) => {
    if (uuid !== service) throw new DOMException('missing App service', 'NotFoundError')
    return {}
  }) }
  const device = { id: preference.id, name: preference.lastConnectedName,
    addEventListener: vi.fn(), removeEventListener: vi.fn(),
    gatt: { connected: true, connect: vi.fn(async () => server), disconnect: vi.fn() } } as unknown as BluetoothDevice
  const client = new InkTagClient()
  const request = vi.fn()
  ;(client as unknown as { requestV2: typeof request }).requestV2 = request
  const sync = vi.spyOn(client, 'syncTime')
  return { client, device, server, request, sync }
}
it.each(['om6626_zkcxxb', 'om6626_zkc42b'])('verifies a remembered %s grant with a stale App name read-only', async board => {
  vi.mocked(db.devicePreferences.get).mockResolvedValue({ ...preference, lastConnectedBoardId: board } as never)
  const run = fixture()
  await run.client.reconnectDevice(run.device)
  expect(run.client.getOtaRecoveryType()).toBe('onmicro')
  expect(run.client.getVerifiedRecoveryName()).toBe('IKT_OM_B892_OTA')
  expect(verifyOnMicroRecovery).toHaveBeenCalledExactlyOnceWith(run.server)
  expect(run.request).not.toHaveBeenCalled()
  expect(run.sync).not.toHaveBeenCalled()
})
it.each([
  undefined, { ...preference, id: 'another-grant' },
  { ...preference, lastConnectedName: 'IKT_OM_63CF' },
  { ...preference, lastConnectedBoardId: 'gr5331_zkel' },
  { ...preference, lastConnectedBoardId: 'unknown' },
  { ...preference, lastConnectedAt: 0 }, { ...preference, reconnectHidden: true },
  { ...preference, lastConnectedAt: Infinity }, { ...preference, lastConnectedAt: NaN },
  { ...preference, lastConnectedName: 'unknown' },
])('does not infer Recovery from an unbound or incompatible preference %#', async pref => {
  vi.mocked(db.devicePreferences.get).mockResolvedValue(pref as never)
  const run = fixture()
  await expect(run.client.reconnectDevice(run.device)).rejects.toThrow('missing App service')
  expect(verifyOnMicroRecovery).not.toHaveBeenCalled()
  expect(run.server.getPrimaryService).toHaveBeenCalledTimes(1)
  expect(run.request).not.toHaveBeenCalled()
})
it.each(['IKT_OM_63CF_OTA', 'IKT_GR_B892_OTA'])('rejects a changed explicit Recovery ID/platform %s on the remembered grant', async name => {
  vi.mocked(db.devicePreferences.get).mockResolvedValue(preference as never)
  const run = fixture()
  Object.assign(run.device, { name })
  await expect(run.client.reconnectDevice(run.device)).rejects.toThrow('Recovery 身份不匹配')
  expect(verifyOnMicroRecovery).not.toHaveBeenCalled()
  expect(run.request).not.toHaveBeenCalled()
})
it('does not complete a Recovery connection after explicit cancellation', async () => {
  vi.mocked(db.devicePreferences.get).mockResolvedValue(preference as never)
  let finish!: () => void
  vi.mocked(verifyOnMicroRecovery).mockReturnValueOnce(new Promise<void>(resolve => { finish = resolve }))
  const run = fixture()
  const connecting = run.client.reconnectDevice(run.device)
  const rejected = expect(connecting).rejects.toThrow('蓝牙连接已取消')
  await vi.waitFor(() => expect(verifyOnMicroRecovery).toHaveBeenCalledOnce())
  Object.assign(run.device.gatt!, { connected: false })
  run.client.disconnect()
  finish()
  await rejected
  expect(run.client.getOtaRecoveryType()).toBeNull()
  expect(useDeviceStore.getState().connection).not.toBe('connected')
  expect(run.request).not.toHaveBeenCalled()
})
it('rejects a remembered grant if the live Recovery profile is invalid', async () => {
  vi.mocked(db.devicePreferences.get).mockResolvedValue(preference as never)
  vi.mocked(verifyOnMicroRecovery).mockRejectedValueOnce(new Error('incompatible live profile'))
  const run = fixture()
  await expect(run.client.reconnectDevice(run.device)).rejects.toThrow('incompatible live profile')
  expect(run.client.getOtaRecoveryType()).toBeNull()
  expect(run.request).not.toHaveBeenCalled()
})
