import { describe, expect, it } from 'vitest'

import vectors from '../../../protocol/golden-vectors.json'
import { CommandV2, EventV2, RequestFlagV2, StatusV2, PROTOCOL_V2_VERSION } from './generatedCommandsV2'
import { decodeScreenConfig, encodeScreenConfig } from './screen'

type PacketVector = {
  name: string
  kind: 'request' | 'response' | 'event'
  opcode?: string
  event?: string
  transactionId?: number
  originOpcode?: string
  originTransactionId?: number
  flags?: string
  status?: string
  payloadHex: string
  packetHex: string
}

const hex = (value: string) => new Uint8Array(value.match(/../g)?.map((byte) => Number.parseInt(byte, 16)) || [])
const named = (values: Record<string, number>, name?: string) => {
  const value = name === undefined ? undefined : values[name]
  if (value === undefined) throw new Error(`unknown golden-vector value: ${name || '<missing>'}`)
  return value
}

describe('Protocol V2 golden packets', () => {
  it('covers every generated command and event header', () => {
    expect(vectors.protocolVersion).toBe(PROTOCOL_V2_VERSION)
    expect(hex(vectors.packets.find(packet => packet.name === 'hello-response')!.payloadHex)[1]).toBe(PROTOCOL_V2_VERSION)
    expect(Object.keys(vectors.commandHeaders).sort()).toEqual(Object.keys(CommandV2).sort())
    expect(Object.keys(vectors.eventHeaders).sort()).toEqual(Object.keys(EventV2).sort())
    for (const [name, packetHex] of Object.entries(vectors.commandHeaders)) {
      expect(hex(packetHex)).toEqual(new Uint8Array([
        named(CommandV2, name), vectors.transactionId, RequestFlagV2.NONE,
      ]))
    }
    for (const [name, packetHex] of Object.entries(vectors.eventHeaders)) {
      expect(hex(packetHex)).toEqual(new Uint8Array([
        named(EventV2, name), 0, StatusV2.OK, CommandV2.HELLO, vectors.transactionId,
      ]))
    }
  })

  it('rebuilds every complete request, response and event fixture', () => {
    for (const vector of vectors.packets as PacketVector[]) {
      const payload = hex(vector.payloadHex)
      let header: Uint8Array
      if (vector.kind === 'request') {
        header = new Uint8Array([
          named(CommandV2, vector.opcode), vector.transactionId || 0,
          named(RequestFlagV2, vector.flags),
        ])
      } else if (vector.kind === 'response') {
        header = new Uint8Array([
          named(CommandV2, vector.opcode), vector.transactionId || 0,
          named(StatusV2, vector.status),
        ])
      } else {
        header = new Uint8Array([
          named(EventV2, vector.event), 0, named(StatusV2, vector.status),
          named(CommandV2, vector.originOpcode), vector.originTransactionId || 0,
        ])
      }
      expect(new Uint8Array([...header, ...payload]), vector.name).toEqual(hex(vector.packetHex))
    }
  })

  it('round-trips the shared release-v1 screen bytes', () => {
    const fixture = (vectors.packets as PacketVector[])
      .find((item) => item.name === 'set-screen-profile-request')
    expect(fixture).toBeDefined()
    const payload = hex(fixture?.payloadHex || '')
    const screen = decodeScreenConfig(payload)
    expect(screen).toEqual({
      schemaVersion: 1, driver: 0x13, recipe: 0, color: 2, transform: 0,
      nativeUiScale: 0,
      width: 400, height: 300, visibleX: 0, visibleY: 0,
      visibleWidth: 400, visibleHeight: 300,
    })
    expect(encodeScreenConfig(screen!)).toEqual(payload)
  })
})
