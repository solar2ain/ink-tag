import { Bluetooth, X } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { inktagClient } from '../protocol/client'
import { bluetoothNameFromDeviceUrl } from '../qr/qr'
import { useDeviceStore } from '../store/device'
import { useTranslation } from '../preferences'

/** A URL name is a discovery hint, never device/password authorization. */
export function DeviceLinkConnection() {
  const name = bluetoothNameFromDeviceUrl(window.location.search)
  const t = useTranslation()
  const [open, setOpen] = useState(false)
  const pending = useRef<{ name: string; result: Promise<boolean> } | null>(null)
  const choose = useRef<HTMLButtonElement>(null)
  const dialog = useRef<HTMLElement>(null)
  const available = Boolean(navigator.bluetooth)
  useEffect(() => {
    if (!name) return
    let active = true
    // Reuse the same attempt across StrictMode's effect replay. Never restart
    // it on disconnect or cancellation, or interrupt a manual connection.
    if (!pending.current || pending.current.name !== name) {
      pending.current = { name, result: (async () => {
        const busy = () => ['selecting', 'connecting', 'connected'].includes(useDeviceStore.getState().connection)
        if (busy()) return false
        if (!navigator.bluetooth) return true
        try {
          const devices = await inktagClient.getKnownDevices()
          if (busy()) return false
          const matches = devices.filter(device => device.name === name)
          if (matches.length !== 1) return true
          await inktagClient.connectKnownDevice(matches[0])
          // connectKnownDevice reports ordinary failure through the store.
          // An explicit cancel returns to idle and must not reopen a dialog.
          return useDeviceStore.getState().connection === 'error'
        } catch {
          return !busy()
        }
      })() }
    }
    void pending.current.result.then(needsChooser => { if (active) setOpen(needsChooser) })
    return () => { active = false }
  }, [name])
  useEffect(() => {
    if (!open) return
    const previous = document.activeElement as HTMLElement | null
    ;(choose.current || dialog.current)?.focus()
    return () => previous?.focus()
  }, [open])
  if (!open) return null
  return <div className="modal-backdrop">
    <section className="device-link-dialog" role="dialog" aria-modal="true" aria-labelledby="device-link-title" tabIndex={-1} ref={dialog}
      onKeyDown={event => {
        if (event.key === 'Escape') { event.preventDefault(); setOpen(false) }
        if (event.key === 'Tab') {
          const buttons = [...(dialog.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') || [])]
          const next = buttons[(buttons.indexOf(document.activeElement as HTMLButtonElement) + (event.shiftKey ? -1 : 1) + buttons.length) % buttons.length]
          event.preventDefault(); next?.focus()
        }
      }}>
      <header><h2 id="device-link-title">{t('连接设备')}</h2><button className="icon-button" aria-label={t('关闭')} onClick={() => setOpen(false)}><X /></button></header>
      <p>{name}</p>
      <p>{t(available ? '未能自动连接，请选择设备。首次授权需要点击下方按钮。' : '当前浏览器不支持 Web Bluetooth，请使用支持蓝牙的浏览器或 CLI。')}</p>
      <footer><button className="button secondary" onClick={() => setOpen(false)}>{t('暂不连接')}</button>
        <button className="button primary" ref={choose} disabled={!available} onClick={() => {
          setOpen(false)
          // Call synchronously within this gesture; do not await DB/permission
          // lookups before opening the browser's native chooser.
          void inktagClient.connect({ onlyInkTag: true, preferredName: name })
        }}><Bluetooth />{t('选择蓝牙设备')}</button></footer>
    </section>
  </div>
}
