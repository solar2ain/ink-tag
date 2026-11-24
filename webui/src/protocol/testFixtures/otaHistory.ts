import { readFileSync } from 'node:fs'
import { createHash } from 'node:crypto'
import manifest from './history/manifest.json'

/** Immutable old client bytes for regression reproduction, without Git access. */
export function historicalOta(file: keyof typeof manifest): string {
  const expected=manifest[file]
  const data=readFileSync(new URL(`./history/${file}`,import.meta.url))
  if(data.length!==expected.bytes || createHash('sha256').update(data).digest('hex')!==expected.sha256)
    throw new Error('Historical OTA client fixture differs from its fixed source')
  return data.toString('utf8')
}
