export const MIN_IMAGE_SCALE = .25
export const MAX_IMAGE_SCALE = 6

export interface Point { x: number; y: number }
export type ImageFitMode = 'crop' | 'stretch'
export type ImagePreviewMode = 'adjusted' | 'dithered'
export type ImagePreviewLayer = 'original' | 'adjusted' | 'dithered'
export type ImageSendDestination = 'display' | 'append' | 'replace'

export function clampImageScale(value: number): number {
  return Math.max(MIN_IMAGE_SCALE, Math.min(MAX_IMAGE_SCALE, value))
}

export function imageCoverScale(
  sourceWidth: number,
  sourceHeight: number,
  targetWidth: number,
  targetHeight: number,
  rotation: number,
): number {
  const quarterTurn = Math.abs(rotation / 90) % 2 === 1
  const rotatedWidth = quarterTurn ? sourceHeight : sourceWidth
  const rotatedHeight = quarterTurn ? sourceWidth : sourceHeight
  return Math.max(targetWidth / rotatedWidth, targetHeight / rotatedHeight)
}

export function imageDrawSize(
  sourceWidth: number,
  sourceHeight: number,
  targetWidth: number,
  targetHeight: number,
  rotation: number,
  fitMode: ImageFitMode,
  scale: number,
): { width: number; height: number } {
  if (fitMode === 'crop') {
    const cover = imageCoverScale(sourceWidth, sourceHeight, targetWidth, targetHeight, rotation) * scale
    return { width: sourceWidth * cover, height: sourceHeight * cover }
  }
  const quarterTurn = Math.abs(rotation / 90) % 2 === 1
  return {
    width: (quarterTurn ? targetHeight : targetWidth) * scale,
    height: (quarterTurn ? targetWidth : targetHeight) * scale,
  }
}

export function zoomPanAtPoint(pan: Point, previousScale: number, nextScale: number, point: Point): Point {
  if (previousScale <= 0 || previousScale === nextScale) return pan
  const ratio = nextScale / previousScale
  return { x: point.x - (point.x - pan.x) * ratio, y: point.y - (point.y - pan.y) * ratio }
}

export function automaticImageRotation(sourceWidth: number, sourceHeight: number, targetWidth: number, targetHeight: number): number {
  if (sourceWidth === sourceHeight || targetWidth === targetHeight) return 0
  return (sourceHeight > sourceWidth) === (targetHeight > targetWidth) ? 0 : 90
}

/** Compose upright photos using the screen's pixel count, in the photo's orientation.
 * Encoding still maps this logical canvas to the unchanged physical profile. */
export function imageCanvasSize(sourceWidth: number, sourceHeight: number, target: {width:number;height:number}) {
  return automaticImageRotation(sourceWidth,sourceHeight,target.width,target.height)
    ? {width:target.height,height:target.width} : {width:target.width,height:target.height}
}

export function resolveImagePreviewLayer(
  mode: ImagePreviewMode,
  compareHeld: boolean,
  cropPreview: boolean,
): ImagePreviewLayer {
  if (cropPreview || compareHeld) return 'original'
  return mode
}

export function imageCompareLabel(): string { return '按住看原图' }
export function canSafelyStoreImage(contiguousFit: number | undefined): boolean {
  return contiguousFit == null || contiguousFit > 0
}
export function shouldRefreshAfterImageSend(destination: ImageSendDestination, displayStoredImage: boolean): boolean {
  return destination === 'display' || displayStoredImage
}
