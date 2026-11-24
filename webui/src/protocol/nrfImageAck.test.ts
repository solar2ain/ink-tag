import { afterEach, expect, it, vi } from 'vitest'
import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, StatusV2, TransferObjectV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

const previousBoard = useDeviceStore.getState().boardId
afterEach(() => { useDeviceStore.setState({ boardId: previousBoard }); vi.restoreAllMocks() })

it.each([
  ['nrf52832_priceetag', true, 244, TransferObjectV2.DIRECT_FRAME, false],
  ['nrf52832_priceetag', false, 244, TransferObjectV2.DIRECT_FRAME, true],
  ['nrf52832_priceetag', undefined, 244, TransferObjectV2.DIRECT_FRAME, true],
  ['nrf52832_priceetag', true, 20, TransferObjectV2.DIRECT_FRAME, true],
  ['tlsr8359_hanshow', true, 244, TransferObjectV2.DIRECT_FRAME, true],
  ['tlsr8359_hanshow_extra', true, 244, TransferObjectV2.DIRECT_FRAME, true],
  ['aesl22_nrf', true, 244, TransferObjectV2.DIRECT_FRAME, true],
  ['om6626_zkcxxb', true, 244, TransferObjectV2.DIRECT_FRAME, true],
  ['nrf52832_priceetag', true, 244, TransferObjectV2.STORED_IMAGE, true],
  ['nrf52832_priceetag', true, 244, TransferObjectV2.GUI_ASSET_PACKAGE, true],
  ['nrf52832_priceetag', true, 244, TransferObjectV2.DEVICE_PASSWORD_BUNDLE, true],
])('gates ATT response omission by %s/%s/%s/%s', async (boardId, property, mtu, object, response) => {
  const client = new InkTagClient() as any
  useDeviceStore.setState({ boardId })
  client.characteristic = { properties: { writeWithoutResponse: property } }
  client.imageMtu = mtu
  client.requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array() })
  await client.writeV2TransferChunk(7, object, 237, 0, new Uint8Array(237))
  expect(client.requestV2.mock.calls[0][6]).toBe(response)
})

it.each([[237, 1, false], [0, 2, false], [5, 1, true]])('retains product offset recovery: %s', async (written, writes, error) => {
  const client = new InkTagClient() as any
  useDeviceStore.setState({ boardId: 'nrf52832_priceetag' })
  client.characteristic = { properties: { writeWithoutResponse: true } }
  client.imageMtu = 244
  const status = new Uint8Array(11)
  status.set([1, TransferObjectV2.DIRECT_FRAME, 7])
  new DataView(status.buffer).setUint32(3, written as number, true)
  new DataView(status.buffer).setUint32(7, 237, true)
  client.requestV2 = vi.fn().mockRejectedValueOnce(new ProtocolV2Error(StatusV2.TIMEOUT))
    .mockResolvedValueOnce({ payload: status }).mockResolvedValue({ payload: new Uint8Array() })
  const task = client.writeV2TransferChunk(7, TransferObjectV2.DIRECT_FRAME, 237, 0, new Uint8Array(237))
  if (error) await expect(task).rejects.toThrow('偏移')
  else await task
  const chunks = client.requestV2.mock.calls.filter((c: unknown[]) => c[0] === CommandV2.TRANSFER_WRITE)
  expect(chunks).toHaveLength(writes as number)
  expect(chunks.every((c: unknown[]) => c[6] === false)).toBe(true)
  expect(client.requestV2.mock.calls[1][0]).toBe(CommandV2.GET_TRANSFER_STATUS)
  expect(client.requestV2.mock.calls[1][6]).toBeUndefined()
})

it('does not resolve a WNR chunk until the product ACK arrives', async () => {
  vi.stubGlobal('window', globalThis)
  const client = new InkTagClient() as any
  useDeviceStore.setState({ boardId: 'nrf52832_priceetag' })
  client.protocolV2 = true
  client.imageMtu = 244
  client.passwordSupported = () => false
  client.characteristic = { properties: { writeWithoutResponse: true } }
  client.writePacket = vi.fn().mockResolvedValue(undefined)
  let completed = false
  const task = client.writeV2TransferChunk(7, TransferObjectV2.DIRECT_FRAME, 237, 0, new Uint8Array(237))
    .then(() => { completed = true })
  await Promise.resolve()
  expect(completed).toBe(false)
  expect(client.writePacket.mock.calls[0][1]).toBe(false)
  const pending = client.v2Pending.get(7)
  clearTimeout(pending.timer)
  client.v2Pending.delete(7)
  pending.resolve({ payload: new Uint8Array(), transactionId: 7 })
  await task
  expect(completed).toBe(true)
  vi.unstubAllGlobals()
})
