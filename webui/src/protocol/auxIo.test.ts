import { expect, it } from 'vitest'
import { decodeAuxIoConfiguration, emptyAuxIoConfiguration, encodeAuxIoConfiguration } from './auxIo'

it('shares the strict shipping-C three-button vector without compacting slots', () => {
  const wire = new Uint8Array([1, 1, 3, 2, 4, 3, 0, 3, 4, 255, 0xd0, 7])
  expect([...encodeAuxIoConfiguration(decodeAuxIoConfiguration(wire))]).toEqual([...wire])
  const config = decodeAuxIoConfiguration(wire)
  config.buttons[1] = { gpio: 255, activeLow: false, pull: 'none' }
  expect([...encodeAuxIoConfiguration(config)].slice(1, 7)).toEqual([1, 3, 255, 0, 3, 0])
})
it.each([
  [1, 1, 9, 2, 4, 3, 0, 3, 4, 255, 0xd0, 7],
  [1, 255, 1, 2, 4, 3, 0, 3, 4, 255, 0xd0, 7],
  [1, 1, 3, 1, 4, 3, 0, 3, 4, 255, 0xd0, 7],
  [3, 1], [2, 1, 3, 2, 4, 3, 0, 3, 4, 255, 0xd0, 7],
])('rejects malformed or conflicting data: %j', (...wire: number[]) => {
  expect(() => decodeAuxIoConfiguration(new Uint8Array(wire))).toThrow()
})
it('never imports an old single-button block or silently drops extra buttons', () => {
  expect(() => decodeAuxIoConfiguration(new Uint8Array([1, 17, 3, 0, 255, 255, 0xd0, 7]))).toThrow()
  const empty = emptyAuxIoConfiguration()
  expect(decodeAuxIoConfiguration(encodeAuxIoConfiguration(empty))).toEqual(empty)
  empty.buttons[1] = { gpio: 18, activeLow: true, pull: 'up' }
  expect(decodeAuxIoConfiguration(encodeAuxIoConfiguration(empty)).buttons[1].gpio).toBe(18)
})
it('rejects the old disabled-button representation even when its first byte is one', () => {
  const old = new Uint8Array([1, 255, 3, 0, 255, 255, 0xd0, 7])
  expect(() => decodeAuxIoConfiguration(old)).toThrow()
})

it('roundtrips all byte values in every opaque tail position without pin conflicts', () => {
  const wire = new Uint8Array([1, 1, 3, 2, 4, 3, 0, 0, 255, 255, 208, 7])
  for (let slot = 7; slot < 12; ++slot) for (let value = 0; value < 256; ++value) {
    const candidate = wire.slice(); candidate[slot] = value
    expect(encodeAuxIoConfiguration(decodeAuxIoConfiguration(candidate))).toEqual(candidate)
  }
})
