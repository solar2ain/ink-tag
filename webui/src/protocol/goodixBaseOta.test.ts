import {expect,it} from 'vitest'
import {readFileSync} from 'node:fs'
import {goodixFixture} from './test-support/goodixOta'

async function fixture(){
 const f=goodixFixture()
 const bytes=new Uint8Array(readFileSync(new URL('../../../tests/fixtures/base-ota/goodix-crc-only.zip',import.meta.url)))
 const pkg=await f.ota.parsePackage({name:'offline.zip',size:bytes.length,arrayBuffer:async()=>bytes.buffer})
 return {f,pkg}
}
it('parses native body + IKCI + SDK image-info without a signing path',async()=>{
 const {pkg}=await fixture();expect(pkg.firmwareType).toBe(0);expect(pkg.signatureStatus).toBe('unsigned');expect(pkg.imageInfo.length).toBe(48)
})
it('rejects unsigned base before any DFU mutation on the old live Recovery',async()=>{
 const {f,pkg}=await fixture()
 await expect(f.ota.acquireRecoverySession({device:f.device,generation:1,isCurrent:()=>true},pkg)).rejects.toThrow('要求发布者签名')
 expect(f.commands).toEqual([1])
})
it('retains live target attestation and an opaque session after exact CRC-only readback',async()=>{
 const {pkg}=await fixture()
 const info=new Uint8Array([1,...new Uint8Array(16),2,0,0,73,75,82,86,1,0,2,1,73,75,83,80,1,2])
 const f=goodixFixture(info)
 const handle=await f.ota.acquireRecoverySession({device:f.device,generation:1,isCurrent:()=>true},pkg)
 expect(f.commands).toEqual([1,0x42])
 await f.ota.releaseRecoverySession(handle)
 expect(f.commands).toEqual([1,0x42])
})
