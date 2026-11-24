export const IMAGE_NAME_MAX_BYTES = 35

const reservedNames = new Set([
  'CALENDAR', 'CLOCK', 'DEVICE', 'SLIDESHOW', 'NEXT', 'PREVIOUS', 'CLEAR',
  '日历', '时间', '设备', '轮播', '下一页', '上一页', '清屏',
])

export function normalizeImageName(value: string) {
  return value.replace(/^ +| +$/g, '')
}

export function imageNameKey(value: string) {
  return Array.from(normalizeImageName(value), (character) =>
    /^[a-z]$/i.test(character) ? character.toUpperCase() : character).join('')
}

export function validateImageName(value: string, peers: string[] = []) {
  const normalized = normalizeImageName(value)
  if (!normalized) return ''
  if (/\p{Surrogate}/u.test(normalized)) return '图片名称必须是有效 UTF-8'
  if (/[\u0000-\u001f\u007f]/.test(normalized)) return '图片名称不能包含控制字符'
  const key = imageNameKey(normalized)
  if (reservedNames.has(key) || key.startsWith('IKNC') || /^SLOT-[1-9][0-9]*$/.test(key) || /^图槽-[1-9][0-9]*$/.test(normalized)) {
    return '图片名称不能与内置 NFC 命令重复'
  }
  if (new TextEncoder().encode(normalized).byteLength > IMAGE_NAME_MAX_BYTES) {
    return `图片名称不能超过 ${IMAGE_NAME_MAX_BYTES} 个 UTF-8 字节`
  }
  if (peers.some((peer) => imageNameKey(peer) === key)) return '该图片名称已被使用'
  return ''
}

export function encodeUploadImageName(value: string, imageMetadata: boolean) {
  if (!imageMetadata) {
    // Installed firmware retains its old descriptor boundary until adapted.
    return new TextEncoder().encode((value || 'image').replace(/[^a-zA-Z0-9_-]/g, '_').slice(0, 19))
  }
  const error = validateImageName(value)
  if (error) throw new RangeError(error)
  return new TextEncoder().encode(normalizeImageName(value))
}
