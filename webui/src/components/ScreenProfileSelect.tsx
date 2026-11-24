import { Check, ChevronDown, Trash2 } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import {
  screenProfileFamily, screenProfileFamilyLabels, screenProfileNote, screenProfileTitle,
  type ScreenProfile, type ScreenProfileFamily,
} from '../data/screenProfiles'

interface ScreenProfileSelectProps {
  profiles: ScreenProfile[]
  selectedId: string
  showCustom: boolean
  onSelect: (profile: ScreenProfile) => void
  onDeleteUser?: (profile: ScreenProfile) => void
}

const presetFamilies: ScreenProfileFamily[] = ['uc8xxx', 'ssd1xxx', 'jd79xxx', 'ist7xxx', 'other']

export function ScreenProfileSelect({ profiles, selectedId, showCustom, onSelect, onDeleteUser }: ScreenProfileSelectProps) {
  const [open, setOpen] = useState(false)
  const root = useRef<HTMLDivElement>(null)
  const selected = profiles.find((item) => item.id === selectedId)

  useEffect(() => {
    const close = (event: PointerEvent) => {
      if (!root.current?.contains(event.target as Node)) setOpen(false)
    }
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape') setOpen(false) }
    document.addEventListener('pointerdown', close)
    document.addEventListener('keydown', escape)
    return () => { document.removeEventListener('pointerdown', close); document.removeEventListener('keydown', escape) }
  }, [])

  return <div className="profile-select" ref={root}>
    <button type="button" className="profile-select-trigger" aria-haspopup="listbox" aria-expanded={open} onClick={() => setOpen(!open)}>
      <span>{selected ? <ProfileCopy profile={selected} /> : showCustom ? <span className="profile-copy"><strong>自定义配置</strong><small>当前设备参数未匹配预设 Profile</small></span> : <span className="profile-placeholder">请选择屏幕 Profile</span>}</span>
      <ChevronDown />
    </button>
    {open && <div className="profile-select-menu" role="listbox">
      <ProfileGroup label="我的 Profile" profiles={profiles.filter((item) => item.group === 'user')} selectedId={selectedId} onSelect={(item) => { onSelect(item); setOpen(false) }} onDeleteUser={onDeleteUser} />
      {presetFamilies.map((family) => <ProfileGroup
        key={family}
        label={screenProfileFamilyLabels[family]}
        profiles={profiles.filter((item) => item.group === 'preset' && screenProfileFamily(item) === family)}
        selectedId={selectedId}
        onSelect={(item) => { onSelect(item); setOpen(false) }}
      />)}
    </div>}
  </div>
}

function ProfileGroup({
  label, profiles, selectedId, onSelect, onDeleteUser,
}: {
  label: string
  profiles: ScreenProfile[]
  selectedId: string
  onSelect: (profile: ScreenProfile) => void
  onDeleteUser?: (profile: ScreenProfile) => void
}) {
  if (!profiles.length) return null
  return <section>
    <h3>{label}<span>{profiles.length}</span></h3>
    {profiles.map((item) => <div className="profile-option-row" key={item.id}>
      <button type="button" role="option" aria-selected={item.id === selectedId} onClick={() => onSelect(item)}>
        <span className="profile-check">{item.id === selectedId && <Check />}</span>
        <ProfileCopy profile={item} />
      </button>
      {item.tag === 'user' && <button className="profile-delete" title="删除用户 Profile" onClick={() => onDeleteUser?.(item)}><Trash2 /></button>}
    </div>)}
  </section>
}

function ProfileCopy({ profile }: { profile: ScreenProfile }) {
  const title = screenProfileTitle(profile)
  const note = screenProfileNote(profile)
  const detail = [note, ...(profile.qualifiedBoard ? [
    profile.model, `仅 ${profile.qualifiedBoard} 原屏验证`, `旋转 ${(profile.config.transform & 3) * 90}°`,
  ] : [])].filter(Boolean).join(' / ')
  return <span className="profile-copy"><strong title={title}>{title}</strong><small title={detail}>{note}</small></span>
}
