export {
  MAX_IMAGE_SCALE,
  MIN_IMAGE_SCALE,
  clampImageScale,
  imageCoverScale,
  imageDrawSize,
  zoomPanAtPoint,
  type ImageFitMode,
  type Point,
} from '../../../packages/inktag-client-core/src/image-layout'

import type { Point } from '../../../packages/inktag-client-core/src/image-layout'

export function clampImagePan(
  pan: Point,
  image: { width: number; height: number },
  board: { width: number; height: number },
  rotation: number,
): Point {
  // The image editor rotates by quarter turns around the image center.
  const quarterTurn = rotation % 180 !== 0
  const width = quarterTurn ? image.height : image.width
  const height = quarterTurn ? image.width : image.height
  const limitX = (board.width + width) / 2 - Math.min(12, width, board.width)
  const limitY = (board.height + height) / 2 - Math.min(12, height, board.height)
  return {
    x: Math.max(-limitX, Math.min(limitX, pan.x)),
    y: Math.max(-limitY, Math.min(limitY, pan.y)),
  }
}
