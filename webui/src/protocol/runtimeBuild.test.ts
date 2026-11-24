import { describe, expect, it } from 'vitest'
import { assertOnMicroOtaAttestation, assertOnMicroOtaBaseline, parseRuntimeBuildInfo } from './runtimeBuild'

function runtimeInfoPayload(generation=2) {
  const strings=new TextEncoder().encode('om6626_zkcxxbIKT_OM_1234label')
  const end=23+strings.length,payload=new Uint8Array(end+16)
  payload.set([1,13,11,5]);payload.set(strings,23)
  const view=new DataView(payload.buffer)
  view.setUint32(15,216400,true)
  payload.set([1,0,4,0],end)
  view.setUint32(end+4,101,true);view.setUint32(end+8,generation,true)
  return payload
}

describe('OM6626 runtime build attestation', () => {
  it('decodes the v1 DEVICE_INFO tail with its buffer byte offset', () => {
    const payload=runtimeInfoPayload(),offset=new Uint8Array(payload.length+6)
    offset.set(payload,3)
    expect(parseRuntimeBuildInfo(offset.subarray(3,-3))).toEqual({
      build: 101,
      appBytes: 216400,
      swapResult: 0,
      swapState: 4,
      swapGeneration: 2,
      startupConfirm: 0,
    })
  })

  it('rejects missing/truncated/foreign tails and inconsistent string lengths', () => {
    const payload=runtimeInfoPayload()
    for(let size=0;size<payload.length;size++) expect(()=>parseRuntimeBuildInfo(payload.slice(0,size))).toThrow()
    for(const index of [0,payload.length-16]) {
      const bad=payload.slice();bad[index]=2
      expect(()=>parseRuntimeBuildInfo(bad)).toThrow()
    }
    const bad=payload.slice();bad[1]=255
    expect(()=>parseRuntimeBuildInfo(bad)).toThrow()
  })

  it('requires the packaged build and a confirmed swap', () => {
    const confirmed = parseRuntimeBuildInfo(runtimeInfoPayload(1))
    expect(() => assertOnMicroOtaAttestation(confirmed, 101, 216400)).not.toThrow()
    expect(() => assertOnMicroOtaAttestation(
      { ...confirmed, swapGeneration: 2 }, 101, 216400, 1,
    )).not.toThrow()
    expect(() => assertOnMicroOtaAttestation(confirmed, 101, 216400, 1)).toThrow(/没有推进/)
    expect(() => assertOnMicroOtaAttestation(
      { ...confirmed, swapState: 1 }, 101, 216400,
    )).toThrow(/没有完成单应用试运行确认/)
    expect(() => assertOnMicroOtaAttestation(confirmed, 102, 216400)).toThrow(/不一致/)
  })

  it('accepts only idle or confirmed OTA baselines', () => {
    const confirmed = parseRuntimeBuildInfo(runtimeInfoPayload(6))
    expect(assertOnMicroOtaBaseline(confirmed)).toBe(6)
    expect(() => assertOnMicroOtaBaseline({ ...confirmed, swapState: 2 })).toThrow(/需要固定 Recovery 处理/)
  })
})
