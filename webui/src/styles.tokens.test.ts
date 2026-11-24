import { readFileSync } from 'node:fs'
import { expect, it } from 'vitest'

it('uses defined theme tokens so controls and errors retain their light/dark colours', () => {
  const css = readFileSync(new URL('./styles.css', import.meta.url), 'utf8')
  const definitions = new Set(Array.from(css.matchAll(/(--[\w-]+)\s*:/g), match => match[1]))
  const references = new Set(Array.from(css.matchAll(/var\((--[\w-]+)\)/g), match => match[1]))
  expect(Array.from(references).filter(token => !definitions.has(token))).toEqual([])
})
