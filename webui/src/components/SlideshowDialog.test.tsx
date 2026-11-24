import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import type { ImageStoreItem } from '../types'
import { SlideshowDialog } from './SlideshowDialog'

const items = [0, 2, 7].map((id, index): ImageStoreItem => ({
  id,
  name: `Smoke-${index + 1}`,
  width: 128,
  height: 250,
  bytes: 8000,
  format: 1,
  segments: 2,
  createdAt: 0,
  compatible: true,
  contentType: 'image',
}))

const render = (action: 'idle' | 'slideshow') => renderToStaticMarkup(
  <SlideshowDialog imageItems={items} storeInfo={{
    width: 128, height: 250, bytesPerImage: 8000, requiredSectors: 2, totalSectors: 22, freeSectors: 14,
    largestFreeRun: 14, fit: 7, contiguousFit: 7, saved: 3, compatible: 3,
    playable: 3, incompatible: 0, slideshow: false, intervalSeconds: 3600,
  }} action={action} onClose={() => undefined} onShow={() => undefined} onStart={() => undefined} onStop={() => undefined} />,
)

describe('device images dialog', () => {
  it('separates compact slot playback from automatic slideshow settings', () => {
    const html = render('idle')
    expect(html).toContain('设备图片')
    expect(html).toContain('当前规格约 10 个图槽 · 已占用 3 个')
    expect(html).toContain('图 1：Smoke-1')
    expect(html).toContain('图 2：Smoke-2')
    expect(html).toContain('图 3：Smoke-3')
    expect(html).toContain('播放所选')
    expect(html).toContain('上一张')
    expect(html).toContain('下一张')
    expect(html).toContain('自动轮播')
  })

  it('keeps manual selection and stepping while automatic slideshow is active', () => {
    const html = render('slideshow')
    expect(html).toContain('手动切换图片')
    expect(html).toContain('上一张')
    expect(html).toContain('下一张')
    expect(html).toContain('播放所选')
    expect(html).toContain('自动轮播正在运行')
    expect(html).toContain('停止轮播')
  })
})
