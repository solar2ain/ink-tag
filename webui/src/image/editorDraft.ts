import type { ProcessingPresetName } from 'epdoptimize'
import type { ImageFitMode } from './crop'
import type { ImageAlgorithm } from './optimize'
import type { ImagePreviewMode } from './previewMode'
import { imageDefaults } from './defaults'

export interface ImageEditorDraft {
  source: HTMLImageElement | null
  sourceUrl: string
  fileName: string
  targetResolution: string
  simulatedColor: number
  algorithm: ImageAlgorithm
  preset: ProcessingPresetName
  contrast: number
  brightness: number
  strength: number
  heavyInk: boolean
  rotationOffset: number
  canvasTurned: boolean
  fitMode: ImageFitMode
  imageScale: number
  pan: { x: number; y: number }
  workspacePan: { x: number; y: number }
  previewZoom: number
  previewMode: ImagePreviewMode
}

// This session draft deliberately keeps the decoded image alive while the SPA
// moves between routes. Loading another image releases the previous object URL.
export const imageEditorDraft: ImageEditorDraft = {
  source: null,
  sourceUrl: '',
  fileName: '图片',
  targetResolution: '400x300',
  simulatedColor: 2,
  algorithm: 'auto',
  preset: 'balanced',
  contrast: imageDefaults.contrast,
  brightness: 0,
  strength: imageDefaults.strength,
  heavyInk: false,
  rotationOffset: 0,
  canvasTurned: false,
  fitMode: 'crop',
  imageScale: 1,
  pan: { x: 0, y: 0 },
  workspacePan: { x: 0, y: 0 },
  previewZoom: 1,
  previewMode: 'dithered',
}
