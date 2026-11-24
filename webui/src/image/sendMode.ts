export type ImageSendDestination = 'display' | 'append' | 'replace'

export function canSafelyStoreImage(contiguousFit: number | undefined) {
  return contiguousFit == null || contiguousFit > 0
}

export function canSafelyReplaceImage(existingCount: number) {
  // Append capacity does not describe an existing slot. The selected device
  // backend checks replacement capacity when it accepts the upload.
  return existingCount > 0
}

export function shouldRefreshAfterImageSend(destination: ImageSendDestination, displayStoredImage: boolean) {
  return destination === 'display' || displayStoredImage
}
