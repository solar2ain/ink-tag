import { describe, expect, it, vi } from 'vitest'
import { InkTagClient } from './client'
import { CommandV2, TestOperationV2 } from './generatedCommandsV2'

describe('local device locator protocol', () => {
  it.each([
    ['starts', true, 1],
    ['stops', false, 0],
  ] as const)('%s with an explicit action byte', async (_label, start, action) => {
    const client = new InkTagClient()
    const requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array(0) })
    ;(client as unknown as { requestV2: typeof requestV2 }).requestV2 = requestV2

    if (start) await client.locateDevice()
    else await client.stopLocateDevice()

    expect(requestV2).toHaveBeenCalledWith(
      CommandV2.RUN_TEST,
      new Uint8Array([TestOperationV2.DEVICE_LOCATE, action]),
    )
  })
})
