import { describe, it, expect } from 'vitest'
import { readFileSync } from 'node:fs'
import { inflateRawSync } from 'node:zlib'
import { webcrypto } from 'node:crypto'
import install from '../ota-drivers/signed_preflight.js?ota-adapter'

const host: any = { crypto: webcrypto }
install(host)
const verifier = host.InkTagSignedOta
const fixture = (name: string) => new Uint8Array(readFileSync(new URL('../../../tests/fixtures/signed-ota/' + name, import.meta.url)))
// Fixture unpacking only, using central-directory sizes from the real native ZIP.
function nordic(target: string) {
  const source = Buffer.from(fixture(target + '.zip')), files: Record<string, Uint8Array> = {}
  let end = source.length - 22
  while (source.readUInt32LE(end) !== 0x06054b50) end--
  let at = source.readUInt32LE(end + 16)
  for (let n = 0; n < source.readUInt16LE(end + 10); n++) {
    const nameLen = source.readUInt16LE(at + 28), extra = source.readUInt16LE(at + 30), comment = source.readUInt16LE(at + 32)
    const method = source.readUInt16LE(at + 10), size = source.readUInt32LE(at + 20), offset = source.readUInt32LE(at + 42)
    const start = offset + 30 + source.readUInt16LE(offset + 26) + source.readUInt16LE(offset + 28)
    const body = source.subarray(start, start + size)
    files[source.subarray(at + 46, at + 46 + nameLen).toString()] = new Uint8Array(method === 8 ? inflateRawSync(body) : body)
    at += 46 + nameLen + extra + comment
  }
  const m = JSON.parse(new TextDecoder().decode(files['manifest.json'])).manifest.application
  return { app: files[m.bin_file], dat: files[m.dat_file] }
}

describe('production public-key preflight, actual signed native formats', () => {
  it('accepts DA publisher signature and v1 context', async () => {
    expect(await verifier.dialog(fixture('da14585_aesl.img'))).toMatchObject({publisherVerified:true,layout:'DAV1',releaseVersion:200})
  })
  it.each([100, -20, -76, -84, 12, 3])('rejects changed DA code/signature/layout/epoch/version/role at %s', async offset => {
    const image=fixture('da14585_aesl.img');image[offset<0?image.length+offset:offset]^=1
    // Recompute the untrusted outer CRC, as an attacker can.
    let crc=0xffffffff;for(const byte of image.slice(64)){crc^=byte;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0)}
    new DataView(image.buffer).setUint32(8,(~crc)>>>0,true)
    await expect(verifier.dialog(image)).rejects.toThrow()
  })
  it.each(['nrf52811_aesl','nrf52832_priceetag'])('authenticates %s and rejects tampering/mispairing', async target => {
    const {app,dat}=nordic(target)
    expect(await verifier.nordic(app,dat)).toMatchObject({publisherVerified:true,compatibilityEpoch:1})
    const changed=app.slice();changed[100]^=1
    await expect(verifier.nordic(changed,dat)).rejects.toThrow()
    const bad=dat.slice();bad[bad.length-1]^=1
    await expect(verifier.nordic(app,bad)).rejects.toThrow()
    const other=nordic(target==='nrf52811_aesl'?'nrf52832_priceetag':'nrf52811_aesl')
    await expect(verifier.nordic(app,other.dat)).rejects.toThrow()
  })
})
