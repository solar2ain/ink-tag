import { describe, expect, it } from 'vitest'
import { automaticImageRotation, imageCanvasSize } from './orientation'

describe('image orientation alignment', () => {
  it('turns the canvas to match the photo without changing the physical profile',()=>{
    const landscape={width:720,height:480},portrait={width:480,height:720}
    expect(imageCanvasSize(480,720,landscape)).toEqual(portrait)
    expect(imageCanvasSize(720,480,portrait)).toEqual(landscape)
    expect(imageCanvasSize(720,480,landscape)).toEqual(landscape)
    expect(imageCanvasSize(400,400,landscape)).toEqual(landscape)
    expect(imageCanvasSize(400,800,{width:240,height:240})).toEqual({width:240,height:240})
    expect(landscape).toEqual({width:720,height:480})
  })
  it('rotates landscape input into a portrait device canvas', () => {
    expect(automaticImageRotation(1920, 1080, 128, 250)).toBe(90)
    expect(automaticImageRotation(1080, 1920, 400, 300)).toBe(90)
  })

  it('preserves matching and square orientations', () => {
    expect(automaticImageRotation(1920, 1080, 400, 300)).toBe(0)
    expect(automaticImageRotation(1080, 1920, 128, 250)).toBe(0)
    expect(automaticImageRotation(500, 500, 128, 250)).toBe(0)
  })
})
