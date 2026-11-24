import { describe, expect, it } from 'vitest'
import { NfcCommandV2 } from './generatedCommandsV2'
import {
  decodeNfcCapabilities, decodeNfcCommandText, decodeNfcNdef, decodeNfcStatus,
  encodeNfcCommandText, encodeNfcNdef, encodeNfcUriRecord, nfcCommandSequenceIsNewer, nfcCommandCrc32,
} from './nfcCommand'

describe('NFC command text envelope', () => {
  it('roundtrips SET_TIME as exactly four little-endian UTC bytes', () => {
    const record = { sequence: 9, opcode: NfcCommandV2.SET_TIME,
      payload: new Uint8Array([0x78, 0x56, 0x34, 0x12]) }
    expect(decodeNfcCommandText(encodeNfcCommandText(record))).toEqual(record)
    for (const length of [0, 1, 2, 3, 5, 6, 7, 8]) {
      expect(() => encodeNfcCommandText({ ...record, payload: new Uint8Array(length) })).toThrow()
      const prefix = `IKNC1:00000009:0C:${'00'.repeat(length)}:`
      const crc = nfcCommandCrc32(new TextEncoder().encode(prefix)).toString(16).padStart(8, '0')
      expect(() => decodeNfcCommandText(prefix + crc)).toThrow('负载')
    }
  })
  it('matches the cross-language frozen vector', () => {
    const encoded = encodeNfcCommandText({
      sequence: 0x12345678,
      opcode: NfcCommandV2.SHOW_SLOT,
      payload: new Uint8Array([42]),
    })
    expect(encoded).toBe('IKNC1:12345678:07:2A:046960E9')
    const decoded = decodeNfcCommandText(encoded)
    expect(decoded.sequence).toBe(0x12345678)
    expect(decoded.opcode).toBe(NfcCommandV2.SHOW_SLOT)
    expect(Array.from(decoded.payload)).toEqual([42])
  })

  it('rejects corruption and unsafe opcodes', () => {
    expect(() => decodeNfcCommandText('IKNC1:12345678:07:2A:046960E8')).toThrow('CRC')
    expect(() => encodeNfcCommandText({
      sequence: 1, opcode: 0x40, payload: new Uint8Array(),
    })).toThrow('无效')
  })

  it('encodes sequence zero as a reusable physical action tag', () => {
    const encoded = encodeNfcCommandText({
      sequence: 0,
      opcode: NfcCommandV2.CALENDAR,
      payload: new Uint8Array(),
    })
    expect(encoded).toBe('IKNC1:00000000:03::936EA733')
    expect(decodeNfcCommandText(encoded).sequence).toBe(0)
  })

  it('uses serial-number arithmetic only for one-shot sequences', () => {
    expect(nfcCommandSequenceIsNewer(1, 0xffffffff)).toBe(true)
    expect(nfcCommandSequenceIsNewer(0, 0xffffffff)).toBe(false)
    expect(nfcCommandSequenceIsNewer(0xffffffff, 1)).toBe(false)
  })

  it('keeps capability, status, UID and NDEF records bounded', () => {
    expect(decodeNfcCapabilities(new Uint8Array([
      1, 0xff, 0, 0xfe, 0x03, 0xc0, 0, 8, 10, 1,
    ]))).toEqual({
      flags: 0xff, commandMask: 0x03fe, maxNdefBytes: 192,
      maxCommandPayload: 8, maxUidBytes: 10, commandFormatVersion: 1,
    })
    const status = new Uint8Array(20)
    status.set([1, 5, 2, NfcCommandV2.CLOCK, 9, 0, 0, 0, 4, 1, 2, 3, 4])
    expect(decodeNfcStatus(status)).toEqual({
      flags: 5, lastResult: 2, lastOpcode: NfcCommandV2.CLOCK,
      lastSequence: 9, uid: new Uint8Array([1, 2, 3, 4]), diagnosticFlags: 0,
    })
    const ndef = new Uint8Array([0xd1, 1, 0, 0x54])
    expect(decodeNfcNdef(encodeNfcNdef(ndef))).toEqual(ndef)
    expect(() => encodeNfcNdef(new Uint8Array(193))).toThrow('192')
  })

  it('encodes the managed WebUI address as one short HTTPS URI record', () => {
    const value = 'https://solar2ain.github.io/ink-tag/?ble=InkTag_%E5%AE%A2%E5%8E%85'
    const record = encodeNfcUriRecord(value)
    expect([...record.slice(0, 5)]).toEqual([0xd1, 0x01, record.length - 4, 0x55, 0x04])
    expect(new TextDecoder().decode(record.slice(5))).toBe(value.slice('https://'.length))
    expect(() => encodeNfcUriRecord('http://example.com')).toThrow('HTTPS')
  })
})
