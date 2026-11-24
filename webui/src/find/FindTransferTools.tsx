import { useEffect, useRef, useState } from 'react'
import { X } from 'lucide-react'
import { useDeviceStore } from '../store/device'
import { inktagClient } from '../protocol/client'
import { findClient, type FindIdentity, type FindSessionState } from './client'
import { bytes, encryptPortableBackup, generateMigrationPassword, MAX_BACKUP_TEXT, newPortableIdentity,
  plainPortableBackup, readPortableBackup, selectPortableBackup, singleIdentityBackup, verifyOwnerProof,
  type PortableBackup, type PortableIdentity } from './portable'
import { enrollOfflineFind, savedFindBackups, saveFindBackup, markFindBackupHistorical, type SavedFindBackup } from './offlineOwner'
import { findDeviceDisplayName, groupFindIdentities } from './identities'

export interface FindTransferRequest { mode: 'export' | 'import' | 'saved'; deviceId?: string; replacementDeviceId?: string }
interface Props {
  serverAvailable: boolean
  identities: FindIdentity[]
  request: FindTransferRequest | null
  closeRequest: () => void
  onSession: (state: FindSessionState) => void
  offlineAction: 'enroll' | 'enable' | 'disable' | 'replace' | null
  closeOfflineAction: () => void
  onMessage: (message: string) => void
  onReplacementRestored?: (identity: FindIdentity) => void
}
export function FindTransferTools(props: Props) {
  const device = useDeviceStore()
  const mode = props.offlineAction || props.request?.mode
  const [password, setPassword] = useState(''), [repeat, setRepeat] = useState('')
  const [generatedPassword, setGeneratedPassword] = useState('')
  const [encrypted, setEncrypted] = useState(''), [confirmed, setConfirmed] = useState(false)
  const [includeHistory, setIncludeHistory] = useState(true), [includeKeys, setIncludeKeys] = useState(true)
  const [encrypt, setEncrypt] = useState(true), [plainAcknowledged, setPlainAcknowledged] = useState(false)
  const [prepared, setPrepared] = useState(false), [unlocked, setUnlocked] = useState(false)
  const [selectedDevices, setSelectedDevices] = useState<string[]>([])
  const [busy, setBusy] = useState(false), [error, setError] = useState(''), [hint, setHint] = useState('')
  const [saved, setSaved] = useState<SavedFindBackup[]>([])
  const identity = useRef<PortableIdentity | null>(null), source = useRef<PortableBackup | null>(null)
  const previousIdentity = useRef<PortableIdentity | null>(null)
  const busyRef = useRef(false)
  busyRef.current = busy
  const firstField = useRef<HTMLInputElement>(null), textField = useRef<HTMLTextAreaElement>(null)
  const generatedField = useRef<HTMLInputElement>(null)
  const reset = () => {
    setPassword(''); setRepeat(''); setGeneratedPassword(''); setEncrypted(''); setConfirmed(false)
    setIncludeHistory(true); setIncludeKeys(true); setEncrypt(true); setPlainAcknowledged(false)
    setPrepared(false); setUnlocked(false); setError(''); setHint(''); identity.current = null; previousIdentity.current = null; source.current = null
  }
  const close = () => {
    if (busyRef.current) return
    reset(); props.closeRequest(); props.closeOfflineAction()
  }
  const groups = groupFindIdentities(props.identities)
  const matchingGroups = props.request?.deviceId
    ? groups.filter(g => g.deviceId.toLowerCase() === props.request!.deviceId!.toLowerCase()) : groups
  useEffect(() => {
    if (!mode) return
    reset()
    setSelectedDevices(props.request?.deviceId ? matchingGroups.map(g => g.id) : [])
    try {
      const records = savedFindBackups(); setSaved(records)
      const matchingId = props.request?.deviceId || props.request?.replacementDeviceId
      const matching = records.filter(r => matchingId && r.deviceId?.toLowerCase() === matchingId.toLowerCase())
      if (matching.length === 1) setEncrypted(matching[0].encryptedBackup)
      else if (records.length === 1 && (mode === 'enable' || mode === 'disable' || mode === 'saved' || !props.serverAvailable))
        setEncrypted(records[0].encryptedBackup)
    } catch (e) { setError(e instanceof Error ? e.message : '无法读取迁移资料。') }
    firstField.current?.focus()
    const previousOverflow = document.body.style.overflow
    document.body.style.overflow = 'hidden'
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape' && !busyRef.current) close() }
    window.addEventListener('keydown', escape)
    return () => { window.removeEventListener('keydown', escape); document.body.style.overflow = previousOverflow }
    // Reset secrets only when opening a new dialog, never on a status refresh.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [mode, props.request])
  const act = async (action: () => Promise<void>) => {
    if (busyRef.current) return
    busyRef.current = true
    setBusy(true); setError(''); setHint('')
    try { await action() } catch (e) { setError(e instanceof Error ? e.message : '迁移操作失败。') }
    finally { busyRef.current = false; setBusy(false) }
  }
  const metadata = () => ({ deviceId: 'activation:' + device.activation.activationId,
    deviceName: device.localAlias || device.deviceName || 'InkTag', bluetoothName: device.deviceName || 'InkTag',
    findNote: device.localAlias || device.deviceName || 'InkTag' })
  const checkTarget = (id: string) => {
    const current = useDeviceStore.getState()
    if (current.connection !== 'connected' || ('activation:' + current.activation.activationId).toLowerCase() !== id.toLowerCase())
      throw new Error('设备连接已变化。迁移资料仍保留，请连接原设备后核对登记状态。')
  }
  const remember = (owner: PortableIdentity, text: string) => {
    // Plaintext must never enter persistent browser storage.
    if (text.startsWith('IKTFIND1.')) saveFindBackup({ fingerprint: owner.fingerprint, deviceId: owner.deviceId,
      deviceName: owner.deviceName || owner.bluetoothName, encryptedBackup: text })
  }
  const encode = (payload: PortableBackup) => encrypt ? encryptPortableBackup(payload, password) : Promise.resolve(plainPortableBackup(payload))
  const prepare = () => act(async () => {
    if (encrypt && password !== repeat) throw new Error('两次迁移口令不一致。')
    if (!encrypt && !plainAcknowledged) throw new Error('请确认未加密资料的隐私泄露风险。')
    if (mode === 'export') {
      if (!includeKeys && !includeHistory) throw new Error('请至少选择设备信息或历史轨迹。')
      if (props.serverAvailable) {
        const fingerprints = matchingGroups.filter(g => selectedDevices.includes(g.id)).flatMap(g => g.identities.map(e => e.keyFingerprint))
        if (!fingerprints.length) throw new Error('请至少选择一个设备。')
        const result = await findClient.exportBackup(password, includeHistory, fingerprints, includeKeys, encrypt, plainAcknowledged)
        setEncrypted(result.backupText || result.encryptedBackup || '')
      } else {
        if (!source.current || !props.request?.deviceId) throw new Error('请先解锁此设备的备份资料。')
        setEncrypted(await encode(selectPortableBackup(source.current, props.request.deviceId, includeKeys, includeHistory)))
      }
    } else {
      if (mode === 'replace' && !previousIdentity.current) throw new Error('请先提供并核对原身份的设备信息。')
      if (device.activation.phase !== 'active' || device.activation.edition !== 'advanced') throw new Error('请先激活 Pro，再登记 Find。')
      const owner = await newPortableIdentity(metadata())
      checkTarget(owner.deviceId)
      const text = await encode(singleIdentityBackup(owner))
      remember(owner, text)
      identity.current = owner; setEncrypted(text)
    }
    setPrepared(true); setPassword(''); setRepeat('')
  })
  const generate = () => act(async () => {
    const value = generateMigrationPassword()
    setPassword(value); setRepeat(value); setGeneratedPassword(value)
    setHint('已自动填写口令，请复制后与备份分开保管。')
  })
  const copyPassword = async () => {
    try { await navigator.clipboard.writeText(generatedPassword); setHint('已复制口令，请与备份分开保管。') }
    catch { generatedField.current?.focus(); generatedField.current?.select(); setHint('请手动复制已选中的口令。') }
  }
  const register = () => act(async () => {
    const owner = identity.current
    if (!owner || !confirmed) throw new Error('请先保存设备信息。')
    checkTarget(owner.deviceId)
    await enrollOfflineFind(owner, encrypted, inktagClient, () => checkTarget(owner.deviceId), localStorage, !encrypt && confirmed,
      mode === 'replace' ? previousIdentity.current || undefined : undefined)
    if (mode === 'replace' && previousIdentity.current) markFindBackupHistorical(previousIdentity.current.fingerprint)
    reset(); props.closeOfflineAction()
    props.onMessage(mode === 'replace' ? 'Find 身份已替换。请保管新的设备信息，并导入本地 WebUI 查询位置。' : '设备已登记并开启 Find。请保管设备信息，导入本地 WebUI 后查询位置。')
  })
  const enableSaved = () => act(async () => {
    const owner = identity.current
    if (!owner || !confirmed) throw new Error('请先保存设备信息。')
    checkTarget(owner.deviceId)
    const status = await inktagClient.readPublicOnlyFindOwnerStatus()
    checkTarget(owner.deviceId)
    if (!status.identityPresent) throw new Error('设备身份已变化，请重新打开 Find 登记。')
    await verifyOwnerProof(owner, status)
    checkTarget(owner.deviceId)
    await inktagClient.configureFind(true)
    checkTarget(owner.deviceId)
    reset(); props.closeOfflineAction()
    props.onMessage('Find 广播已开启。请将设备信息导入本地 WebUI 后查询位置。')
  })
  const readInput = () => act(async () => {
    const plain = encrypted.trimStart().startsWith('{')
    if (mode === 'import') {
      let fingerprint: string | undefined
      if (props.request?.replacementDeviceId) {
        const id = props.request.replacementDeviceId
        checkTarget(id)
        const payload = await readPortableBackup(encrypted, password, plain && plainAcknowledged)
        const status = await inktagClient.readPublicOnlyFindOwnerStatus()
        checkTarget(id)
        if (!status.timeValid) throw new Error('设备时间无效，请先校时后再核对原身份。')
        const owner = payload.identities.find((entry): entry is PortableIdentity => !entry.historyOnly &&
          entry.deviceId.toLowerCase() === id.toLowerCase() && entry.fingerprint === status.fingerprintHex.toLowerCase())
        if (!owner || !status.identityPresent) throw new Error('备份中没有当前设备正在使用的私钥，请选择这台设备原先保存的设备信息。')
        await verifyOwnerProof(owner, status)
        checkTarget(id); fingerprint = owner.fingerprint
      }
      const result = await findClient.importBackup(encrypted, password, plain && plainAcknowledged)
      if (props.request?.replacementDeviceId) checkTarget(props.request.replacementDeviceId)
      const restored = fingerprint ? result.identities.find(entry => entry.keyFingerprint === fingerprint) : undefined
      if (fingerprint && !restored) throw new Error('原身份资料未能恢复，请保留备份后重试。')
      props.onSession(result); reset(); props.closeRequest()
      if (restored) { props.onReplacementRestored?.(restored); return }
      props.onMessage('已导入资料。新导入设备的定期查询默认关闭；仅轨迹资料不能查询新位置。')
      return
    }
    const payload = await readPortableBackup(encrypted, password, plain && plainAcknowledged)
    const id = props.request?.deviceId || metadata().deviceId
    if (mode === 'export') {
      source.current = selectPortableBackup(payload, id, true, true)
      setUnlocked(true); setEncrypted(''); setPassword(''); setRepeat(''); setGeneratedPassword(''); setPlainAcknowledged(false)
      return
    }
    checkTarget(id)
    const status = await inktagClient.readPublicOnlyFindOwnerStatus()
    checkTarget(id)
    const owner = payload.identities.find((x): x is PortableIdentity => !x.historyOnly &&
      x.deviceId.toLowerCase() === id.toLowerCase() && x.fingerprint === status.fingerprintHex.toLowerCase())
    if (!owner || !status.identityPresent) throw new Error('资料中没有当前设备的查询密钥。请从已登记的本地 WebUI 导出设备信息后重试。')
    await verifyOwnerProof(owner, status)
    checkTarget(id)
    if (mode === 'replace') {
      previousIdentity.current = owner; setUnlocked(true); setEncrypted(''); setPassword(''); setRepeat(''); setGeneratedPassword(''); setPlainAcknowledged(false)
      setHint('原身份已核对。请生成并保存新的设备信息，再确认替换。')
      return
    }
    if (mode === 'enable') {
      const text = plain ? plainPortableBackup(singleIdentityBackup(owner)) : await encryptPortableBackup(singleIdentityBackup(owner), password)
      checkTarget(id)
      identity.current = owner; remember(owner, text); setEncrypted(text); setEncrypt(!plain); setPrepared(true)
      setPassword(''); setGeneratedPassword(''); setConfirmed(false)
      setHint('已核对设备身份。请复制或保存这份资料，再确认开启。')
      return
    }
    const key = bytes(owner.materialHex).slice(28)
    try { await inktagClient.configureFind(false, key); checkTarget(id) } finally { key.fill(0) }
    reset(); props.closeOfflineAction(); props.onMessage('Find 广播已关闭，身份资料和历史轨迹保留。')
  })
  const copy = async () => {
    try { await navigator.clipboard.writeText(encrypted); setHint('已复制资料，请妥善保存。') }
    catch { textField.current?.focus(); textField.current?.select(); setHint('请手动复制已选中的字符串，或保存文件。') }
  }
  const download = () => {
    const url = URL.createObjectURL(new Blob([encrypted], { type: 'application/octet-stream' }))
    const a = document.createElement('a'); a.href = url; a.download = encrypted.startsWith('IKTFIND1.') ? 'inktag-find-backup.iktfind' : 'inktag-find-backup.json'; a.click()
    setTimeout(() => URL.revokeObjectURL(url), 1000); setHint('已生成备份文件，请妥善保存。')
  }
  const readFile = async (file?: File) => {
    if (!file) return
    if (file.size > MAX_BACKUP_TEXT) { setError('迁移文件超过大小限制。'); return }
    try { setEncrypted(await file.text()); setPlainAcknowledged(false); setError('') } catch { setError('无法读取迁移文件。') }
  }
  const needsInput = mode === 'import' || mode === 'disable' || (mode === 'replace' && !unlocked) || (mode === 'enable' && !prepared) || (mode === 'export' && !props.serverAvailable && !unlocked)
  const isOutput = mode === 'saved' || prepared
  const createsOutput = mode === 'enroll' || (mode === 'replace' && unlocked) || (mode === 'export' && !needsInput)
  const plainInput = needsInput && encrypted.trimStart().startsWith('{')
  const plainOutput = prepared && !encrypted.startsWith('IKTFIND1.')
  const fileOnly = mode === 'export' && !props.request?.deviceId && includeHistory && selectedDevices.length !== 1
  const canRead = Boolean(encrypted && (plainInput ? plainAcknowledged : password))
  const title = mode === 'replace' ? '替换 Find 身份' : mode === 'enroll' || mode === 'enable' ? '保存设备信息并开启 Find' : mode === 'disable' ? '验证并关闭 Find'
    : mode === 'import' ? props.request?.replacementDeviceId ? '恢复原身份后替换' : '导入 Find 资料' : mode === 'saved' ? '本浏览器备份' : props.request?.deviceId ? '导出设备信息' : '导出本机 Find 资料'
  return <>{mode && <div className="modal-backdrop">
    <section className="find-transfer-dialog" role="dialog" aria-modal="true" aria-labelledby="find-transfer-title" onKeyDown={event => {
      if (event.key !== 'Tab') return
      const fields = Array.from(event.currentTarget.querySelectorAll<HTMLElement>('button:not([disabled]),input:not([disabled]),textarea,select')).filter(e => e.offsetParent !== null)
      const first = fields[0], last = fields.at(-1)
      if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus() }
      else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus() }
    }}>
      <header><h2 id="find-transfer-title">{title}</h2><button className="icon-button" aria-label="关闭" disabled={busy} onClick={close}><X /></button></header>
      <div className="find-transfer-body">
      {error && <div className="notice error" role="alert">{error}</div>}
      {hint && <p role="status">{hint}</p>}
      {props.request?.replacementDeviceId && <p>请粘贴此前保存的设备信息字符串，或选择备份文件；加密资料还需原口令。资料必须包含当前设备的查询私钥，仅轨迹备份无法替换身份。</p>}
      {mode === 'replace' && !unlocked && <p>请提供当前设备原先保存的设备信息（含查询私钥），粘贴字符串或选择备份文件。原口令和身份都必须匹配，才能替换。</p>}
      {(mode === 'enroll' || mode === 'enable') && <div className="notice warning"><span>请保存设备信息，并导入支持查询的本地 WebUI，否则后续无法获取该设备的位置。设备本身无法找回查询密钥。</span></div>}
      {(mode === 'saved' || needsInput) && <>
        {saved.length > 0 && (mode !== 'import' || props.request?.replacementDeviceId) && <label>已保存标签<select value={encrypted} onChange={e => { setEncrypted(e.target.value); setPlainAcknowledged(false) }}>
          <option value="">选择标签或粘贴资料</option>{saved.map(x => <option key={x.fingerprint} value={x.encryptedBackup}>{x.deviceName ? `${x.deviceName} · ` : ''}{x.fingerprint.slice(-8).toUpperCase()}</option>)}</select></label>}
        {mode === 'saved' && !saved.length && <p>本浏览器尚未保存加密资料。未加密资料需从另存的文件恢复。</p>}
        {needsInput && <><label>{mode === 'replace' || props.request?.replacementDeviceId ? '原设备信息（含查询私钥）' : '迁移字符串'}<textarea ref={textField} value={encrypted} onChange={e => { setEncrypted(e.target.value); setPlainAcknowledged(false) }} maxLength={MAX_BACKUP_TEXT} spellCheck={false} /></label>
          <label>或选择备份文件<input type="file" accept=".iktfind,.json,.txt" onChange={e => void readFile(e.target.files?.[0])} /></label></>}
      </>}
      {mode === 'export' && !needsInput && !prepared && <>
        {props.serverAvailable && <fieldset className="find-export-devices"><legend>选择设备</legend>
          {!props.request?.deviceId && <div className="find-export-selection-actions">
            <button className="button secondary" disabled={busy || matchingGroups.every(g => selectedDevices.includes(g.id))} onClick={() => setSelectedDevices(matchingGroups.map(g => g.id))}>全部勾选</button>
            <button className="button secondary" disabled={busy || !selectedDevices.length} onClick={() => setSelectedDevices([])}>全部清除</button>
          </div>}
          <div className="find-export-device-list">{matchingGroups.map(g => <label className="find-transfer-ack" key={g.id}>
            <input type="checkbox" checked={selectedDevices.includes(g.id)} disabled={busy} onChange={e => setSelectedDevices(old => e.target.checked ? [...old, g.id] : old.filter(id => id !== g.id))} />
            {findDeviceDisplayName(g.identities)} <small>{g.identities.length} 个身份</small></label>)}</div></fieldset>}
        <fieldset className="find-export-content"><legend>导出内容</legend>
          <label className="find-transfer-ack"><input type="checkbox" checked={includeKeys} onChange={e => setIncludeKeys(e.target.checked)} disabled={busy} />设备信息（含查询私钥）</label>
          <label className="find-transfer-ack"><input type="checkbox" checked={includeHistory} onChange={e => setIncludeHistory(e.target.checked)} disabled={busy} />历史轨迹</label>
          <p className="find-transfer-hint">仅轨迹资料不含查询私钥，导入后只能查看历史。</p>
        </fieldset>
      </>}
      {createsOutput && !prepared && <div className="find-transfer-encryption"><label className="find-transfer-ack"><input type="checkbox" checked={encrypt} onChange={e => { setEncrypt(e.target.checked); setPlainAcknowledged(false); setPassword(''); setRepeat(''); setGeneratedPassword('') }} disabled={busy} />使用口令加密（推荐）</label>{encrypt && <button className="button secondary" disabled={busy} onClick={() => void generate()}>自动生成口令</button>}</div>}
      {(plainInput || (createsOutput && !prepared && !encrypt)) && <div className="notice find-plaintext-warning">
        <div><strong>未加密资料可能泄露隐私</strong><p>设备信息包含查询私钥，持有者可能查询设备位置；轨迹会暴露历史位置。请勿公开或发送给他人。</p>
        <label className="find-transfer-ack"><input type="checkbox" checked={plainAcknowledged} onChange={e => setPlainAcknowledged(e.target.checked)} disabled={busy} />我了解并接受未加密资料的风险</label></div>
      </div>}
      {!isOutput && ((needsInput && !plainInput) || (createsOutput && encrypt)) && <>
        <div className="find-transfer-passphrase-fields"><label>迁移口令<input ref={firstField} type="password" autoComplete="new-password" value={password} onChange={e => { setPassword(e.target.value); setGeneratedPassword('') }} disabled={busy} /></label>
        {createsOutput && <label>再次输入口令<input type="password" autoComplete="new-password" value={repeat} onChange={e => { setRepeat(e.target.value); setGeneratedPassword('') }} disabled={busy} /></label>}
        </div>
        <p className="find-transfer-hint">至少6个字符，短口令较易被猜中。口令不会保存，请与备份分开保管。</p>
      </>}
      {generatedPassword && <label>生成的口令<div className="find-transfer-password-row"><input ref={generatedField} aria-label="生成的迁移口令" value={generatedPassword} readOnly autoComplete="off" spellCheck={false} /><button className="button secondary" onClick={() => void copyPassword()}>复制口令</button></div><span className="find-transfer-hint">关闭窗口后不再显示，请另行保存。</span></label>}
      {isOutput && encrypted && <>
        {plainOutput && <div className="notice danger">这份资料未加密，请勿公开；不会自动保存在浏览器中。</div>}
        {fileOnly ? <p role="status">备份文件已生成，请选择“保存文件”。轨迹较多时无需复制字符串。</p>
          : <label>{encrypted.startsWith('IKTFIND1.') ? '加密迁移字符串' : '未加密迁移字符串'}<textarea ref={textField} value={encrypted} readOnly spellCheck={false} /></label>}
        <div className="find-transfer-actions">
          {!fileOnly && <button className="button secondary" onClick={() => void copy()}>复制</button>}
          {(!props.request?.deviceId || mode !== 'export') && <button className="button secondary" onClick={download}>保存文件</button>}
        </div>
        <p className="find-transfer-hint">请导入支持查询的本地 WebUI。仅轨迹资料只能查看历史；加密资料需另外保管口令。</p>
        {mode === 'export' && !props.request?.deviceId && !fileOnly && <p className="find-transfer-hint">也可保存为文件，方便保管或导入。</p>}
      </>}
      {(mode === 'enroll' || mode === 'enable' || mode === 'replace') && prepared && <label className="find-transfer-ack"><input type="checkbox" checked={confirmed} onChange={e => setConfirmed(e.target.checked)} disabled={busy} />我已保存设备信息及所需口令，了解需导入本地 WebUI 后查询</label>}
      {mode === 'import' && <p className="find-transfer-hint">合并设备和轨迹，不覆盖已有资料；新导入设备的定期查询默认关闭。</p>}
      </div>
      <footer><button className="button secondary" disabled={busy} onClick={close}>关闭</button>
        {createsOutput && !prepared && <button className="button primary" disabled={busy || (!encrypt && !plainAcknowledged) || (mode === 'export' && props.serverAvailable && !matchingGroups.some(g => selectedDevices.includes(g.id)))} onClick={() => void prepare()}>{busy ? '正在生成…' : encrypt ? '生成加密资料' : '生成未加密资料'}</button>}
        {(mode === 'enroll' || mode === 'replace') && prepared && <button className="button primary" disabled={busy || !confirmed} onClick={() => void register()}>{busy ? '正在提交…' : mode === 'replace' ? '确认替换身份' : '登记并开启Find'}</button>}
        {mode === 'enable' && prepared && <button className="button primary" disabled={busy || !confirmed} onClick={() => void enableSaved()}>{busy ? '正在开启…' : '开启Find'}</button>}
        {needsInput && <button className="button primary" disabled={busy || !canRead} onClick={() => void readInput()}>{busy ? '正在验证…' : mode === 'import' ? '解密并导入' : mode === 'disable' ? '验证并关闭' : '读取并核对资料'}</button>}
      </footer>
    </section>
  </div>}</>
}
