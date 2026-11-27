import { useEffect, useRef, useState } from 'react'
import { getLanguage, useTranslation } from '../preferences'
import { useDeviceStore } from '../store/device'
import type { LogEntry } from '../types'
import { usePhoneNdefReader, usePhoneNfcUnavailable, writePhoneCommand } from './browserNdef'
import { webSerial, type SerialPort } from './pn532'
import { forgetReaderPort, rememberReaderPort, restoreReaderPort } from './readerPort'
import { useSerialTag, type SerialImageProgress, type SerialOperation } from './serialTag'

export interface NfcOperationState {
  title: string
  status: string
  busy: boolean
  placement?: 'phone' | 'reader'
  image?: SerialImageProgress
  error?: boolean
  phoneUnavailable?: boolean
  autoClose?: boolean
}

export function useNfcOperation() {
  const t = useTranslation()
  const controller = useRef<AbortController | undefined>(undefined)
  const releasePhoneReader = useRef<(() => void) | undefined>(undefined)
  const readTag = useRef<{ port: SerialPort; uid: Uint8Array } | undefined>(undefined)
  const [operation, setOperation] = useState<NfcOperationState | null>(null)
  const PhoneReader = usePhoneNdefReader()
  useEffect(() => () => {
    controller.current?.abort()
    releasePhoneReader.current?.()
  }, [])
  useEffect(() => {
    if (!operation?.autoClose) return
    const timer = window.setTimeout(() => setOperation(current => current === operation ? null : current), 2000)
    return () => window.clearTimeout(timer)
  }, [operation])

  const close = () => {
    controller.current?.abort()
    setOperation(null)
    if (operation?.phoneUnavailable) usePhoneNfcUnavailable.setState(true)
  }

  const run = async (title: string, action: SerialOperation) => {
    if (controller.current) return
    releasePhoneReader.current?.()
    const Reader = action.kind === 'command' && typeof PhoneReader?.prototype.write === 'function' ? PhoneReader : undefined
    const serial = webSerial()
    const log = (message: string, level?: LogEntry['level']) =>
      useDeviceStore.getState().addLog(`[NFC · ${Reader ? '手机' : 'Web Serial'}] ${title}：${message}`, level)
    if (!Reader && !serial) {
      const status = '请在支持 Web Serial 的桌面 Chrome 或 Edge 中连接串口读卡器。'
      log(status, 'error')
      setOperation({ title, status, busy: false, error: true })
      return
    }

    const pending = new AbortController()
    controller.current = pending
    const update = (change: Partial<NfcOperationState>) => {
      if (!pending.signal.aborted) setOperation(current => current && { ...current, ...change })
    }
    let stage = Reader ? '等待手机贴卡发送指令' : '正在查找读卡器'
    let loggedQuarter = -1
    log(stage)
    setOperation({ title, status: Reader ? '请将手机顶部贴近设备。' : '正在查找读卡器。', busy: true, placement: Reader ? 'phone' : undefined })
    const fail = (status: string, extra?: Partial<NfcOperationState>, detail = status) => {
      log(`${detail}（最后阶段：${stage}）`, 'error')
      update({ status, busy: false, error: true, ...extra })
    }
    const tapTimeout = new Error('等待贴卡超时。若已接触标签，请查看设备状态。')
    let choosingPort = false
    let reusedPort = false
    let readerReady = false
    let timeout: number | undefined
    let readerHeldAfterWrite = false
    try {
      if (Reader && action.kind === 'command') {
        timeout = window.setTimeout(() => pending.abort(tapTimeout), 60_000)
        const reader = new Reader()
        let holdingReaderMode = false
        if (reader.scan) {
          // An active foreground scan keeps Android in reader mode after write().
          // The listener keeps Chromium's reader active without using tag contents.
          reader.onreading = () => {}
          try {
            await reader.scan({ signal: pending.signal })
            holdingReaderMode = true
          } catch {
            pending.signal.throwIfAborted()
            log('无法保持手机读卡模式，指令将按普通方式发送。', 'warning')
          }
        }
        pending.signal.throwIfAborted()
        await writePhoneCommand(reader, action.text(), pending.signal)
        pending.signal.throwIfAborted()
        log('指令已发送（Web NFC 已确认 NDEF 写入）。', 'success')
        if (holdingReaderMode) {
          readerHeldAfterWrite = true
          const release = () => {
            window.clearTimeout(readerTimer)
            releasePhoneReader.current = undefined
            pending.abort()
          }
          // Web NFC exposes no tag removal event. Allow time to move the phone away
          // without tying reader mode to the lifetime of the success dialog.
          const readerTimer = window.setTimeout(release, 10_000)
          releasePhoneReader.current = release
        }
        update({ status: '指令已发送，请移开手机并查看设备。', busy: false, autoClose: true })
        return
      }
      let port = await restoreReaderPort(serial!)
      pending.signal.throwIfAborted()
      reusedPort = Boolean(port)
      if (!port) {
        stage = '等待选择读卡器串口'
        log(stage)
        update({ status: '请选择读卡器串口。' })
        choosingPort = true
        port = await serial!.requestPort()
        choosingPort = false
      }
      pending.signal.throwIfAborted()
      if (readTag.current?.port !== port) readTag.current = undefined
      const expected = action.kind === 'command' ? readTag.current?.uid : undefined
      const selectedPort = port
      stage = reusedPort ? '正在识别记住的 PN532 读卡器' : '正在识别选定的 PN532 读卡器'
      log(stage)
      update({ status: '正在识别 PN532 读卡器。' })
      const result = await useSerialTag(selectedPort, action, expected, pending.signal, (status, image, waitingForDevice) => {
        if (pending.signal.aborted) return
        stage = status
        if (image) {
          // Like BLE transfers, keep progress checkpoints in the shared log.
          const quarter = Math.floor(4 * image.confirmed / image.total)
          if (quarter > loggedQuarter) {
            loggedQuarter = quarter
            log(status, 'tx')
          }
        } else log(status)
        update({ status, placement: waitingForDevice ? 'reader' : undefined, ...(image ? { image } : {}) })
      }, () => {
        readerReady = true
        if (!pending.signal.aborted) rememberReaderPort(selectedPort)
      }, t)
      pending.signal.throwIfAborted()
      if (action.kind === 'read') readTag.current = { port, uid: result.uid }
      const uid = Array.from(result.uid, byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase()
      log(`PN532 ${result.firmware} · ${result.tagType} · UID ${uid} · ${result.outcome}`,
        action.kind === 'image' && result.screen ? 'warning' : 'success')
      update({
        status: action.kind === 'read' ? `${result.outcome} · ${result.tagType} · UID ${uid}` : result.outcome,
        busy: false, image: undefined,
        autoClose: action.kind === 'command' || (action.kind === 'image' && !result.screen),
      })
      return result
    } catch (failure) {
      const error = failure as Error
      if (choosingPort && error.name === 'NotFoundError') {
        log('已取消串口选择。')
        close()
        return
      }
      if (pending.signal.aborted) {
        if (pending.signal.reason === tapTimeout) {
          log(tapTimeout.message, 'error')
          setOperation(current => current && { ...current, status: tapTimeout.message, busy: false, error: true })
        } else log(`操作已取消（最后阶段：${stage}）`)
        return
      }
      if (reusedPort && !readerReady && error.name !== 'InvalidStateError') {
        forgetReaderPort()
        const status = '无法连接记住的读卡器。再次操作可重新选择串口。'
        const detail = error.message ? `${status} ${error.message}` : status
        fail(getLanguage() === 'en' ? status : detail, undefined, detail)
        return
      }
      if (Reader && error.name === 'NotSupportedError') {
        fail('当前手机无法使用 Web NFC。', { phoneUnavailable: true })
        return
      }
      const status = error.name === 'NotAllowedError' ? '浏览器未获得设备访问权限，请检查权限后重试。'
        : (Reader && error.name === 'NotReadableError')
          ? '无法使用 NFC。请开启手机 NFC 后重试。'
          : error.message || '操作失败，请让设备保持贴近后重试。'
      const localized = t(status)
      if (action.kind === 'command' && getLanguage() === 'en' && /\p{Script=Han}/u.test(localized))
        fail('发送指令失败。请检查设备是否贴近，详情见日志。', undefined, status)
      else fail(status)
    } finally {
      if (timeout !== undefined) window.clearTimeout(timeout)
      if (Reader && !readerHeldAfterWrite && !pending.signal.aborted) pending.abort()
      controller.current = undefined
    }
  }

  return { operation, run, close }
}
