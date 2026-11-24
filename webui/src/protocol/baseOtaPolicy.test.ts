import { describe, expect, it, vi } from 'vitest'
import { readFileSync } from 'node:fs'
import { inflateRawSync } from 'node:zlib'
import { webcrypto } from 'node:crypto'
import install from '../ota-drivers/signed_preflight.js?ota-adapter'
import installNordic from '../ota-drivers/nordic_dfu.js?ota-adapter'
import installDialog from '../ota-drivers/dialog_suota.js?ota-adapter'

const host: any = { crypto: webcrypto }
install(host); installNordic(host); installDialog(host)
const v = host.InkTagSignedOta
const fixture = (name: string) => new Uint8Array(readFileSync(new URL('../../../tests/fixtures/signed-ota/' + name, import.meta.url)))
function zip(name: string) {
  const source = Buffer.from(fixture(name)), files: Record<string, Uint8Array> = {}
  let end = source.length - 22
  while (source.readUInt32LE(end) !== 0x06054b50) end--
  let at = source.readUInt32LE(end + 16)
  for (let n = 0; n < source.readUInt16LE(end + 10); n++) {
    const size=source.readUInt32LE(at+20),offset=source.readUInt32LE(at+42),nl=source.readUInt16LE(at+28)
    const start=offset+30+source.readUInt16LE(offset+26)+source.readUInt16LE(offset+28)
    const body=source.subarray(start,start+size)
    files[source.subarray(at+46,at+46+nl).toString()]=new Uint8Array(source.readUInt16LE(at+10)===8?inflateRawSync(body):body)
    at+=46+nl+source.readUInt16LE(at+30)+source.readUInt16LE(at+32)
  }
  const m=JSON.parse(new TextDecoder().decode(files['manifest.json'])).manifest.application
  return {firmware:files[m.bin_file],initPacket:files[m.dat_file]}
}
function fields(bytes: Uint8Array) {
  let at=0
  const variable=()=>{let out=0,factor=1,next;do{next=bytes[at++];out+=(next&127)*factor;factor*=128}while(next&128);return out}
  const out=new Map<number,any>()
  while(at<bytes.length){const tag=variable(),value=variable();if((tag&7)===2){out.set(tag>>3,bytes.slice(at,at+value));at+=value}else out.set(tag>>3,value)}
  return out
}
function unsignedNordic(target='nrf52811_aesl') {
  const pkg=zip(target+'.zip'),command=fields(fields(pkg.initPacket).get(2)).get(1) as Uint8Array
  let n=command.length;const length=[]
  do{let b=n&127;n>>>=7;if(n)b|=128;length.push(b)}while(n)
  return {...pkg,initPacket:new Uint8Array([10,...length,...command])}
}
function crc(image: Uint8Array) {
  new DataView(image.buffer).setUint32(8,host.DialogSuota.crc32(image.slice(64)),true)
}
async function unsignedDialog() {
  const image=fixture('da14585_aesl.img'),view=new DataView(image.buffer),at=image.length-96
  view.setUint32(at,0x31554b49,true);image[at+5]=0
  view.setUint32(at+24,0x31554144,true);view.setUint32(60,0x31554144,true)
  const digest=new Uint8Array(await webcrypto.subtle.digest('SHA-256',image.slice(64,-64)))
  image.fill(0,image.length-64);image.set(digest,image.length-64);crc(image)
  return image
}

describe('base-only optional signatures, with exact live Recovery policy',()=>{
  it.each(['nordic','dialog'])('keeps unsigned %s inspection separate from write permission',async transport=>{
    const pkg=transport==='nordic'?unsignedNordic():await unsignedDialog()
    const info=transport==='nordic'?await v.inspectNordic((pkg as any).firmware,(pkg as any).initPacket):await v.inspectDialog(pkg)
    expect(info).toMatchObject({publisherVerified:false,signatureStatus:'unsigned'})
    const strict=()=>transport==='nordic'?v.nordic((pkg as any).firmware,(pkg as any).initPacket):v.dialog(pkg)
    await expect(strict()).rejects.toThrow()
    const proof=new Uint8Array(transport==='nordic'?[193]:[13,73,75,1,1,83,80,1,0])
    expect(()=>v.requireRecoverySignature(info,transport,proof)).not.toThrow()
    for(const bad of [new Uint8Array(),new Uint8Array([129]),new Uint8Array([13,73,75,1,1]),new Uint8Array([...proof,0])])
      expect(()=>v.requireRecoverySignature(info,transport,bad)).toThrow()
  })
  it('requires new CRC-only Recovery for unsigned nRF52832; never accepts a mixed packet',async()=>{
    const other=unsignedNordic('nrf52832_priceetag')
    const info=await v.inspectNordic(other.firmware,other.initPacket)
    expect(()=>v.requireRecoverySignature(info,'nordic',new Uint8Array([225]))).not.toThrow()
    for(const bad of [[],[129],[193],[225,0]])
      expect(()=>v.requireRecoverySignature(info,'nordic',new Uint8Array(bad))).toThrow()
    const pkg=unsignedNordic()
    await expect(v.inspectNordic(pkg.firmware,new Uint8Array([...pkg.initPacket,18,0]))).rejects.toThrow()
    pkg.firmware[100]^=1
    await expect(v.inspectNordic(pkg.firmware,pkg.initPacket)).rejects.toThrow()
  })
  it.each(['nordic','dialog'])('requires exact CRC-only %s readback',async transport=>{
    const pkg=transport==='nordic'?unsignedNordic():await unsignedDialog()
    const info=transport==='nordic'?await v.inspectNordic((pkg as any).firmware,(pkg as any).initPacket):await v.inspectDialog(pkg)
    const proof=new Uint8Array(transport==='nordic'?[225]:[13,73,75,1,1,83,80,1,2])
    expect(v.recoveryPolicy(transport,proof)).toBe('crc-only')
    expect(v.recoverySafeResume(transport,proof)).toBe(true)
    expect(()=>v.requireRecoverySignature(info,transport,proof)).not.toThrow()
    for(const bad of [proof.slice(0,-1),new Uint8Array([...proof,0]),new Uint8Array([...proof,...proof])]){
      expect(v.recoveryPolicy(transport,bad)).toBe('required')
      expect(v.recoverySafeResume(transport,bad)).toBe(false)
      expect(()=>v.requireRecoverySignature(info,transport,bad)).toThrow()
    }
  })
  it('does not downgrade a bad signed package even with optional Recovery',async()=>{
    const pkg=zip('nrf52811_aesl.zip');pkg.initPacket[pkg.initPacket.length-1]^=1
    await expect(v.inspectNordic(pkg.firmware,pkg.initPacket)).rejects.toThrow()
    const image=fixture('da14585_aesl.img');image[image.length-1]^=1;crc(image)
    await expect(v.inspectDialog(image)).rejects.toThrow()
  })
  it('rejects DA unsigned payload changes with a freshly forged outer CRC',async()=>{
    const image=await unsignedDialog();image[100]^=1;crc(image)
    await expect(v.inspectDialog(image)).rejects.toThrow('哈希')
  })
  it.each([[],[129],[193,0],[192]].map(bytes=>({bytes})))('rejects unsigned Nordic before PRN/CREATE with reply $bytes',async ({bytes})=>{
    const pkg=unsignedNordic(),info=await v.inspectNordic(pkg.firmware,pkg.initPacket)
    const dfu=new host.NordicDfu.SecureDfuTransport({})
    dfu.open=vi.fn(async()=>{});dfu.close=vi.fn(async()=>{})
    dfu.request=vi.fn(async()=>new Uint8Array(bytes));dfu.sendCommand=vi.fn()
    await expect(dfu.transfer(pkg,info)).rejects.toThrow('要求发布者签名')
    expect(dfu.open).toHaveBeenCalledWith(false)
    expect(dfu.request.mock.calls.map((x:any[])=>x[0])).toEqual([0])
    expect(dfu.sendCommand).not.toHaveBeenCalled()
  })
  it.each([[],[13,73,75,1,1],[13,73,75,1,1,83,80,1,1]].map(bytes=>({bytes})))('rejects unsigned DA before opening transfer or Flash with reply $bytes',async ({bytes})=>{
    const image=await unsignedDialog()
    const characteristic={readValue:vi.fn(async()=>new DataView(new Uint8Array(bytes).buffer))}
    const service={getCharacteristic:vi.fn(async()=>characteristic)}
    const server={getPrimaryService:vi.fn(async()=>service)}
    await expect(host.DialogSuota.transferServer(server,{image})).rejects.toThrow('要求发布者签名')
    expect(service.getCharacteristic).toHaveBeenCalledExactlyOnceWith(host.DialogSuota.VERSION_UUID)
  })
  it.each(['nordic','dialog'])('matches read-only %s policy to guarded resume',async transport=>{
    const optional=new Uint8Array(transport==='nordic'?[193]:[13,73,75,1,1,83,80,1,0])
    expect(v.recoveryPolicy(transport,optional)).toBe('optional')
    expect(v.recoverySafeResume(transport,optional)).toBe(true)
    expect(v.recoveryPolicy(transport,new Uint8Array([...optional,0]))).toBe('required')
    expect(v.recoverySafeResume(transport,new Uint8Array([...optional,0]))).toBe(false)
  })
  it('keeps the signed Nordic transfer negotiation unchanged',async()=>{
    const pkg=zip('nrf52811_aesl.zip'),info=await v.inspectNordic(pkg.firmware,pkg.initPacket)
    const dfu=new host.NordicDfu.SecureDfuTransport({})
    dfu.open=vi.fn(async()=>{});dfu.close=vi.fn(async()=>{})
    dfu.request=vi.fn();dfu.sendCommand=vi.fn(async()=>{})
    dfu.prepareResume=vi.fn(async()=>({offset:0}));dfu.sendFirmware=vi.fn(async()=>{})
    await dfu.transfer(pkg,info)
    expect(dfu.open).toHaveBeenCalledExactlyOnceWith()
    expect(dfu.request).not.toHaveBeenCalled()
    expect(dfu.sendCommand).toHaveBeenCalledExactlyOnceWith(pkg.initPacket)
  })
  it('allows the exact optional Nordic policy and sends the original command and image',async()=>{
    const pkg=unsignedNordic(),info=await v.inspectNordic(pkg.firmware,pkg.initPacket)
    const dfu=new host.NordicDfu.SecureDfuTransport({})
    dfu.open=vi.fn(async()=>{});dfu.close=vi.fn(async()=>{})
    dfu.request=vi.fn(async(op:number)=>new Uint8Array(op===0?[193]:[]))
    dfu.sendCommand=vi.fn(async()=>{});dfu.prepareResume=vi.fn(async()=>({offset:0}))
    dfu.sendFirmware=vi.fn(async()=>{})
    await dfu.transfer(pkg,info)
    expect(dfu.request.mock.calls.map((x:any[])=>x[0])).toEqual([0,2])
    expect(dfu.sendCommand).toHaveBeenCalledExactlyOnceWith(pkg.initPacket)
    expect(dfu.sendFirmware).toHaveBeenCalledExactlyOnceWith(pkg.firmware,{offset:0})
  })
})
