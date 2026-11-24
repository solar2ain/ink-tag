import { afterEach, describe, expect, it, vi } from 'vitest'
import vectors from '../../../tests/fixtures/lzo1x-image.json'
import { buildLzo1xBlocks, encodeLzo1x } from './lzo1x'
import { inktagClient } from './client'
import { CapabilityExtensionV2, CapabilityV2, CommandV2, TransferDescriptorFlagV2 } from './generatedCommandsV2'
import { makeScreenConfig } from './screen'
import type { ImageTransfer } from './image'

const bytes = (hex: string) => Uint8Array.from(hex.match(/../g)?.map(v => parseInt(v,16)) ?? [])
const hex = (data: Uint8Array<ArrayBufferLike>) => Array.from(data,v=>v.toString(16).padStart(2,'0')).join('')
const client = inktagClient as unknown as {
  v2Capabilities: number; v2CapabilityExtensions: number; v2WritePayload: number
  requestV2: (op: number, payload: Uint8Array) => Promise<{payload: Uint8Array}>
  waitForV2Completion: () => Promise<Uint8Array>
  writeV2TransferChunk: (tx: number, object: number, total: number, offset: number, chunk: Uint8Array, decoded?: number) => Promise<void>
  sendCanvasV2: (...args: unknown[]) => Promise<unknown>
}
const previous = {caps:client.v2Capabilities, ext:client.v2CapabilityExtensions, chunk:client.v2WritePayload}
afterEach(()=>{vi.restoreAllMocks();client.v2Capabilities=previous.caps;client.v2CapabilityExtensions=previous.ext;client.v2WritePayload=previous.chunk})

describe('LZO1X shared image codec',()=>{
  for (const row of vectors.vectors) {
    it(`matches native-decoder-validated ${row.name}`,()=>{
      expect(hex(encodeLzo1x(bytes(row.rawHex)))).toBe(row.encodedHex)
      for (const plan of row.plans) {
        const blocks=buildLzo1xBlocks(bytes(row.rawHex),7,plan.encodedLimit)
        expect(blocks.map(b=>({offset:b.decodedOffset,length:b.decodedLength,hex:hex(b.encoded)}))).toEqual(plan.blocks)
      }
    })
  }
  it.each([false,true])('selects device-advertised LZO with RLE advertised=%s',async(rle)=>{
    client.v2Capabilities=rle?CapabilityV2.TRANSFER_PACKBITS_RLE:0
    client.v2CapabilityExtensions=CapabilityExtensionV2.TRANSFER_LZO1X_2000
    client.v2WritePayload=244
    const begins:Uint8Array[]=[]
    vi.spyOn(client,'requestV2').mockImplementation(async(op,payload)=>{
      if(op===CommandV2.TRANSFER_BEGIN)begins.push(payload.slice())
      return {payload:new Uint8Array(0)}
    })
    vi.spyOn(client,'waitForV2Completion').mockResolvedValue(new Uint8Array(0))
    const writes=vi.spyOn(client,'writeV2TransferChunk').mockResolvedValue(undefined)
    const screen=makeScreenConfig({driver:0x33,recipe:1,color:3,transform:0,width:400,height:300,
      visibleX:0,visibleY:0,visibleWidth:400,visibleHeight:300})
    const data=new Uint8Array(30000).fill(0xab)
    const transfers:ImageTransfer[]=[{step:'color',data}]
    await client.sendCanvasV2({} as HTMLCanvasElement,transfers,3,screen,'lzo',false,'image')
    expect(begins).toHaveLength(1)
    expect(begins[0][3]&0xc0).toBe(TransferDescriptorFlagV2.LZO1X)
    let offset=0
    for(const call of writes.mock.calls){
      expect(call[3]).toBe(offset);expect(call[5]).toBeGreaterThan(0);expect(call[5]).toBeLessThanOrEqual(2000)
      expect(call[4]).toEqual(encodeLzo1x(data.subarray(offset,offset+call[5]!)))
      offset+=call[5]!
    }
    expect(offset).toBe(data.length)
  })
})
