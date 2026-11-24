import { expect,it } from 'vitest'
import { readFileSync } from 'node:fs'
import source from '../ota-drivers/telink_ota.js?raw'

async function fixture(){
  const host:any={};new Function('window',source)(host)
  const a=host.TelinkOta
  const raw=new Uint8Array(readFileSync(new URL('../../../tests/fixtures/base-ota/telink-crc-only.zip',import.meta.url)))
  const pkg=await a.parsePackage({name:'offline.zip',size:raw.length,arrayBuffer:async()=>raw.buffer})
  return {a,pkg}
}
it('parses the same native base body/CRC/entry context as Python',async()=>{
  const {pkg}=await fixture();expect(pkg.signatureStatus).toBe('unsigned');expect(pkg.firmware.length).toBe(516)
})
it.each([[],[73,75,82,86,1,0,1,1],[73,75,82,86,1,0,2,1,73,75,83,80,1,0],
  [73,75,82,86,1,0,2,1,73,75,83,80,1,2,0]].map(bytes=>({bytes})))('rejects an old or malformed policy before other queries: $bytes',async ({bytes})=>{
    const {a,pkg}=await fixture();const calls:string[]=[]
    const service={getCharacteristic:async(uuid:string)=>{calls.push(uuid);return {properties:{read:true},readValue:async()=>new DataView(new Uint8Array(bytes).buffer)}}}
    await expect(a.checkRecoveryCompatibility(service,pkg.firmware)).rejects.toThrow()
    expect(calls).toEqual(['00010203-0405-0607-0809-0a0b0c0d2b15'])
})
it('retains the original context and entry proof after exact CRC-only readback',async()=>{
  const {a,pkg}=await fixture();const calls:string[]=[]
  const records:Record<string,Uint8Array>={
    '00010203-0405-0607-0809-0a0b0c0d2b15':new Uint8Array([73,75,82,86,1,0,2,1,73,75,83,80,1,2]),
    '00010203-0405-0607-0809-0a0b0c0d2b13':new Uint8Array(20),
    '00010203-0405-0607-0809-0a0b0c0d2b14':pkg.firmware.slice(0x140,0x154),
  }
  const context=records['00010203-0405-0607-0809-0a0b0c0d2b13'];context.set([73,84,67,49])
  const v=new DataView(context.buffer);[0x3f000,1,0x31564c54,0x53484c54].forEach((n,i)=>v.setUint32(4+i*4,n,true))
  const service={getCharacteristic:async(uuid:string)=>{calls.push(uuid);return {properties:{read:true},readValue:async()=>new DataView(records[uuid].buffer)}}}
  await expect(a.checkRecoveryCompatibility(service,pkg.firmware)).resolves.toBeUndefined()
  expect(calls).toEqual(Object.keys(records))
})
