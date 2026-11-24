export interface InkTagImageData {
  width: number
  height: number
  data: Uint8ClampedArray
}
export function adjustBrightness<T extends InkTagImageData>(image: T, amount?: number): T
export function adjustContrast<T extends InkTagImageData>(image: T, factor: number): T
export function ditherImage(
  image: InkTagImageData, algorithm: string, strength: number, colorMode: string,
  options?: { heavyInk?: boolean; gray4Darken?: number },
): InkTagImageData
