import { describe, expect, it } from 'vitest'
import * as imageNames from './imageNames'
import { encodeUploadImageName, imageNameKey, validateImageName } from './imageNames'

describe('image metadata', () => {
  it('uses the same UTF-8 names for CREATE, with only old firmware bounded to its old descriptor', () => {
    expect(new TextDecoder().decode(encodeUploadImageName(' 课程表 ', true))).toBe('课程表')
    expect(encodeUploadImageName('', true)).toHaveLength(0)
    expect(encodeUploadImageName('一'.repeat(11) + 'ab', true)).toHaveLength(35)
    expect(() => encodeUploadImageName('一'.repeat(12), true)).toThrow()
    expect(new TextDecoder().decode(encodeUploadImageName('课程表', false))).toBe('___')
    expect(encodeUploadImageName('a'.repeat(35), false)).toHaveLength(19)
  })
  it('does not expose a standalone name or type mutation encoder', () => {
    expect(imageNames).not.toHaveProperty('encodeImageMetadataRequest')
  })

  it('reserves fixed NFC commands and rejects duplicate names', () => {
    expect(validateImageName('calendar')).toContain('内置')
    expect(validateImageName('SLOT-1')).toContain('内置')
    for (const reserved of ['CLEAR', 'cLeAr', '清屏', 'IKNCfoo', 'iknc', 'iKnCFoo']) {
      expect(validateImageName(reserved)).toContain('内置')
    }
    expect(validateImageName('IKN')).toBe('')
    expect(validateImageName('课程表', ['课程表'])).toContain('已被使用')
    expect(imageNameKey(' desk ')).toBe('DESK')
  })

  it('enforces the 35-byte UTF-8 boundary, not a character limit', () => {
    expect(validateImageName('一'.repeat(11) + 'ab')).toBe('')
    expect(validateImageName('一'.repeat(12))).toContain('35')
    expect(validateImageName('\ud800')).toContain('UTF-8')
    expect(validateImageName('\nname')).toContain('控制')
  })

  it('allows unnamed uploads without treating the placeholder as a device name', () => {
    expect(validateImageName('', [''])).toBe('')
    expect(encodeUploadImageName('', true)).toHaveLength(0)
  })
})
