import { FeedbackNotice, useFeedbackMessage } from './FeedbackNotice'
import { ChevronLeft, ChevronRight, CloudDownload, CloudOff, Download, HardDrive, MonitorUp, Pause, Play, RefreshCw, Trash2 } from 'lucide-react'
import { useEffect, useMemo, useState } from 'react'
import { adjacentPlayableImage } from '../imageSlotNavigation'
import { canRunManualImageOperation } from '../image/manualOperation'
import { inktagClient } from '../protocol/client'
import { useDeviceStore } from '../store/device'
import { imageSlotContentTypeLabels } from '../types'
import { SlideshowDialog } from './SlideshowDialog'
import { useConfirmationDialog } from './ConfirmationDialog'
import { imageSlotAvailability } from '../access/imageQuota'
import { unavailableImageStorageMessage } from '../image/storageAvailability'

export function DeviceImageSlots() {
  const confirmation = useConfirmationDialog()
  const { connection, capabilities, deviceId, imageStoreInfo, imageStoreItems, resourceReadState, resourceReadError, action, activation, screen } = useDeviceStore()
  const connected = connection === 'connected'
  const [selectedId, setSelectedId] = useState<number | null>(null)
  const [previews, setPreviews] = useState<Record<number, string>>({})
  const [slideshowDialogOpen, setSlideshowDialogOpen] = useState(false)
  const [readingId, setReadingId] = useState<number | null>(null)
  const [readProgress, setReadProgress] = useState(0)
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  const selected = imageStoreItems.find((item) => item.id === selectedId) || null

  useEffect(() => {
    setPreviews({})
    setReadingId(null)
    setReadProgress(0)
  }, [deviceId])

  useEffect(() => {
    if (selectedId != null && imageStoreItems.some((item) => item.id === selectedId)) return
    setSelectedId(imageStoreItems[0]?.id ?? null)
  }, [imageStoreItems, selectedId])

  const playableItems = useMemo(() => imageStoreItems.filter((item) => item.compatible && item.contentType === 'image'), [imageStoreItems])
  const imageCount = playableItems.length
  const slotAvailability = imageStoreInfo ? imageSlotAvailability(imageStoreInfo, activation, screen) : null
  const manualOperationAvailable = canRunManualImageOperation(action)
  const run = async (operation: () => Promise<unknown>, success: string) => {
    try { await operation(); setMessage(success) }
    catch (error) { setMessage(error instanceof Error ? error.message : String(error)) }
  }
  const readPreview = async (): Promise<string | null> => {
    if (!selected) return null
    setReadingId(selected.id)
    setReadProgress(0)
    setMessage('正在从设备回读图片槽。')
    try {
      const dataUrl = await inktagClient.readStoredImage(selected.id, setReadProgress)
      setPreviews((current) => ({ ...current, [selected.id]: dataUrl }))
      setMessage('图片槽已从设备回读。')
      return dataUrl
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
      return null
    } finally {
      setReadingId(null)
      setReadProgress(0)
    }
  }
  const downloadPreview = async () => {
    if (!selected) return
    const dataUrl = previews[selected.id] || await readPreview()
    if (!dataUrl) return
    const safeName = selected.name.replace(/[\\/:*?"<>|]+/g, '_').trim() || `slot-${selected.id + 1}`
    const anchor = document.createElement('a')
    anchor.href = dataUrl
    anchor.download = `${safeName}-slot-${selected.id + 1}.png`
    anchor.click()
    setMessage('设备图片槽已导出为 PNG。')
  }
  const showAdjacent = async (direction: -1 | 1) => {
    const next = adjacentPlayableImage(imageStoreItems, selectedId, direction)
    if (!next) return
    setSelectedId(next.id)
    await run(() => inktagClient.showStoredImage(next.id), `已请求设备显示${direction < 0 ? '上一张' : '下一张'}图片。`)
  }
  const clearAll = async () => {
    if (!await confirmation.confirm({ title: '清空设备图片槽', message: <>将删除设备中的全部 {imageStoreItems.length} 个图片槽。<strong>此操作无法撤销。</strong></>, confirmLabel: '全部清空', danger: true })) return
    await run(() => inktagClient.clearStoredImages(false), '设备图片槽已清空。')
  }
  const deleteSelected = async () => {
    if (!selected || !await confirmation.confirm({ title: '删除设备图片', message: <>将删除设备图片“{selected.name}”。<strong>此操作无法撤销。</strong></>, confirmLabel: '删除图片', danger: true })) return
    const { id, name } = selected
    try {
      await inktagClient.deleteStoredImage(id)
      setSelectedId(null)
      setPreviews((current) => {
        const next = { ...current }
        delete next[id]
        return next
      })
      const remaining = useDeviceStore.getState().imageStoreItems.length
      setMessage(`已删除“${name}”（槽 ${id + 1}），剩余 ${remaining} 张。`)
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    }
  }
  return <section className="device-slot-section" aria-labelledby="device-slot-title">
    <div className="section-heading"><div><h2 id="device-slot-title">设备图片</h2><p>点击图片可预览或管理。</p></div></div>
    {!connected || !capabilities.imageStore ? <aside className="capability-banner"><CloudOff /><div><strong>设备图片槽不可用</strong><span>{connected ? unavailableImageStorageMessage(capabilities, resourceReadError) : '连接带图片槽的设备后显示容量和内容。'}</span></div></aside> : <>
      {resourceReadState !== 'idle' && <div className="library-message" role="status">{resourceReadState === 'loading' ? '正在读取设备图槽，请稍候。' : `图槽读取未完成：${resourceReadError}`}</div>}
      <div className="slot-summary"><div><strong>{imageStoreInfo?.saved || 0}</strong><span>已保存</span></div><div><strong>{imageStoreInfo?.compatible || 0}</strong><span>当前屏幕可用</span></div><div><strong>{slotAvailability?.free ?? '--'}</strong><span>还可新增</span></div><div className="slot-summary-storage"><strong>{imageStoreInfo ? `${imageStoreInfo.totalSectors * 4} KiB` : '--'}</strong><span>图槽总容量</span><small>空闲 {imageStoreInfo ? `${imageStoreInfo.freeSectors * 4} KiB` : '--'}</small></div></div>
      <div className="slot-toolbar">
        <button className="button secondary" disabled={resourceReadState === 'loading'} onClick={() => void run(() => inktagClient.listStoredImages(), '设备图片槽已刷新。')}><RefreshCw />{resourceReadState === 'loading' ? '读取中' : '刷新'}</button>
        <div className="slot-manual-navigation" aria-label="手动切换轮播图片">
          <button className="button secondary" disabled={playableItems.length < 2 || !manualOperationAvailable || readingId !== null} onClick={() => void showAdjacent(-1)}><ChevronLeft />上一张</button>
          <button className="button secondary" disabled={playableItems.length < 2 || !manualOperationAvailable || readingId !== null} onClick={() => void showAdjacent(1)}>下一张<ChevronRight /></button>
        </div>
        <button className="button secondary slideshow-toolbar-button" disabled={!imageStoreInfo?.playable} onClick={() => {
          if (action === 'slideshow') void run(() => inktagClient.stopSlideshow(), '图片轮播已停止。')
          else setSlideshowDialogOpen(true)
        }}>{action === 'slideshow' ? <Pause /> : <Play />}{action === 'slideshow' ? '停止轮播' : '开始轮播'}</button>
        {Boolean(imageStoreInfo?.incompatible) && <button className="button secondary" onClick={() => void run(() => inktagClient.clearStoredImages(true), '不兼容的历史图片已清理，容量已重新计算。')}>清理旧图</button>}
        <button className="button secondary danger-text" disabled={!imageStoreItems.length} onClick={() => void clearAll()}><Trash2 />清空</button>
      </div>
      {message && <FeedbackNotice className="library-message" noticeKey={messageRevision}>{message}</FeedbackNotice>}

      {imageStoreItems.length ? <div className="device-slot-browser">
        <div className="slot-list" role="listbox" aria-label="设备图片槽">
          {imageStoreItems.map((item) => <button type="button" role="option" aria-selected={selectedId === item.id} className={selectedId === item.id ? 'selected' : ''} key={item.id} onClick={() => setSelectedId(item.id)}>
            <span className={`slot-state ${item.compatible ? 'ready' : ''}`}><HardDrive /></span>
            <span><strong>{item.name || '未命名图片'}</strong><small>槽 {item.id + 1} · {imageSlotContentTypeLabels[item.contentType]} · {item.width} × {item.height} · {(item.bytes / 1024).toFixed(1)} KiB</small></span>
          </button>)}
        </div>
        {selected && <div className="slot-preview-pane">
          <div className="slot-preview-heading"><strong>{selected.name}</strong><span>{selected.compatible ? selected.contentType === 'image' ? '参与图片轮播' : '独立页面，不参与图片轮播' : '与当前屏幕配置不兼容'}</span></div>
          <div className="slot-preview-media">{previews[selected.id] ? <img src={previews[selected.id]} alt={`${selected.name} 的设备回读预览`} /> : <div><HardDrive /><strong>{readingId === selected.id ? `正在回读 ${Math.round(readProgress * 100)}%` : '尚未回读预览'}</strong><span>WebUI 不会自动读取整帧；需要时请单独点击回读。</span></div>}</div>
          <div className="slot-preview-actions"><button className="button secondary" disabled={!selected.compatible || !manualOperationAvailable || readingId !== null} onClick={() => void readPreview()}><CloudDownload />{readingId === selected.id ? '回读中' : '回读预览'}</button><button className="button secondary" disabled={!selected.compatible || readingId !== null || (!previews[selected.id] && !manualOperationAvailable)} onClick={() => void downloadPreview()}><Download />导出 PNG</button><button className="button primary" disabled={!selected.compatible || !manualOperationAvailable || readingId !== null} onClick={() => void run(() => inktagClient.showStoredImage(selected.id), '已请求设备显示所选图片。')}><MonitorUp />在设备上预览</button><button className="button secondary danger-text" title="删除图槽" disabled={readingId !== null} onClick={() => void deleteSelected()}><Trash2 />删除图片</button></div>
        </div>}
      </div> : resourceReadState === 'idle' && imageStoreInfo ? <div className="slot-empty"><HardDrive /><strong>设备图片槽为空</strong><span>在上方发送图片时，可以选择加入新图槽。</span></div> : null}
      {slideshowDialogOpen && <SlideshowDialog imageItems={imageStoreItems} storeInfo={imageStoreInfo} action={action} initialSelectedId={selectedId} onSelect={setSelectedId} onClose={() => setSlideshowDialogOpen(false)} onShow={(id) => void run(() => inktagClient.showStoredImage(id), '已请求设备显示所选图片。')} onStart={(intervalSeconds) => void run(() => inktagClient.startSlideshow(intervalSeconds), `已开始轮播 ${imageCount} 张图片，间隔 ${intervalSeconds / 60} 分钟。`)} onStop={() => void run(() => inktagClient.stopSlideshow(), '图片轮播已停止。')} />}
      {confirmation.dialog}
    </>}
  </section>
}
