import { beforeEach, describe, expect, it, vi } from 'vitest'
import { InkTagClient } from './client'
import { CommandV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

const token = new Uint8Array(96)
token.set([0x49, 0x41, 1, 1])
const code = btoa(String.fromCharCode(...token)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
const request = new Uint8Array(32)
request[0] = 1
request[16] = 1
request[24] = 1
const status = new Uint8Array(24)
status.set([2, 2, 1, 0, 1, 1, 128])
status[8] = 1
status[12] = 1
status[16] = 1

describe('explicit activation restore client', () => {
  beforeEach(() => useDeviceStore.setState({ deviceId: '', capabilities: { ...useDeviceStore.getState().capabilities, imageStore: false } }))
  it.each([false, true])('keeps mode explicit (%s) and requests before restoring', async (restore) => {
    const client = new InkTagClient()
    const send = vi.spyOn(client as any, 'requestV2').mockImplementation(async (command) => ({
      payload: command === CommandV2.GET_ACTIVATION_REQUEST ? request : status,
    }))
    await client.installActivationCode(code, restore)
    expect(send.mock.calls.map(([command]) => command)).toEqual(restore
      ? [CommandV2.GET_ACTIVATION_REQUEST, CommandV2.SET_ACTIVATION_CODE]
      : [CommandV2.SET_ACTIVATION_CODE])
    const payload = send.mock.calls.at(-1)![1] as Uint8Array
    expect(payload).toEqual(restore ? new Uint8Array([1, ...token]) : token)
  })
  it('does not send code or fallback after request failure', async () => {
    const client = new InkTagClient()
    const send = vi.spyOn(client as any, 'requestV2').mockRejectedValue(new Error('unavailable'))
    await expect(client.installActivationCode(code, true)).rejects.toThrow('unavailable')
    expect(send).toHaveBeenCalledTimes(1)
    expect(send.mock.calls[0][0]).toBe(CommandV2.GET_ACTIVATION_REQUEST)
  })
})
