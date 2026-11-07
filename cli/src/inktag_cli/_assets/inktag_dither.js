/* InkTag image quantizer, independently implemented in 2026.
 * Algorithm definitions and intentional output changes are recorded in
 * docs/research/2026-11-04-independent-image-pipeline.md.
 * Diffusion coefficients, noise and rank tables are defined or generated here.
 */

const PALETTES = Object.freeze({
  blackWhiteColor: [[0, 0, 0], [255, 255, 255]],
  threeColor: [[0, 0, 0], [255, 255, 255], [255, 0, 0]],
  fourColor: [[0, 0, 0], [255, 255, 255], [255, 0, 0], [255, 255, 0]],
  gray4Color: [[0, 0, 0], [130, 130, 130], [205, 205, 205], [255, 255, 255]],
  sixColor: [[0, 0, 0], [255, 255, 255], [255, 0, 0], [255, 255, 0], [0, 255, 0], [0, 0, 255]],
})

// [future column, future row, weight numerator], followed by a denominator.
// These are the published mathematical diffusion matrices, not source code.
const MATRICES = Object.freeze({
  floydSteinberg: [16, [[1, 0, 7], [-1, 1, 3], [0, 1, 5], [1, 1, 1]]],
  atkinson: [8, [[1, 0, 1], [2, 0, 1], [-1, 1, 1], [0, 1, 1], [1, 1, 1], [0, 2, 1]]],
  jarvis: [48, [[1, 0, 7], [2, 0, 5], [-2, 1, 3], [-1, 1, 5], [0, 1, 7], [1, 1, 5], [2, 1, 3], [-2, 2, 1], [-1, 2, 3], [0, 2, 5], [1, 2, 3], [2, 2, 1]]],
  stucki: [42, [[1, 0, 8], [2, 0, 4], [-2, 1, 2], [-1, 1, 4], [0, 1, 8], [1, 1, 4], [2, 1, 2], [-2, 2, 1], [-1, 2, 2], [0, 2, 4], [1, 2, 2], [2, 2, 1]]],
  burkes: [32, [[1, 0, 8], [2, 0, 4], [-2, 1, 2], [-1, 1, 4], [0, 1, 8], [1, 1, 4], [2, 1, 2]]],
  twoRowSierra: [16, [[1, 0, 4], [2, 0, 3], [-2, 1, 1], [-1, 1, 2], [0, 1, 3], [1, 1, 2], [2, 1, 1]]],
  sierraLite: [4, [[1, 0, 2], [-1, 1, 1], [0, 1, 1]]],
})

const GAMMA = Float64Array.from({ length: 256 }, (_, i) => {
  const value = i / 255
  return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4
})
const clip = value => Math.max(0, Math.min(255, value))
const luminance = rgb => (rgb[0] * 54 + rgb[1] * 183 + rgb[2] * 19) / 256
const toGamma = value => {
  const v = Math.max(0, Math.min(1, value))
  return 255 * (v <= 0.0031308 ? 12.92 * v : 1.055 * v ** (1 / 2.4) - 0.055)
}

function validate(image) {
  const pixels = image.width * image.height
  if (!Number.isSafeInteger(image.width) || !Number.isSafeInteger(image.height)
      || image.width < 1 || image.height < 1 || !Number.isSafeInteger(pixels)
      || image.data.length !== pixels * 4) throw new RangeError('Invalid RGBA frame dimensions')
}

function lab(rgb) {
  const [r, g, b] = rgb.map(channel => GAMMA[Math.round(clip(channel))])
  const curve = t => t > 216 / 24389 ? Math.cbrt(t) : (24389 / 27 * t + 16) / 116
  const x = curve((0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047)
  const y = curve(0.2126729 * r + 0.7151522 * g + 0.072175 * b)
  const z = curve((0.0193339 * r + 0.119192 * g + 0.9503041 * b) / 1.08883)
  return [116 * y - 16, 500 * (x - y), 200 * (y - z)]
}

function quantizer(palette, reference) {
  const entries = reference.map(lab)
  const indices = new Int8Array(2 * 64 * 64 * 64).fill(-1)
  return rgb => {
    const r = Math.round(clip(rgb[0])), g = Math.round(clip(rgb[1])), b = Math.round(clip(rgb[2]))
    const neutral = Math.max(r, g, b) - Math.min(r, g, b) < 12
    const key = (((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2)) + (neutral ? 262144 : 0)
    if (indices[key] >= 0) return indices[key]
    const sample = lab([r, g, b])
    // Achromatic text and gray edges must not acquire an accent-colour fringe.
    let best = 0, minimum = Infinity
    for (let i = 0; i < entries.length; i++) {
      if (neutral && Math.max(...palette[i]) - Math.min(...palette[i]) > 0) continue
      const delta = entries[i].map((value, axis) => value - sample[axis])
      const distance = delta[0] ** 2 + delta[1] ** 2 + delta[2] ** 2
      if (distance < minimum) { best = i; minimum = distance }
    }
    indices[key] = best
    return best
  }
}

function sourcePixel(image, pixel, heavyInk, grayDarken) {
  const offset = pixel * 4, alpha = image.data[offset + 3] / 255
  const rgb = [0, 1, 2].map(c => 255 + (image.data[offset + c] - 255) * alpha)
  const gain = heavyInk && Math.min(...rgb) < 250 ? 0.92 : 1
  return rgb.map(value => clip(value * gain - grayDarken * 16))
}

function writePixel(output, pixel, rgb) {
  const offset = pixel * 4
  output[offset] = rgb[0]; output[offset + 1] = rgb[1]; output[offset + 2] = rgb[2]
  output[offset + 3] = 255
}

// Procedurally rank high-pass-filtered deterministic samples. No noise asset.
let noiseRanks
function noiseTile() {
  if (noiseRanks) return noiseRanks
  const side = 32, count = side * side
  const samples = Float64Array.from({ length: count }, (_, index) => {
    let seed = Math.imul(index + 1, 0x45d9f3b)
    seed = Math.imul(seed ^ (seed >>> 16), 0x45d9f3b)
    return ((seed ^ (seed >>> 16)) >>> 0) / 4294967296
  })
  const at = (x, y) => samples[((y + side) % side) * side + (x + side) % side]
  const ranked = Array.from(samples, (value, index) => {
    const x = index % side, y = Math.floor(index / side)
    const low = (4 * value + 2 * (at(x - 1, y) + at(x + 1, y) + at(x, y - 1) + at(x, y + 1))
      + at(x - 1, y - 1) + at(x + 1, y - 1) + at(x - 1, y + 1) + at(x + 1, y + 1)) / 16
    return { index, score: value - low }
  }).sort((a, b) => a.score - b.score || a.index - b.index)
  noiseRanks = new Float64Array(count)
  ranked.forEach((item, rank) => { noiseRanks[item.index] = (rank + 0.5) / count - 0.5 })
  return noiseRanks
}

function bayerThreshold(x, y) {
  let rank = 0
  for (let bit = 0; bit < 3; bit++) {
    const a = (x >> bit) & 1, b = (y >> bit) & 1
    rank = rank * 4 + (2 * (a ^ b) + b)
  }
  return (rank + 0.5) / 64 - 0.5
}

// Clip a power-of-two Hilbert traversal to a rectangle without walking its
// potentially enormous unused square (e.g. a 1x65536 image).
function visitHilbert(width, height, visitor) {
  let side = 1, depth = 0
  while (side < Math.max(width, height)) { side *= 2; depth++ }
  function visit(x, y, ax, ay, bx, by, level) {
    const xs = [x, x + ax, x + bx, x + ax + bx]
    const ys = [y, y + ay, y + by, y + ay + by]
    if (Math.min(...xs) >= width || Math.max(...xs) <= 0
        || Math.min(...ys) >= height || Math.max(...ys) <= 0) return
    if (!level) {
      const px = Math.floor(x + (ax + bx) / 2), py = Math.floor(y + (ay + by) / 2)
      if (px >= 0 && py >= 0 && px < width && py < height) visitor(px, py)
      return
    }
    visit(x, y, bx / 2, by / 2, ax / 2, ay / 2, level - 1)
    visit(x + ax / 2, y + ay / 2, ax / 2, ay / 2, bx / 2, by / 2, level - 1)
    visit(x + ax / 2 + bx / 2, y + ay / 2 + by / 2, ax / 2, ay / 2, bx / 2, by / 2, level - 1)
    visit(x + ax / 2 + bx, y + ay / 2 + by, -bx / 2, -by / 2, -ax / 2, -ay / 2, level - 1)
  }
  visit(0, 0, side, 0, 0, side, depth)
}

function adjustContrast(image, factor) {
  validate(image)
  if (!Number.isFinite(factor) || factor < 0) throw new RangeError('Invalid contrast')
  for (let i = 0; i < image.data.length; i += 4)
    for (let c = 0; c < 3; c++) image.data[i + c] = clip(128 + (image.data[i + c] - 128) * factor)
  return image
}

function ditherImage(image, algorithm, strength, mode, options = {}) {
  validate(image)
  if (!Number.isFinite(strength)) throw new RangeError('Invalid dithering strength')
  const palette = PALETTES[mode]
  if (!palette) throw new RangeError('Unsupported ink palette')
  const known = new Set([...Object.keys(MATRICES), 'none', 'bayer', 'blueNoise', 'riemersma',
    'hybrid', 'adaptiveThreePoint', 'ostromoukhov', 'linearLightSierra'])
  if (!known.has(algorithm)) throw new RangeError('Unsupported dithering algorithm')
  const amount = Math.max(0, Math.min(5, strength))
  const output = new Uint8ClampedArray(image.data.length)
  // E6 green/blue ink is darker than the additive RGB primaries. Match and
  // diffuse against the existing screen-preview calibration, but emit the
  // same canonical indices consumed by the unchanged wire encoder.
  const reference = mode === 'sixColor'
    ? palette.map((rgb, i) => i === 4 ? [35, 140, 35] : i === 5 ? [30, 40, 140] : rgb)
    : palette
  const choose = quantizer(palette, reference)
  const sample = pixel => sourcePixel(image, pixel, options.heavyInk === true,
    mode === 'gray4Color' ? Math.max(0, Math.min(1, options.gray4Darken || 0)) : 0)
  const white = palette.find(entry => entry.every(channel => channel === 255))
  const emit = (pixel, rgb) => {
    const index = image.data[pixel * 4 + 3] === 0 ? palette.indexOf(white) : choose(rgb)
    const selected = palette[index]
    writePixel(output, pixel, selected)
    return reference[index]
  }
  const finish = () => ({ width: image.width, height: image.height, data: output })
  const ordered = algorithm === 'bayer' || algorithm === 'blueNoise'
  if (!amount || algorithm === 'none' || ordered) {
    const noise = algorithm === 'blueNoise' ? noiseTile() : null
    for (let pixel = 0; pixel < image.width * image.height; pixel++) {
      const x = pixel % image.width, y = Math.floor(pixel / image.width)
      const threshold = noise ? noise[(y & 31) * 32 + (x & 31)] : bayerThreshold(x, y)
      emit(pixel, sample(pixel).map(value => value + (ordered ? threshold * 160 * amount : 0)))
    }
    return finish()
  }
  if (algorithm === 'riemersma') {
    const errors = Array.from({ length: 16 }, () => [0, 0, 0])
    const weights = Array.from({ length: 16 }, (_, age) => 0.75 ** age)
    const sum = weights.reduce((a, b) => a + b, 0)
    let cursor = 0
    visitHilbert(image.width, image.height, (x, y) => {
      const pixel = y * image.width + x, source = sample(pixel)
      const corrected = source.map((value, channel) => value + amount * errors.reduce((acc, _, age) =>
        acc + errors[(cursor + 15 - age) & 15][channel] * weights[age], 0) / sum)
      const selected = emit(pixel, corrected)
      errors[cursor] = corrected.map((value, channel) => Math.max(-255, Math.min(255, value - selected[channel])))
      cursor = (cursor + 1) & 15
    })
    return finish()
  }
  const linear = algorithm === 'linearLightSierra'
  const hybrid = algorithm === 'hybrid'
  const adaptive = algorithm === 'adaptiveThreePoint' || algorithm === 'ostromoukhov'
  const [denominator, matrix] = MATRICES[linear ? 'sierraLite' : hybrid || adaptive ? 'floydSteinberg' : algorithm]
  const errors = Array.from({ length: 3 }, () => new Float64Array(image.width * 3))
  const noise = hybrid ? noiseTile() : null
  for (let y = 0; y < image.height; y++) {
    const direction = y & 1 ? -1 : 1, row = errors[y % 3]
    for (let step = 0; step < image.width; step++) {
      const x = direction > 0 ? step : image.width - 1 - step, pixel = y * image.width + x
      const source = sample(pixel)
      const original = linear ? source.map(value => GAMMA[Math.round(value)]) : source
      const edge = hybrid ? (Math.abs(luminance(source) - luminance(sample(y * image.width + Math.max(0, x - 1))))
        + Math.abs(luminance(source) - luminance(sample(Math.max(0, y - 1) * image.width + x)))) / 510 : 0
      const corrected = original.map((value, channel) => value + row[x * 3 + channel]
        + (hybrid ? noise[(y & 31) * 32 + (x & 31)] * amount * 12 * (1 - edge) : 0))
      const selected = emit(pixel, linear ? corrected.map(toGamma) : corrected)
      const target = linear ? selected.map(value => GAMMA[value]) : selected
      const gain = Math.min(amount, 1.75) * (hybrid ? 1 - 0.7 * Math.min(1, edge * 4) : 1)
      const error = corrected.map((value, channel) => Math.max(linear ? -1 : -255,
        Math.min(linear ? 1 : 255, value - target[channel])) * gain)
      const tone = Math.min(luminance(source), 255 - luminance(source)) / 127.5
      const taps = adaptive ? [[1, 0, 0.6 - 0.2 * tone], [-1, 1, 0.1 + 0.2 * tone], [0, 1, 0.3]] : matrix
      for (const [dx, dy, weight] of taps) {
        const nextX = x + dx * direction
        if (nextX < 0 || nextX >= image.width || y + dy >= image.height) continue
        const next = errors[(y + dy) % 3], fraction = weight / (adaptive ? 1 : denominator)
        for (let channel = 0; channel < 3; channel++) next[nextX * 3 + channel] += error[channel] * fraction
      }
    }
    row.fill(0)
  }
  return finish()
}

export { adjustContrast, ditherImage }
