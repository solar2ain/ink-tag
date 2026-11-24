import { beforeEach, expect, it, vi } from 'vitest'
import { decodeTestOperations } from './testOperations'
import { InkTagClient } from './client'
import { CapabilityV2, CommandV2, DisplayModeV2, TestOperationV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

beforeEach(() => useDeviceStore.getState().resetDevice())

it('validates the exact schema, count and unique operations instead of a settings version', () => {
  expect(decodeTestOperations(new Uint8Array([1, 0]))).toEqual([])
  expect(decodeTestOperations(new Uint8Array([1, 2, 0x30, 0x31]))).toEqual([0x30, 0x31])
  for (const bytes of [[], [1], [0, 0], [1, 1], [1, 0, 0x30], [1, 2, 0x30, 0x30]]) {
    expect(() => decodeTestOperations(new Uint8Array(bytes))).toThrow('测试项目清单')
  }
})

it('does not query a test command on a firmware without the broad TEST capability', async () => {
  const client = new InkTagClient(), request = vi.fn()
  Object.assign(client, { protocolV2: true, v2Capabilities: 0, requestV2: request })
  expect(await client.listTests()).toEqual([])
  await expect(client.testTimeShift(60, DisplayModeV2.CLOCK)).rejects.toThrow('未提供这项测试')
  expect(request).not.toHaveBeenCalled()
})

it('rejects a stale list when the connection changes before its response', async () => {
  const client = new InkTagClient()
  let finish!: (value: { payload: Uint8Array }) => void
  Object.assign(client, { protocolV2: true, v2Capabilities: CapabilityV2.TEST,
    requestV2: vi.fn(() => new Promise(resolve => { finish = resolve })) })
  const pending = client.listTests()
  Object.assign(client, { connectionGeneration: 1 })
  finish({ payload: new Uint8Array([1, 1, TestOperationV2.TIME_SET]) })
  await expect(pending).rejects.toThrow('连接已改变')
})

it('rejects unlisted time, button and LED operations before RUN_TEST', async () => {
  const client = new InkTagClient(), request = vi.fn().mockResolvedValue({ payload: new Uint8Array([1, 0]) })
  Object.assign(client, { protocolV2: true, v2Capabilities: CapabilityV2.TEST, requestV2: request })
  for (const operation of [() => client.testTimeShift(60, DisplayModeV2.CLOCK),
    () => client.testTimeSet(new Date('2026-11-07T00:00:00Z'), DisplayModeV2.CALENDAR),
    () => client.simulateButton('single'), () => client.testLed(1)]) {
    await expect(operation()).rejects.toThrow('未提供这项测试')
  }
  expect(request.mock.calls.every(call => call[0] === CommandV2.LIST_TESTS)).toBe(true)
  expect(useDeviceStore.getState().action).toBe('idle')
})

it('accepts only the specific listed time operation, retaining its existing work path', async () => {
  const client = new InkTagClient(), work = vi.fn().mockResolvedValue(new Uint8Array(7))
  Object.assign(client, { protocolV2: true, v2Capabilities: CapabilityV2.TEST,
    requestV2: vi.fn().mockResolvedValue({ payload: new Uint8Array([1, 1, TestOperationV2.TIME_SHIFT]) }),
    runV2TestWork: work })
  await client.testTimeShift(60, DisplayModeV2.CLOCK)
  expect(work).toHaveBeenCalledExactlyOnceWith(new Uint8Array([TestOperationV2.TIME_SHIFT, DisplayModeV2.CLOCK, 60, 0, 0, 0]))
  await expect(client.testTimeSet(new Date(), DisplayModeV2.CLOCK)).rejects.toThrow('未提供这项测试')
  expect(work).toHaveBeenCalledTimes(1)
})

it('does not carry an admitted test across the final async connection boundary', async () => {
  const client = new InkTagClient(), work = vi.fn()
  Object.assign(client, { protocolV2: true, runV2TestWork: work,
    requireTest: async () => { Object.assign(client, { connectionGeneration: 1 }); return 0 } })
  await expect(client.testTimeShift(60, DisplayModeV2.CLOCK)).rejects.toThrow('连接已改变')
  expect(work).not.toHaveBeenCalled()
  expect(useDeviceStore.getState().action).toBe('idle')
})
