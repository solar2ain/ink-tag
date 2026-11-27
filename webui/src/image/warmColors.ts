/** Map saturated cool colors to ink the red/yellow panels actually have. */
export function supportsWarmColors(color: number) {
  return color === 2 || color === 3
}

export function remapCoolColors(pixels: Uint8ClampedArray, color: number) {
  if (!supportsWarmColors(color)) return
  for (let offset = 0; offset < pixels.length; offset += 4) {
    const [r, g, b] = [pixels[offset], pixels[offset + 1], pixels[offset + 2]]
    const max = Math.max(r, g, b)
    const min = Math.min(r, g, b)
    const chroma = max - min
    if (!pixels[offset + 3] || r === max || chroma < 18) continue
    const hue = max === g ? 120 + 60 * (b - r) / chroma : 240 + 60 * (r - g) / chroma
    const weight = Math.max(0, Math.min(1, (hue - 78) / 30, (270 - hue) / 30, (chroma - 18) / 36))
    if (!weight) continue
    // On four-color screens, green/cyan move toward yellow and blue toward red.
    // Retain highlight/shadow variation by leaving the original RGB floor in place.
    const yellow = color === 3 ? Math.max(0, Math.min(1, (226 - hue) / 55)) : 0
    pixels[offset] = r + weight * (max - r)
    pixels[offset + 1] = g + weight * (min + chroma * yellow - g)
    pixels[offset + 2] = b + weight * (min - b)
  }
}
