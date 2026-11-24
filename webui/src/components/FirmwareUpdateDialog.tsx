import { FileUp, Package, RefreshCw, Sparkles, X } from 'lucide-react'
import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { checkFirmwareUpdate, v020FirmwareOptions } from '../protocol/firmwareUpdate'
import { isFirmwareVersion } from '../protocol/updates'
import { mcuDisplayName } from '../protocol/boards'
import { useDeviceStore } from '../store/device'
import { useFirmwareUpdateStore } from '../store/firmwareUpdate'
import type { FirmwareBuildEdition } from '../types'

export function FirmwareBuildBadge({ edition }: { edition: FirmwareBuildEdition }) {
  const Icon = edition === 'pro' ? Sparkles : Package
  return <span className={`edition-badge firmware-build-badge ${edition === 'pro' ? 'advanced' : ''}`}>
    <Icon aria-hidden="true" />{edition === 'pro' ? 'Pro' : 'BASIC'}
  </span>
}

function dismissalKey(deviceId: string, releaseTag: string) {
  return `inktag:firmware-update-dismissed:${deviceId}:${releaseTag}`
}

function wasDismissed(key: string) {
  try { return sessionStorage.getItem(key) === '1' } catch { return false }
}

function rememberDismissal(key: string) {
  try { sessionStorage.setItem(key, '1') } catch { /* file:// storage may be unavailable */ }
}

export function FirmwareUpdateDialog() {
  const navigate = useNavigate()
  const device = useDeviceStore()
  const updateState = useFirmwareUpdateStore()
  const [dismissed, setDismissed] = useState('')
  const edition = device.firmwareBuildEdition

  useEffect(() => {
    if (device.connection !== 'connected' || !device.deviceId || !device.boardId || !edition ||
        !isFirmwareVersion(device.firmwareVersion) || device.capabilities.ota === 'none') {
      updateState.clear()
      return
    }
    let cancelled = false
    updateState.setChecking()
    const options = v020FirmwareOptions(device.boardId, device.firmwareVersion, device.protocolVersion)
    void checkFirmwareUpdate(device.boardId, device.firmwareVersion, { ...options, edition }).then((result) => {
      if (!cancelled) updateState.setResult(result.status, result.latestVersion, result.update)
    }).catch((error) => {
      if (!cancelled) updateState.setUnavailable(error instanceof Error ? error.message : String(error))
    })
    return () => { cancelled = true }
  }, [device.connection, device.deviceId, device.boardId, device.firmwareVersion, device.protocolVersion, device.capabilities.ota, edition, updateState.requestId])

  const update = updateState.update
  if (device.connection !== 'connected' || updateState.status !== 'available' || !update ||
      update.edition !== edition || update.boardId !== device.boardId || update.currentVersion !== device.firmwareVersion) return null
  const key = dismissalKey(device.deviceId, update.releaseTag)
  if (dismissed === key || wasDismissed(key)) return null

  const close = () => {
    rememberDismissal(key)
    setDismissed(key)
  }
  const openFirmware = () => {
    close()
    navigate('/device?tab=ota')
  }
  return <div className="modal-backdrop firmware-update-backdrop" onMouseDown={(event) => {
    if (event.target === event.currentTarget) close()
  }}>
    <section className="firmware-update-dialog" role="dialog" aria-modal="true" aria-labelledby="firmware-update-title">
      <header><div><RefreshCw /><span><h2 id="firmware-update-title">发现设备固件更新</h2><p><FirmwareBuildBadge edition={update.edition} />{update.releaseTag}</p></span></div><button className="icon-button" title="稍后提醒" aria-label="稍后提醒" onClick={close}><X /></button></header>
      <div className="firmware-update-summary"><FileUp /><span><strong>{update.currentVersion} → {update.latestVersion}</strong>已找到适用于当前设备的 OTA 包。WebUI 不会自动开始升级。</span></div>
      <dl><div><dt>主控</dt><dd>{mcuDisplayName(update.boardId)}</dd></div><div><dt>升级包</dt><dd title={update.artifactName}>{update.artifactName}</dd></div></dl>
      <footer><button className="button secondary" onClick={close}>稍后处理</button><button className="button primary" onClick={openFirmware}><FileUp />选择在线固件</button></footer>
    </section>
  </div>
}
