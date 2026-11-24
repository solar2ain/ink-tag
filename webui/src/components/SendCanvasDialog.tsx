import { RefreshCw, Send, X } from 'lucide-react'
import { useEffect, useId, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { canRunManualImageOperation } from '../image/manualOperation'
import { canSafelyReplaceImage, canSafelyStoreImage, shouldRefreshAfterImageSend, type ImageSendDestination } from '../image/sendMode'
import { inktagClient } from '../protocol/client'
import { normalizeImageName, validateImageName } from '../protocol/imageNames'
import { canvasSizeForScreen } from '../protocol/screen'
import { adaptCanvasToSize } from '../image/canvasFit'
import { automaticImageRotation } from '../image/orientation'
import { useDeviceStore } from '../store/device'
import type { ImageSlotContentType } from '../types'
import { ImageSlotTargetPicker } from './ImageSlotTargetPicker'
import { imageStorageWriteHint, unavailableImageStorageMessage } from '../image/storageAvailability'

type Props = {
  title?: string
  defaultName: string
  contentType?: ImageSlotContentType
  initialDestination?: 'display' | 'store'
  prepareCanvas: () => HTMLCanvasElement | null | Promise<HTMLCanvasElement | null>
  canSend?: () => boolean
  readingCanvasSize?: { width: number; height: number }
  message: (text: string) => void
  close: () => void
}

// One send flow for pictures, the editor and saved pages. The device client
// remains authoritative for storage quota, target validity, AUTH and transport.
export function SendCanvasDialog(props: Props) {
  const { title = '发送页面', defaultName, contentType = 'image', prepareCanvas, message, close } = props
  const state = useDeviceStore(), id = useId(), dialog = useRef<HTMLFormElement>(null)
  const liveProps = useRef(props); liveProps.current = props
  const [target] = useState(() => ({ deviceId: state.deviceId, screen: JSON.stringify(state.screen) }))
  const known = state.capabilities.imageStore && state.resourceReadState === 'idle' && state.imageStoreInfo !== null
  const canAppend = Boolean(known && canSafelyStoreImage(state.imageStoreInfo?.contiguousFit))
  const canReplace = Boolean(known && canSafelyReplaceImage(state.imageStoreItems.length))
  const [destination, setDestination] = useState<ImageSendDestination>(() => props.initialDestination === 'store' && (canAppend || canReplace) ? canAppend ? 'append' : 'replace' : 'display')
  const [slotId, setSlotId] = useState<number | undefined>()
  const [name, setName] = useState(defaultName), [refreshScreen, setRefreshScreen] = useState(true)
  const [busy, setBusy] = useState(false), [error, setError] = useState('')
  const pending = useRef(false)
  const stored = destination !== 'display'
  const nameError = stored && state.capabilities.imageMetadata ? validateImageName(name,
    state.imageStoreItems.filter(item => destination !== 'replace' || item.id !== slotId).map(item => item.name)) : ''
  const sameDevice = state.deviceId === target.deviceId && JSON.stringify(state.screen) === target.screen
  const size = state.screen && canvasSizeForScreen(state.screen)
  const screenRelativeBadge = size && props.readingCanvasSize && !state.capabilities.imageReadingOrientation &&
    automaticImageRotation(props.readingCanvasSize.width, props.readingCanvasSize.height, size.width, size.height) !== 0
  const ready = state.connection === 'connected' && sameDevice && Boolean(size) && canRunManualImageOperation(state.action)
  const storageReady = !stored || (destination === 'append' ? canAppend : canReplace && slotId !== undefined)
  const selectStored = () => {
    if (canAppend) { setDestination('append'); setSlotId(undefined) }
    else if (canReplace) { setDestination('replace'); setSlotId(undefined) }
  }
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null
    dialog.current?.querySelector<HTMLInputElement>('input:not(:disabled)')?.focus()
    return () => { if (previous?.isConnected) previous.focus() }
  }, [])
  const autoRead = useRef(false)
  useEffect(() => {
    if (autoRead.current || !ready || !state.capabilities.imageStore || state.resourceReadState !== 'idle' || state.imageStoreInfo) return
    autoRead.current = true
    void inktagClient.listStoredImages().catch(cause => setError(cause instanceof Error ? cause.message : String(cause)))
  }, [ready, state.capabilities.imageStore, state.resourceReadState, state.imageStoreInfo])
  const send = async () => {
    if (pending.current || !ready || !storageReady || nameError || liveProps.current.canSend?.() === false) return
    pending.current = true; setBusy(true); setError('')
    try {
      let canvas = await prepareCanvas()
      if (!canvas) throw new Error('画面尚未准备完成，请稍后重试。')
      const current = useDeviceStore.getState()
      if (current.connection !== 'connected' || current.deviceId !== target.deviceId || JSON.stringify(current.screen) !== target.screen)
        throw new Error('设备或屏幕配置已变化，请关闭窗口后重新发送。')
      if (!canRunManualImageOperation(current.action)) throw new Error('设备正在执行其他操作，请稍后重试。')
      if (liveProps.current.canSend?.() === false) throw new Error('当前页面暂不能发送，请关闭窗口后检查页面内容。')
      const size = current.screen && canvasSizeForScreen(current.screen)
      if (!size) throw new Error('尚未读取设备屏幕配置，请重新连接后再试。')
      const readingRotation = current.capabilities.imageReadingOrientation
        ? automaticImageRotation(canvas.width, canvas.height, size.width, size.height) / 90 : 0
      canvas = adaptCanvasToSize(canvas, size)
      // Recheck the live list after asynchronous export; never overwrite a
      // newly occupied empty slot, or use stale names after a list read.
      if (stored) {
        if (current.resourceReadState !== 'idle' || !current.imageStoreInfo || !current.capabilities.imageStore)
          throw new Error('图槽列表尚未读取完成，请稍后重试。')
        const occupied = current.imageStoreInfo.occupiedIds?.includes(slotId ?? -1)
        if ((destination === 'replace' && !current.imageStoreItems.some(item => item.id === slotId)) ||
          (destination === 'append' && slotId !== undefined && occupied !== false))
          throw new Error('目标图槽已变化，请关闭窗口后重新选择。')
        if (current.capabilities.imageMetadata) {
          const invalid = validateImageName(name, current.imageStoreItems.filter(item => destination !== 'replace' || item.id !== slotId).map(item => item.name))
          if (invalid) throw new Error(invalid)
        }
      }
      const uploadName = stored && current.capabilities.imageMetadata ? normalizeImageName(name) : defaultName
      const result = await inktagClient.sendCanvas(canvas, uploadName, stored, contentType, stored ? slotId : undefined,
        shouldRefreshAfterImageSend(destination, refreshScreen), ...(readingRotation ? [readingRotation] as const : []))
      let text = !stored ? '画面已发送，屏幕刷新完成。'
        : `已保存到设备图槽${result == null ? '' : ` ${result + 1}`}，${refreshScreen ? '屏幕刷新完成' : '屏幕保持不变'}。`
      if (stored && current.capabilities.imageMetadata) text += result == null ? '设备未返回图槽编号，名称未确认。' : uploadName ? `名称“${uploadName}”已保存。` : '图片未命名。'
      message(text); close()
    } catch (cause) {
      const text = cause instanceof Error ? cause.message : String(cause)
      setError(text); message(text)
    } finally { pending.current = false; setBusy(false) }
  }
  return createPortal(<div className="modal-backdrop" onMouseDown={event => { if (event.target === event.currentTarget && !pending.current) close() }}>
    <form ref={dialog} className="send-destination-dialog" role="dialog" aria-modal="true" aria-labelledby={`${id}-title`}
      onSubmit={event => { event.preventDefault(); void send() }} onKeyDown={event => {
        event.stopPropagation()
        if (event.key === 'Escape' && !pending.current) { event.preventDefault(); close() }
        if (event.key === 'Tab') {
          const fields = [...event.currentTarget.querySelectorAll<HTMLElement>('button:not(:disabled),input:not(:disabled),select:not(:disabled)')]
          const first = fields[0], last = fields.at(-1)
          if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus() }
          else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus() }
        }
      }}>
      <header><div><h2 id={`${id}-title`}>{title}</h2><p>选择直接显示，或保存到设备图槽。</p></div><button className="icon-button" type="button" aria-label="关闭" disabled={busy} onClick={close}><X /></button></header>
      <fieldset className="send-canvas-fields" disabled={busy}>
        <legend className="sr-only">发送方式</legend>
        <div className="send-destination-options">
          <label className={!stored ? 'active' : ''}><input type="radio" name={`${id}-destination`} checked={!stored} onChange={() => setDestination('display')} /><span><strong>仅显示一次</strong><small>刷新屏幕，不占用图槽，不能通过 NFC 再次调用。</small></span></label>
          <label className={stored ? 'active' : ''}><input type="radio" name={`${id}-destination`} checked={stored} disabled={!canAppend && !canReplace} onChange={selectStored} /><span><strong>保存到图槽</strong><small>{state.connection !== 'connected' ? '连接设备后读取图槽和可用空间。' : !state.capabilities.imageStore ? unavailableImageStorageMessage(state.capabilities, state.resourceReadError) : !known ? state.resourceReadError || '图槽列表未就绪，请刷新后选择。' : !canAppend && canReplace ? '图槽已满，可选择已有图片覆盖。' : !canAppend ? imageStorageWriteHint(state.imageStoreInfo!) : '选择空槽保存，或覆盖已有图片。'}</small></span></label>
        </div>
        {state.capabilities.imageStore && <div className="send-storage-status"><small>{state.resourceReadState === 'loading' ? '正在读取图槽…' : state.resourceReadState === 'error' ? '图槽读取失败，可重新读取。' : known ? '使用设备当前图槽列表' : '尚未读取图槽列表'}</small><button className="button secondary" type="button" disabled={!ready || state.resourceReadState === 'loading'} onClick={async () => {
          setError('')
          try { await inktagClient.listStoredImages() } catch (cause) { setError(cause instanceof Error ? cause.message : String(cause)) }
        }}><RefreshCw />刷新图槽</button></div>}
        {stored && <>
          <section className="image-slot-target-section" aria-labelledby={`${id}-slots`}>
            <div><strong id={`${id}-slots`}>保存位置</strong><span>{destination === 'replace' ? slotId === undefined ? '请选择要覆盖的已有图片。' : `将替换槽 ${slotId + 1}；写入中断可能丢失旧图片，请先下载备份。` : slotId !== undefined ? `将保存到槽 ${slotId + 1}（空槽）。` : '设备自动分配下一个可用空槽。'}</span></div>
            <ImageSlotTargetPicker items={state.imageStoreItems} storeInfo={state.imageStoreInfo}
              availableCount={canAppend ? state.imageStoreInfo?.contiguousFit ?? null : 0} canReplace={canReplace}
              selectedId={slotId ?? null} newSlotSelected={destination === 'append'}
              onSelectExisting={value => { setSlotId(value); setDestination('replace') }}
              onSelectNew={value => { setSlotId(value); setDestination('append') }} />
          </section>
          {state.capabilities.imageMetadata ? <label className="send-image-name"><span>图片名称</span><input aria-label="图片名称" value={name} placeholder="可选，最多 35 UTF-8 字节" aria-invalid={Boolean(nameError)} onChange={event => setName(event.target.value)} />{nameError && <small role="alert">{nameError}</small>}</label>
            : <p className="image-slot-target-note">当前固件不支持自定义中文名称；保存时使用兼容名称。</p>}
          <p className="image-slot-target-note send-nfc-note">支持 NFC 控制的 Pro 设备，可按{state.capabilities.imageMetadata ? '已保存的图片名称或' : ''}图槽编号选择画面。此操作不会修改 NFC 配置。</p>
          <label className="send-display-toggle"><input type="checkbox" checked={refreshScreen} onChange={event => setRefreshScreen(event.target.checked)} /><span><strong>保存后刷新屏幕</strong><small>关闭后只写入图槽，当前画面保持不变。</small></span></label>
        </>}
      </fieldset>
      {screenRelativeBadge && <p className="image-slot-target-note">当前固件的 InkTag 标识仍按设备屏幕方向显示，与画布中的位置可能不同。</p>}
      {!busy && !ready && <p className="send-canvas-error" role="alert">{!sameDevice ? '设备或屏幕配置已变化，请关闭窗口后重新发送。' : state.connection !== 'connected' ? '请先连接设备，然后重新打开发送窗口；当前画面无需保存。' : !size ? '尚未读取设备屏幕配置，请重新连接后再试。' : '设备正在执行其他操作。'}</p>}
      {error && <p className="send-canvas-error" role="alert">{error}</p>}
      <footer><button className="button secondary" type="button" disabled={busy} onClick={close}>取消</button><button className="button primary" type="submit" disabled={busy || !ready || !storageReady || Boolean(nameError)}><Send />{busy ? '发送中' : '开始发送'}</button></footer>
    </form>
  </div>, document.body)
}
