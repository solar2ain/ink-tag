import { Bluetooth, Save, X } from 'lucide-react'
import { useEffect, useState } from 'react'
import { getDevicePreference, updateDevicePreference } from '../data/devicePreferences'
import { inktagClient } from '../protocol/client'
import {
  BLUETOOTH_NAME_PREFIX, BLUETOOTH_NAME_PREFIXES, bluetoothNameBytes, deviceLabelBytes, normalizeBluetoothName,
  validateBluetoothName, validateDeviceLabel,
} from '../protocol/deviceSettings'
import { useDeviceStore } from '../store/device'
import { buildInkTagDeviceUrl, isManagedInkTagDeviceUrl } from '../qr/qr'

export type DeviceIdentityField = 'localAlias' | 'bluetoothName'

interface DeviceIdentityDialogProps {
  initialField: DeviceIdentityField
  onClose: () => void
}

export function DeviceIdentityDialog({ initialField, onClose }: DeviceIdentityDialogProps) {
  const state = useDeviceStore()
  const [localAlias, setLocalAlias] = useState(state.localAlias)
  const [bluetoothName, setBluetoothName] = useState(state.deviceName)
  const [saving, setSaving] = useState(false)
  const [message, setMessage] = useState('')
  const [managedQr, setManagedQr] = useState(true)
  const [pendingLinkSync, setPendingLinkSync] = useState<{ name: string; syncQr: boolean } | null>(null)
  const nameLimit = state.settingsCapabilities.bluetoothNameMaxBytes
  const deviceLabelLimit = state.settingsCapabilities.deviceLabelMaxBytes
  const normalizedBluetoothName = normalizeBluetoothName(bluetoothName)
  const bluetoothPrefix = BLUETOOTH_NAME_PREFIXES.find((prefix) => normalizedBluetoothName.startsWith(prefix)) || BLUETOOTH_NAME_PREFIX
  const bluetoothSuffix = normalizedBluetoothName.slice(bluetoothPrefix.length)
  const bluetoothChanged = normalizedBluetoothName !== state.deviceName
  const deviceLabelChanged = localAlias.trim() !== state.localAlias
  const nameError = bluetoothChanged ? validateBluetoothName(normalizedBluetoothName, nameLimit) : ''
  const deviceLabelError = deviceLabelChanged ? validateDeviceLabel(localAlias, deviceLabelLimit) : ''
  const isPro = state.activation.phase === 'active' && state.activation.edition === 'advanced'
  const canWriteBluetooth = state.connection === 'connected' && state.settingsCapabilities.bluetoothName && isPro
  const canWriteDeviceLabel = state.connection === 'connected' && state.settingsCapabilities.deviceLabel
  const updateBluetoothSuffix = (value: string) => {
    const suffix = value.replace(/^(?:IKT|InkTag)_/i, '')
    setBluetoothName(`${BLUETOOTH_NAME_PREFIX}${suffix}`)
  }

  useEffect(() => {
    if (!state.deviceId) return
    void getDevicePreference(state.deviceId).then((preference) => {
      setLocalAlias(state.localAlias || preference.localAlias)
      setBluetoothName(state.deviceName || preference.requestedBluetoothName)
      setManagedQr(isManagedInkTagDeviceUrl(preference.deviceInfoQrUrl))
    })
  }, [state.deviceId, state.deviceName, state.localAlias])

  useEffect(() => {
    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape' && !saving) onClose()
    }
    window.addEventListener('keydown', closeOnEscape)
    return () => window.removeEventListener('keydown', closeOnEscape)
  }, [onClose, saving])

  const save = async () => {
    if (!state.deviceId || nameError || deviceLabelError ||
        (bluetoothChanged && !canWriteBluetooth) ||
        (deviceLabelChanged && !canWriteDeviceLabel)) return
    setSaving(true)
    setMessage('')
    try {
      const nextAlias = localAlias.trim()
      const nextBluetoothName = normalizedBluetoothName
      if (deviceLabelChanged) await inktagClient.setDeviceLabel(nextAlias)
      if (bluetoothChanged) {
        await inktagClient.setBluetoothName(nextBluetoothName)
        try {
          await inktagClient.syncManagedDeviceLinks(nextBluetoothName, managedQr)
          setPendingLinkSync(null)
        } catch (error) {
          setPendingLinkSync({ name: nextBluetoothName, syncQr: managedQr })
          await updateDevicePreference(state.deviceId, {
            localAlias: nextAlias,
            requestedBluetoothName: nextBluetoothName,
          })
          const detail = error instanceof Error ? error.message : String(error)
          state.addLog(detail, 'error')
          setMessage(`${detail}。可在此重试，不需要再次修改蓝牙名。`)
          return
        }
      }
      await updateDevicePreference(state.deviceId, {
        localAlias: nextAlias,
        requestedBluetoothName: nextBluetoothName,
        ...(bluetoothChanged && managedQr ? { deviceInfoQrUrl: buildInkTagDeviceUrl(nextBluetoothName) } : {}),
      })
      state.setIdentity({ localAlias: nextAlias })
      const saved = [deviceLabelChanged ? '设备名' : '', bluetoothChanged ? '蓝牙广播名' : ''].filter(Boolean)
      state.addLog(`${saved.join('和') || '设备名称'}已写入设备`, 'success')
      onClose()
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    } finally {
      setSaving(false)
    }
  }

  const retryLinkSync = async () => {
    if (!state.deviceId || !pendingLinkSync) return
    setSaving(true)
    setMessage('')
    try {
      await inktagClient.syncManagedDeviceLinks(pendingLinkSync.name, pendingLinkSync.syncQr)
      if (pendingLinkSync.syncQr) {
        await updateDevicePreference(state.deviceId, {
          deviceInfoQrUrl: buildInkTagDeviceUrl(pendingLinkSync.name),
        })
      }
      state.addLog('设备二维码与 NFC 链接已同步', 'success')
      setPendingLinkSync(null)
      onClose()
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    } finally {
      setSaving(false)
    }
  }

  const unavailable = (bluetoothChanged && !canWriteBluetooth) ||
    (deviceLabelChanged && !canWriteDeviceLabel)
  return <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && !saving) onClose() }}>
    <section className="identity-dialog" role="dialog" aria-modal="true" aria-labelledby="identity-dialog-title">
      <header><div><h2 id="identity-dialog-title">修改设备名称</h2><p>设备名保存在设备中，蓝牙名用于广播发现。</p></div><button className="icon-button" title="关闭" aria-label="关闭" disabled={saving} onClick={onClose}><X /></button></header>
      <div className="identity-dialog-fields">
        <label>设备名<input autoFocus={initialField === 'localAlias'} type="text" maxLength={24} autoCapitalize="none" spellCheck={false} value={localAlias} placeholder="例如：InkTag 3A57" aria-invalid={Boolean(deviceLabelError)} onChange={(event) => setLocalAlias(event.target.value)} /><small className={deviceLabelError ? 'input-error' : ''}>{deviceLabelError || `仅英文字符 · 已使用 ${deviceLabelBytes(localAlias)} / ${deviceLabelLimit} 字节 · 清空后使用默认设备名`}</small></label>
        <label>蓝牙广播名 <span className="tier-chip">Pro</span><div className="prefixed-name-input"><span>{bluetoothPrefix}</span><input disabled={!isPro} autoFocus={initialField === 'bluetoothName'} type="text" inputMode="text" autoCapitalize="none" spellCheck={false} maxLength={Math.max(1, nameLimit - bluetoothPrefix.length)} value={bluetoothSuffix} placeholder="OM_AA06" aria-invalid={Boolean(nameError)} onChange={(event) => updateBluetoothSuffix(event.target.value)} /></div><small className={nameError ? 'input-error' : ''}>{nameError || (isPro ? `仅英文、数字、_ 和 - · 已使用 ${bluetoothNameBytes(normalizedBluetoothName)} / ${nameLimit} 字节` : '免费版使用默认蓝牙名称。')}</small></label>
      </div>
      {unavailable && <div className="identity-dialog-message warning"><Bluetooth />{state.connection !== 'connected' ? '连接设备后才能写入名称。' : bluetoothChanged && !isPro ? '修改蓝牙广播名需要 InkTag Pro。' : '当前固件未声明对应的名称写入能力。'}</div>}
      {message && <div className="identity-dialog-message error">{message}</div>}
      <footer><button className="button secondary" disabled={saving} onClick={onClose}>{pendingLinkSync ? '稍后处理' : '取消'}</button>{pendingLinkSync ? <button className="button primary" disabled={saving} onClick={() => void retryLinkSync()}><Save />{saving ? '正在同步' : '重试链接同步'}</button> : <button className="button primary" disabled={saving || !state.deviceId || Boolean(nameError) || Boolean(deviceLabelError) || unavailable} onClick={() => void save()}><Save />{saving ? '正在保存' : '保存修改'}</button>}</footer>
    </section>
  </div>
}
