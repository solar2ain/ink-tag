import { ChevronLeft, ChevronRight, Image, Pause, Play, Timer, X } from 'lucide-react'
import { useEffect, useMemo, useState } from 'react'
import { adjacentPlayableImage } from '../imageSlotNavigation'
import {
  DEFAULT_SLIDESHOW_MINUTES,
  MAX_SLIDESHOW_MINUTES,
  NORMAL_SLIDESHOW_MINUTES,
  normalizeSlideshowMinutes,
  slideshowIntervalSeconds,
} from '../slideshow/interval'
import type { DeviceAction, ImageStoreInfo, ImageStoreItem } from '../types'

interface SlideshowDialogProps {
  imageItems: ImageStoreItem[]
  storeInfo?: ImageStoreInfo | null
  action: DeviceAction
  initialSelectedId?: number | null
  onClose: () => void
  onSelect?: (id: number) => void
  onShow: (id: number) => void
  onStart: (intervalSeconds: number) => void
  onStop: () => void
}

const intervalPresets = [30, 60, 120, 360]

export function SlideshowDialog({ imageItems, storeInfo = null, action, initialSelectedId = null, onClose, onSelect, onShow, onStart, onStop }: SlideshowDialogProps) {
  const [minutes, setMinutes] = useState(DEFAULT_SLIDESHOW_MINUTES)
  const playableItems = useMemo(() => imageItems.filter((item) => item.compatible && item.contentType === 'image'), [imageItems])
  const [selectedId, setSelectedId] = useState(() => playableItems.some((item) => item.id === initialSelectedId) ? initialSelectedId : playableItems[0]?.id ?? null)
  const minimum = NORMAL_SLIDESHOW_MINUTES
  const normalizedMinutes = normalizeSlideshowMinutes(minutes, false)
  const invalidMinutes = !Number.isFinite(minutes) || !Number.isInteger(minutes) || minutes < minimum || minutes > MAX_SLIDESHOW_MINUTES
  const slideshowActive = action === 'slideshow'
  const busy = action !== 'idle' && !slideshowActive
  const selectedIndex = playableItems.findIndex((item) => item.id === selectedId)
  const selected = selectedIndex >= 0 ? playableItems[selectedIndex] : null
  const occupiedSlots = storeInfo?.saved ?? imageItems.length
  const totalSlots = storeInfo && storeInfo.requiredSectors > 0
    ? occupiedSlots + storeInfo.contiguousFit
    : null

  useEffect(() => {
    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') onClose()
    }
    window.addEventListener('keydown', closeOnEscape)
    return () => window.removeEventListener('keydown', closeOnEscape)
  }, [onClose])

  useEffect(() => {
    if (selected && playableItems.some((item) => item.id === selected.id)) return
    setSelectedId(playableItems[0]?.id ?? null)
  }, [playableItems, selected])

  const select = (id: number) => {
    setSelectedId(id)
    onSelect?.(id)
  }

  const showAdjacent = (direction: -1 | 1) => {
    const next = adjacentPlayableImage(playableItems, selectedId, direction)
    if (!next) return
    select(next.id)
    onShow(next.id)
  }

  const confirm = () => {
    if (invalidMinutes) return
    onStart(slideshowIntervalSeconds(minutes, false))
  }

  return <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget) onClose() }}>
    <section className="slideshow-dialog device-images-dialog" role="dialog" aria-modal="true" aria-labelledby="slideshow-dialog-title" aria-describedby="slideshow-dialog-description">
      <header>
        <div><h2 id="slideshow-dialog-title">设备图片</h2><p id="slideshow-dialog-description">选择图片立即显示，或设置设备自动轮播。</p></div>
        <button className="icon-button" title="关闭" aria-label="关闭" onClick={onClose}><X /></button>
      </header>

      <section className="device-images-dialog-section" aria-labelledby="device-images-slots-title">
        <div className="device-images-dialog-heading"><div><h3 id="device-images-slots-title">图槽</h3><p>{totalSlots == null ? `${occupiedSlots} 个已占用图槽` : `当前规格约 ${totalSlots} 个图槽 · 已占用 ${occupiedSlots} 个`} · {playableItems.length} 张可播放</p></div>{selected && <span>已选：图 {selectedIndex + 1}</span>}</div>
        <div className="device-image-slot-picker" role="listbox" aria-label="选择设备图片">
          {playableItems.map((item, index) => <button type="button" role="option" aria-selected={selectedId === item.id} aria-label={`图 ${index + 1}：${item.name}`} title={`${item.name} · 设备槽 ${item.id + 1}`} className={selectedId === item.id ? 'selected' : ''} key={item.id} onClick={() => select(item.id)}>{index + 1}</button>)}
        </div>
        {selected ? <div className="device-image-selection"><div><Image /><span><strong>{selected.name}</strong><small>图 {selectedIndex + 1} · 设备槽 {selected.id + 1}</small></span></div><button className="button primary" type="button" disabled={busy} onClick={() => onShow(selected.id)}><Play />播放所选</button></div> : <div className="device-image-selection empty"><span>没有与当前屏幕兼容的轮播图片。</span></div>}
        <div className="slideshow-manual-controls" aria-label="手动切换图片">
          <button type="button" disabled={busy || playableItems.length < 2} onClick={() => showAdjacent(-1)}><ChevronLeft />上一张</button>
          <button type="button" disabled={busy || playableItems.length < 2} onClick={() => showAdjacent(1)}>下一张<ChevronRight /></button>
        </div>
        {slideshowActive && <div className="device-images-playing"><Play />自动轮播正在运行；手动切换会从所选图片继续计时。</div>}
      </section>

      <section className="device-images-dialog-section auto-slideshow-section" aria-labelledby="auto-slideshow-title">
        <div className="device-images-dialog-heading"><div><h3 id="auto-slideshow-title">自动轮播</h3><p>设备按图槽顺序定时切换</p></div></div>
        <div className="slideshow-interval-field">
          <label htmlFor="slideshow-minutes"><Timer />轮播间隔</label>
          <div><input id="slideshow-minutes" type="number" inputMode="numeric" min={minimum} max={MAX_SLIDESHOW_MINUTES} step="1" value={minutes} disabled={slideshowActive} aria-invalid={invalidMinutes} onChange={(event) => setMinutes(Number(event.target.value))} /><span>分钟</span></div>
          <small className={invalidMinutes ? 'input-error' : ''}>{invalidMinutes ? `请输入 ${minimum} 到 ${MAX_SLIDESHOW_MINUTES} 之间的整数分钟。` : '正常使用最短 30 分钟，推荐 60 分钟；短间隔验证请到测试页。'}</small>
        </div>

        <div className="slideshow-presets" aria-label="常用轮播间隔">
          {intervalPresets.map((value) => <button type="button" disabled={slideshowActive} className={!invalidMinutes && normalizedMinutes === value ? 'active' : ''} key={value} onClick={() => setMinutes(value)}>{value < 60 ? `${value} 分` : `${value / 60} 小时`}</button>)}
        </div>
      </section>

      <footer>
        <button className="button secondary" onClick={onClose}>关闭</button>
        {slideshowActive ? <button className="button primary" onClick={onStop}><Pause />停止轮播</button> : <button className="button primary" disabled={invalidMinutes || busy || playableItems.length === 0} onClick={confirm}><Play />开始轮播</button>}
      </footer>
    </section>
  </div>
}
