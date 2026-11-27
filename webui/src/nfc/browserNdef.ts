import { create } from 'zustand'
import { encodeNfcTextRecord } from '../../../packages/inktag-client-core/src/nfc'

export interface PhoneNdefReader {
  onreading?: (() => void) | null
  scan?(options: { signal: AbortSignal }): Promise<void>
  write(message: { records: { recordType: 'text'; lang: string; encoding: string; data: string }[] },
    options: { overwrite: boolean; signal: AbortSignal }): Promise<void>
}

export const usePhoneNfcUnavailable = create(() => false)

export function usePhoneNdefReader() {
  const unavailable = usePhoneNfcUnavailable()
  if (unavailable || !window.isSecureContext || window.top !== window) return undefined
  const Reader = (window as Window & { NDEFReader?: new () => PhoneNdefReader }).NDEFReader
  return typeof Reader === 'function' ? Reader : undefined
}

export function writePhoneCommand(reader: PhoneNdefReader, text: string, signal: AbortSignal) {
  encodeNfcTextRecord(text)
  return reader.write({ records: [{ recordType: 'text', lang: 'en', encoding: 'utf-8', data: text }] },
    { overwrite: true, signal })
}
