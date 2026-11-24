import type { ImageStoreItem } from './types'

export function adjacentPlayableImage(
  items: ImageStoreItem[],
  selectedId: number | null,
  direction: -1 | 1,
) {
  const playable = items.filter((item) => item.compatible && item.contentType === 'image')
  if (!playable.length) return null

  const selectedIndex = playable.findIndex((item) => item.id === selectedId)
  if (selectedIndex < 0) return direction < 0 ? playable[playable.length - 1] : playable[0]
  return playable[(selectedIndex + direction + playable.length) % playable.length]
}
