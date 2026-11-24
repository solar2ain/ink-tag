import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import type { Plugin } from 'vite'

/** Compile trusted legacy IIFEs into module functions; never evaluate text in the browser. */
export function otaAdaptersPlugin(root: string): Plugin {
  const allowed = new Set(['signed_preflight', 'ota_rejection', 'nordic_dfu', 'goodix_ota', 'onmicro_ota', 'dialog_suota', 'telink_ota']
    .map(name => resolve(root, 'src/ota-drivers', name + '.js')))
  return {
    name: 'inktag-compiled-ota-adapters',
    enforce: 'pre',
    load(id) {
      if (!id.endsWith('?ota-adapter')) return
      const file = resolve(id.slice(0, -'?ota-adapter'.length))
      if (!allowed.has(file)) throw new Error('Unexpected OTA adapter source')
      this.addWatchFile(file)
      return `export default function install(window) {\n${readFileSync(file, 'utf8')}\n}\n`
    },
  }
}
