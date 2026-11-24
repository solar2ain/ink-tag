import { loadOptionalAsset } from './optionalAssets'

import { calligraphyFamily } from './bundledFontMetadata'
export { calligraphyFamily, calligraphyFont } from './bundledFontMetadata'
let loading: Promise<void> | null = null

/** Canvas must not measure or export a silently substituted font. */
export function ensureBundledFont(familyOrDocument: string): Promise<void> {
  if (!familyOrDocument.includes('Ma Shan Zheng') && !familyOrDocument.includes('ma-shan-zheng')) return Promise.resolve()
  if (!loading) {
    loading = loadOptionalAsset('calligraphy').then(async data => {
      // One payload supplies both the editor and exported-document aliases.
      const bytes = Uint8Array.from(atob(data.split(',')[1]), character => character.charCodeAt(0))
      const faces = await Promise.all(['InkTag Ma Shan Zheng', 'Ma Shan Zheng'].map(async family => {
        const face = new FontFace(family, bytes)
        await face.load()
        return face
      }))
      for (const face of faces) document.fonts.add(face)
      return document.fonts.load(`24px ${calligraphyFamily}`)
    }).then((faces) => {
      if (!faces.length || faces.some((face) => face.status !== 'loaded')) throw new Error('内置书法字体未能加载，请重试；未使用系统字体代替。')
    }).catch(() => { loading = null; throw new Error('书法字体资源未能加载，请补齐 assets 中的可选资源后重试；未使用系统字体代替。') })
  }
  return loading
}
