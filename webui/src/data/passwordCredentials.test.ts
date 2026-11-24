import { beforeEach, describe, expect, it, vi } from 'vitest'
import { consumePasswordQrLocation, getPasswordCredential, savePasswordCredential, takePasswordQrCredential } from './passwordCredentials'
import { buildPasswordQrLink } from '../protocol/devicePassword'

describe('origin-local password credential isolation', () => {
  const id = '0123456789ABCDEF'
  const key = new Uint8Array(32).fill(7)
  beforeEach(() => {
    const memory = new Map<string,string>()
    Object.defineProperty(globalThis,'localStorage',{configurable:true,value:{
      getItem:(k:string)=>memory.get(k) ?? null,
      setItem:(k:string,v:string)=>memory.set(k,v), removeItem:(k:string)=>memory.delete(k),
    }})
  })
  it('validates values and can clear only this local device record', () => {
    expect(savePasswordCredential(id,key)).toBe(true)
    expect(getPasswordCredential(id.toLowerCase())).toEqual(key)
    expect(()=>savePasswordCredential('0000000000000000',key)).toThrow()
    expect(()=>savePasswordCredential(id,new Uint8Array(32))).toThrow()
    expect(savePasswordCredential(id,null)).toBe(true)
    expect(getPasswordCredential(id)).toBeNull()
  })
  it('scrubs QR fragment before exposing only a matching one-use candidate', () => {
    const href = buildPasswordQrLink('IKT_TL_1234',id,key)
    Object.defineProperty(globalThis,'location',{configurable:true,value:{href,hash:new URL(href).hash,pathname:'/ink-tag/',search:'?ble=IKT_TL_1234'}})
    const replaceState = vi.fn()
    Object.defineProperty(globalThis,'history',{configurable:true,value:{replaceState}})
    consumePasswordQrLocation()
    expect(replaceState).toHaveBeenCalledWith(null,'','/ink-tag/?ble=IKT_TL_1234#/')
    expect(getPasswordCredential(id)).toBeNull()
    expect(takePasswordQrCredential('FEDCBA9876543210')).toBeNull()
    expect(takePasswordQrCredential(id)).toEqual(key)
    expect(takePasswordQrCredential(id)).toBeNull()
  })
})
