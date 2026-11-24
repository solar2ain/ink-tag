import type { ImageStoreInfo, ImageStoreItem } from '../types'

interface ImageSlotTargetPickerProps {
  items: ImageStoreItem[]
  availableCount: number | null
  storeInfo: ImageStoreInfo | null
  selectedId: number | null
  newSlotSelected: boolean
  canReplace?: boolean
  onSelectExisting: (id: number) => void
  onSelectNew: (id?: number) => void
}

export function ImageSlotTargetPicker({
  items, availableCount, storeInfo, selectedId, newSlotSelected, canReplace = true, onSelectExisting, onSelectNew,
}: ImageSlotTargetPickerProps) {
  const byId = new Map(items.map((item) => [item.id, item]))
  const occupied = new Set(storeInfo?.occupiedIds ?? items.map((item) => item.id))
  // Current-screen capacity is a storage estimate, never a Flash address or
  // proof that an older backend can CREATE at an arbitrary empty handle.
  const capacity = storeInfo?.requiredSectors ? Math.floor(storeInfo.totalSectors / storeInfo.requiredSectors) : 0
  const fixed = Boolean(storeInfo?.fixedSlotCount)
  const count = Math.min(255, storeInfo?.fixedSlotCount ?? Math.max(capacity, ...[...occupied].map((id) => id + 1), 0))
  const ids = Array.from({length: count}, (_, id) => id)
  const nextEmpty = ids.find((id) => !occupied.has(id))
  const canAdd = availableCount == null || availableCount > 0
  const known = storeInfo?.occupiedIds !== undefined

  return <>
    <div className="image-slot-target-legend"><span className="empty">空槽</span><span className="occupied">已有图片</span></div>
    <div className="image-slot-target-picker" role="listbox" aria-label="选择保存图槽">
    {ids.map((id) => {
      const item = byId.get(id), used = occupied.has(id)
      const selectable = used ? Boolean(item) && canReplace : known && canAdd && (fixed || id === nextEmpty)
      const selected = used ? !newSlotSelected && selectedId === id : newSlotSelected && id === (selectedId ?? nextEmpty) && selectable
      const status = used ? item ? '已使用' : '已使用，当前版本不可使用' : known ? '空槽' : '尚未读取'
      const hint = used ? item ? `${item.name || '未命名图片'} · ${item.width} × ${item.height}` : '当前版本不可使用'
        : !known ? '等待完整图槽列表' : !canAdd ? '当前容量或版本额度已满'
          : fixed ? '保存到这个空槽' : id === nextEmpty ? '设备自动分配的下一个新槽' : `当前固件自动分配新槽，请先使用槽 ${Number(nextEmpty) + 1}`
      return <button
      key={id}
      type="button"
      role="option"
      aria-selected={selected}
      aria-label={`槽 ${id + 1}，${status}${item?.name ? `，${item.name}` : ''}`}
      title={`槽 ${id + 1} · ${hint}`}
      disabled={!selectable}
      className={`image-slot-target ${used ? 'occupied' : known ? 'empty' : 'unknown'} ${selected ? 'selected' : ''}`}
      onClick={() => used ? onSelectExisting(id) : onSelectNew(fixed ? id : undefined)}
    >
      <strong>{id + 1}</strong>
      <small>{used ? item ? '已用' : '不可用' : known ? '空槽' : '--'}</small>
    </button>})}
    </div>
    <small className="image-slot-target-note">编号从 1 开始，空槽不重排。{fixed ? `当前屏幕共 ${count} 个槽，可指定空槽保存。` : <>{storeInfo ? `按当前屏幕预计 ${count} 个槽；` : '容量尚未读取；'}旧固件新增时由设备自动分配。</>}</small>
  </>
}
