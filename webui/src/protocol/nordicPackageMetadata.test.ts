import { expect, it } from 'vitest'
import { readNordicPackageVersion } from './nordicPackageMetadata'
import { otaPackagePresentation } from '../pages/otaPresentation'
import { INKTAG_FIRMWARE_VERSION } from '../generated/version'
const currentVersion = 65536 + INKTAG_FIRMWARE_VERSION.otaVersion
const cat = (...parts: number[][]) => parts.flat()
const variable = (value: number) => {const bytes: number[]=[];do{let n=value%128;value=Math.floor(value/128);if(value)n|=128;bytes.push(n)}while(value);return bytes}
const field = (n:number, value:number) => [(n<<3),...variable(value)]
const nested = (n:number, value:number[]) => [(n<<3)|2,...variable(value.length),...value]
function packet(version=65736, appBytes=69592, duplicate=false) {
 const init=cat(field(1,version),duplicate?field(1,200):[],field(4,0),field(7,appBytes))
 const command=cat(field(1,1),nested(2,init))
 return new Uint8Array(nested(2,cat(nested(1,command),field(2,0),nested(3,Array(64).fill(0x5a)))))
}
it('reads the signed-command metadata structure without modifying bytes or claiming a signature check',()=>{
 const bytes=packet(currentVersion),before=bytes.slice();expect(readNordicPackageVersion(bytes,69592)).toEqual({encodedVersion:currentVersion,compatibilityEpoch:1,releaseNumber:INKTAG_FIRMWARE_VERSION.otaVersion});expect(bytes).toEqual(before)
 const shown=otaPackagePresentation({type:'nordic',payload:{initPacket:bytes,firmware:new Uint8Array(69592),fileName:'misleading-v9.9.9.zip'},summary:'original'},'aesl22_nrf')
 expect(shown.version).toBe(INKTAG_FIRMWARE_VERSION.version+'.0');expect(shown.technicalSummary).toContain(String(currentVersion));expect(shown).not.toHaveProperty('signatureVerified')
})
it('uses an exact raw version number for an unknown release instead of guessing semantic version',()=>{
 const shown=otaPackagePresentation({type:'nordic',payload:{initPacket:packet(131071),firmware:new Uint8Array(69592)},summary:''},'aesl22_nrf');expect([shown.versionLabel,shown.version]).toEqual(['版本编号','131071'])
})
it('does not display ambiguous, truncated, unsigned or size-mismatched metadata',()=>{
 const bytes=packet();for(const bad of [packet(65736,69592,true),new Uint8Array([0x0a,0]),new Uint8Array(4097),new Uint8Array([0x12,0xff]),...Array.from({length:bytes.length},(_,n)=>bytes.slice(0,n))])expect(readNordicPackageVersion(bad,69592)).toBeUndefined()
 expect(readNordicPackageVersion(bytes,1)).toBeUndefined()
})
