import { describe, expect, it } from 'vitest'
import { fontOptions } from './fonts'

describe('editor font catalog', () => {
  it('offers only the selected calligraphy faces as browser fonts', () => {
    expect(fontOptions.map((item) => item.id)).toEqual(['ma-shan-zheng', 'kai'])
  })
})
