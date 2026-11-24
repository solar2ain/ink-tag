import { expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import source from '../ota-drivers/onmicro_ota.js?raw'
import { assertOnMicroOtaAttestation } from './runtimeBuild'

function adapter() {
  const host:any={}
  new Function('window',source)(host)
  return host.OnMicroOta
}
async function fixture() {
  const bytes=new Uint8Array(readFileSync(new URL('../../../tests/fixtures/base-ota/onmicro-crc-only.zip',import.meta.url)))
  const a=adapter()
  const pkg=await a.parsePackage({name:'offline-free.zip',size:bytes.length,arrayBuffer:async()=>bytes.buffer})
  const record=new Uint8Array(26),view=new DataView(record.buffer)
  view.setUint32(0,0x50524b49,true);record[4]=1;view.setUint16(6,20,true)
  view.setUint32(8,0x31565a4f,true);view.setUint32(12,0x4e000-96,true);view.setUint32(16,0x4e000,true)
  record.set([73,75,83,80,1,2],20)
  return {a,pkg,record}
}
it('parses a native unsigned package, but old or malformed live policy never permits a write',async()=>{
  const {a,pkg,record}=await fixture()
  expect(pkg.signatureStatus).toBe('unsigned')
  expect(()=>a.validateRecoveryPreflight(record,pkg)).not.toThrow()
  for(const bad of [record.slice(0,20),record.slice(0,25),new Uint8Array([...record,0]),new Uint8Array([...record,...record])])
    expect(()=>a.validateRecoveryPreflight(bad,pkg)).toThrow()
  for(const policy of [0,1,3]){
    record[25]=policy
    expect(()=>a.validateRecoveryPreflight(record,pkg)).toThrow()
  }
})
it.each([0,100,512,516,517,518,520,524,528,532,536,540,544,548])('rejects body/descriptor mutation at %i even with a rewritten descriptor CRC',async offset=>{
  const {a,pkg,record}=await fixture()
  pkg.firmware[offset]^=1
  if(offset>=512&&offset<548)new DataView(pkg.firmware.buffer).setUint32(548,a.crc32(pkg.firmware.slice(512,548)),true)
  expect(()=>a.validateRecoveryPreflight(record,pkg)).toThrow()
})
it.each(['board','layout','releaseVersion','compatibilityEpoch','sourceProfile'])('binds %s to the exact native payload',async field=>{
  const {a,pkg,record}=await fixture()
  pkg.manifest[field]=typeof pkg.manifest[field]==='number'?pkg.manifest[field]+1:'wrong'
  expect(()=>a.validateRecoveryPreflight(record,pkg)).toThrow()
})

it.each([false,true])('uses verified linked body bytes for post-OTA attestation (signed=%s)',async signed=>{
  const {a,pkg}=await fixture()
  if(signed){
    pkg.manifest.signature={required:true}
    pkg.firmware=new Uint8Array([...pkg.firmware,...new Uint8Array(96)])
  }
  const size=a.applicationBodySize(pkg)
  expect(size).toBe(512)
  const runtime={build:201,swapState:4,swapGeneration:2,appBytes:512,swapResult:0,startupConfirm:0}
  expect(()=>assertOnMicroOtaAttestation(runtime,201,size,1)).not.toThrow()
  expect(()=>assertOnMicroOtaAttestation({...runtime,appBytes:552},201,size,1)).toThrow()
  expect(()=>assertOnMicroOtaAttestation({...runtime,swapGeneration:1},201,size,1)).toThrow()
})
it('keeps legacy signed size and rejects corrupt present IKCI in signed payloads',async()=>{
  const {a,pkg}=await fixture()
  pkg.manifest.signature={required:true}
  const saved=pkg.firmware.slice()
  pkg.firmware=new Uint8Array(648)
  expect(a.applicationBodySize(pkg)).toBe(552)
  for(const at of [0,516,520,528,532,536,540,548]){
    pkg.firmware=new Uint8Array([...saved,...new Uint8Array(96)])
    pkg.firmware[at]^=1
    expect(()=>a.applicationBodySize(pkg)).toThrow()
  }
})
