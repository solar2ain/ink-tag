import { createEditionBadgeFrame, editionBadgeGeometry } from '../../device-gui/editionBadgeBitmap'
import { paletteForColor } from '../../image/processing'
import type { FirmwareActivation } from '../../types'
import type { RgbaFrame } from '../../../../packages/inktag-client-core/src/screen'

export function brandPreview(state: { connection: string; activation: FirmwareActivation; brandVisible: boolean | null }, width: number, height: number, color: number) {
  const pro = state.connection === 'connected' && state.activation.phase === 'active' && state.activation.edition === 'advanced'
  if (pro && state.brandVisible === false) return undefined
  const edition = pro ? 'pro' : 'base'
  return { ...editionBadgeGeometry(width, height, edition), ...createEditionBadgeFrame(edition, paletteForColor(color).length, width, height) }
}

/** Draw after sizing, just as the device overlays its badge on the final frame. */
export function overlayBrandPreview(source: RgbaFrame, state: Parameters<typeof brandPreview>[0], color: number): RgbaFrame {
  const badge=brandPreview(state,source.width,source.height,color)
  if(!badge)return source
  const data=new Uint8ClampedArray(source.data),colors=[[255,255,255],[0,0,0],[220,48,38]]
  for(let y=0;y<badge.height;y++)for(let x=0;x<badge.width;x++) {
    const tx=badge.x+x,ty=badge.y+y
    if(tx<0||ty<0||tx>=source.width||ty>=source.height)continue
    const rgb=colors[badge.pixels[y*badge.width+x]]??colors[1],offset=(ty*source.width+tx)*4
    data.set([...rgb,255],offset)
  }
  // ImageData exposes dimensions through prototype getters, not own fields.
  return {width:source.width,height:source.height,data}
}
