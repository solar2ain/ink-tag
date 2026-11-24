export interface U8g2DisplayInfo {
  pixelWidth: number
  pixelHeight: number
  tileWidth: number
  tileHeight: number
  layout: 'vertical' | 'horizontal'
}

export class U8g2Font {
  static fromBase64(value: string): U8g2Font
  static fromArray(value: Uint8Array | number[]): U8g2Font
  readonly info: {
    bits_per_0: number
    bits_per_1: number
    bits_per_char_width: number
    bits_per_char_height: number
    bits_per_char_x: number
    bits_per_char_y: number
    bits_per_delta_x: number
    start_pos_unicode: number
  }
  hasGlyph(encoding: number): boolean
  getGlyphProps(encoding: number): { width: number; height: number; ox: number; oy: number; dx: number } | null
}

export class U8g2 {
  constructor(options: { width: number; height: number; layout?: 'vertical' | 'horizontal'; pageRows?: number })
  displayInfo: U8g2DisplayInfo
  displayMemory: Uint8Array
  clearBuffer(): void
  setFont(font: U8g2Font): void
  setFontMode(transparent: boolean | number): void
  setFontPosTop(): void
  getUTF8Width(text: string): number
  getGlyphWidth(encoding: number): number
  drawUTF8(x: number, y: number, text: string): number
}
