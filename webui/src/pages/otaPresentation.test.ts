import { expect, it } from 'vitest'
import { otaPackagePresentation, otaPlatformLabel } from './otaPresentation'
import { otaPresentationFixtures } from './fixtures/otaPresentation'
import type { ParsedOtaPackage } from '../protocol/ota'

it.each(otaPresentationFixtures)('uses the parsed application size and public platform name for $id', ({ pkg, boardId, platform, appBytes }) => {
  const before = pkg.payload
  const shown = otaPackagePresentation(pkg, boardId)
  expect(shown.platform).toBe(platform)
  expect(shown.appBytes).toBe(appBytes)
  expect(shown.fileName).toBe(pkg.payload.fileName)
  expect(shown.technicalSummary).toBe(pkg.summary)
  expect(pkg.payload).toBe(before)
})
it('never substitutes TLSR transport bytes for missing declared application size', () => {
  expect(otaPackagePresentation({ type: 'telink', payload: { firmware: new Uint8Array(122128) }, summary: '' }, '').appBytes).toBeUndefined()
})
it.each([0, -1, NaN, Infinity, 12.5, '122000', undefined])('does not invent application size from invalid metadata %s', value => {
  const pkg: ParsedOtaPackage = { type: 'telink', payload: { declaredSize: value }, summary: '' }
  expect(otaPackagePresentation(pkg, '').applicationSize).toBe('未提供')
})
it('does not derive Nordic version or signature validity from the filename, CRC or an invalid init packet', () => {
  const pkg = otaPresentationFixtures[4].pkg
  const shown = otaPackagePresentation({ ...pkg, payload: { ...pkg.payload, firmwareCrc: 0x12345678 } }, 'aesl22_nrf')
  expect(shown.version).toBe('未提供')
  expect(shown).not.toHaveProperty('signatureVerified')
})
it('labels a provided numeric release as a version number without decoding a guessed version', () => {
  const shown = otaPackagePresentation({ type: 'onmicro', payload: { manifest: { releaseVersion: 65736 } }, summary: '' }, '')
  expect([shown.versionLabel, shown.version]).toEqual(['版本编号', '65736'])
})
it('keeps unrecognized Nordic boards generic and handles missing metadata without parsing', () => {
  expect(otaPlatformLabel('nordic', '')).toBe('Nordic')
  expect(otaPackagePresentation({ type: 'nordic', payload: undefined, summary: 'original' }, '').version).toBe('未提供')
})
it.each(['goodix', 'onmicro', 'telink', 'dialog'] as const)('shows four version components for parsed %s metadata without changing it', type => {
  for (const [original, expected] of [['0.2.0', '0.2.0.0'], ['0.2.0.7', '0.2.0.7'], ['0.2.0.34462', '0.2.0.34462']]) {
    const payload = type === 'dialog' ? { version: original } : { manifest: { release: original } }
    const before = JSON.stringify(payload)
    expect(otaPackagePresentation({ type, payload, summary: '' }, '').version).toBe(expected)
    expect(JSON.stringify(payload)).toBe(before)
  }
})
