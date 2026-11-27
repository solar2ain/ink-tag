import { afterEach, describe, expect, it, vi } from 'vitest'
import { checkFirmwareUpdate, downloadFirmwareFile, firmwareBrowserDownloadUrl, resolveFirmwareUpdate, v020FirmwareOptions } from './firmwareUpdate'
import { compareFirmwareVersions } from './updates'
import generatedFirmwareFeed from './fixtures/update-feed.json'
import { compareVersions, componentUpdate, loadUpdates, validateArtifact, validateFeed, verifyDownload, type UpdateFeed, type Component, type FetchLike } from './updates'

function artifact(type = 'ota') { return { type, name: 'InkTag-test-v0.2.1-ota.zip', bytes: 3, sha256: 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
  downloadUrl: 'https://github.com/solar2ain/ink-tag/releases/download/v0.2.1/InkTag-test-v0.2.1-ota.zip' } }
function component(): Component { return { version: '0.2.1', buildId: 'test-2', buildNumber: 2, releaseTag: 'v0.2.1', channel: 'stable', candidateOnly: false, releaseReady: true, published: true, artifacts: [artifact()] } }
const target = { family: 'gr5331_zkel', boardId: 4, protocolVersion: 16, layout: 'layout-v1', compatibilityEpoch: 1 }
function feed(): UpdateFeed { return { schemaVersion: 3, channel: 'stable', candidateOnly: false, releaseReady: true,
  components: { webui: component(), cli: null, localServer: null }, firmwareSelection: { published: true },
  firmware: [{ ...component(), target, releaseVersion: 201, edition: 'basic' }] } }
const options = { target, releaseVersion: 200 }
afterEach(() => { vi.unstubAllGlobals(); vi.useRealTimers() })
describe('public update discovery', () => {
  it.each(['v0.2.1-20261031.2', 'v0.2.1+20261031.1'])('accepts approved dated/historical stable tags %s', releaseTag => {
    const value = feed()
    for (const item of [value.components.webui!, ...value.firmware]) {
      item.releaseTag = releaseTag
      item.artifacts = item.artifacts.map(a => ({ ...a, downloadUrl: `https://github.com/solar2ain/ink-tag/releases/download/${releaseTag}/${a.name}` }))
    }
    expect(validateFeed(value, 'stable').firmware).toHaveLength(1)
  })
  it.each(['v0.2.1-pre.20261031.2', 'v0.2.1-rc.2', 'v0.2.1-20261031.0'])('rejects preview or invalid batch tag on stable %s', releaseTag => {
    const value = feed(); value.components.webui!.releaseTag = releaseTag
    expect(() => validateFeed(value, 'stable')).toThrow()
  })
  it('uses only exact known0.2 wire identities and rejects unknown baselines', () => {
    for(const family of ['gr5331_zkel','gr551x_zkel','gr5513_zkel_internal','om6626_zkcxxb','tlsr8359_hanshow','aesl22_nrf','nrf52832_priceetag','da14585_aesl']) {
      const selected=v020FirmwareOptions(family,'0.2.0.200',16)
      expect(selected.target).toMatchObject({family,boardId:family,protocolVersion:16,compatibilityEpoch:1})
      const value=feed();value.firmware[0]!.target=selected.target!
      value.firmware[0]!.releaseVersion=selected.releaseVersion!+1
      expect(resolveFirmwareUpdate(value,family,'0.2.0',selected).status).toBe('available')
      expect(resolveFirmwareUpdate(value,family,'0.2.1',selected).status).toBe('current')
    }
    for(const [family,version,protocol] of [['unknown','0.2.0',16],['gr5331_zkel','0.1.0',16],['gr5331_zkel','0.2.0',17]] as const)
      expect(v020FirmwareOptions(family,version,protocol)).toEqual({})
  })
  it.each([['0.2.1', '0.2.0', 1], ['0.2.0', '0.2.0', 0], ['0.1.9', '0.2.0', -1],
    ['1.0.0-preview.10', '1.0.0-preview.2', 1], ['1.0.0', '1.0.0-preview.2', 1], ['1.0.0+build', '1.0.0', 0]])('compares %s with %s', (a, b, result) => {
    expect(Math.sign(compareVersions(a, b))).toBe(result)
  })
  it.each(['Recovery', '1.2', '01.2.3', '1.2.3-preview.01', '1.2.3-..'])('rejects malformed version %s', value => expect(() => compareVersions(value, '1.0.0')).toThrow())
  it('uses component versions and monotonic build order, not device versions or random hashes', () => {
    const latest = component()
    expect(componentUpdate(latest, { version: '0.2.1', buildId: 'test-2', buildNumber: 2 })).toBe('current')
    expect(componentUpdate(latest, { version: '0.2.1', buildId: 'test-1', buildNumber: 1 })).toBe('available')
    expect(componentUpdate(latest, { version: '0.2.2', buildId: 'older-hash', buildNumber: 1 })).toBe('current')
    expect(componentUpdate(latest, { version: '0.2.1', buildId: 'unknown-hash', buildNumber: 3 })).toBe('current')
  })
  it('accepts and orders the four-component WebUI version', () => {
    const value = feed()
    const latest = value.components.webui!
    latest.version = '0.2.2.1'
    expect(validateFeed(value, 'stable').components.webui).toEqual(latest)
    expect(componentUpdate(latest, { version: '0.2.2', buildId: 'test-2', buildNumber: 2 })).toBe('available')
    expect(componentUpdate(latest, { version: '0.2.2.2', buildId: 'test-3', buildNumber: 3 })).toBe('current')
  })
  it('matches complete exact board/family/protocol/layout/epoch only', () => {
    expect(resolveFirmwareUpdate(feed(), target.family, '0.2.0', options).status).toBe('available')
    for (const changed of [{ boardId: 5 }, { family: 'gr551x_zkel' }, { protocolVersion: 1 }, { layout: 'other' }, { compatibilityEpoch: 2 }]) {
      expect(resolveFirmwareUpdate(feed(), target.family, '0.2.0', { ...options, target: { ...target, ...changed } }).status).toBe('not-published')
    }
    expect(() => resolveFirmwareUpdate(feed(), target.family, '0.2.0')).toThrow(/完整兼容信息/)
    expect(resolveFirmwareUpdate(feed(), target.family, '0.2.0', { ...options, target: { family: target.family } as typeof target }).status).toBe('not-published')
    expect(resolveFirmwareUpdate(feed(), target.family, '0.2.1', options).status).toBe('current')
    expect(resolveFirmwareUpdate(feed(), target.family, '0.2.2', options).status).toBe('current')
    expect(resolveFirmwareUpdate(feed(), target.family, '0.2.0', { ...options, releaseVersion: 202 }).status).toBe('current')
  })
  it('consumes the actual Python-generator contract and compares all four firmware fields', () => {
    expect(resolveFirmwareUpdate(generatedFirmwareFeed, target.family, '0.2.0.9', options).status).toBe('not-published')
    const taggedFeed = { ...generatedFirmwareFeed,
      firmware: generatedFirmwareFeed.firmware.map(entry => ({ ...entry, edition: 'basic' })) }
    expect(resolveFirmwareUpdate(taggedFeed, target.family, '0.2.0.9', options)).toMatchObject({status:'available',latestVersion:'0.2.0.10'})
    expect(resolveFirmwareUpdate(taggedFeed, target.family, '0.2.0.10', options).status).toBe('current')
    expect(resolveFirmwareUpdate(taggedFeed, target.family, '0.2.0.11', options).status).toBe('current')
    expect(compareFirmwareVersions('0.2.0.10','0.2.0.9')).toBe(1)
    expect(compareFirmwareVersions('0.2.0','0.2.0.0')).toBe(0)
    expect(() => compareVersions('0.2.0.10','0.2.0')).toThrow()
  })
  it('downloads from the exact release tree path and rejects a different board or traversal', async () => {
    const value = feed()
    const repositoryPath = 'release/firmware/gr5331_zkel/ota/app-ota.zip'
    value.firmware[0]!.buildTarget = 'gr5331_zkel'
    value.firmware[0]!.artifacts[0]!.repositoryPath = repositoryPath
    const update = resolveFirmwareUpdate(value, target.family, '0.2.0', options).update!
    const url = 'https://raw.githubusercontent.com/solar2ain/ink-tag/v0.2.1/' + repositoryPath
    const fetcher = vi.fn<FetchLike>(async () => new Response('abc'))
    await expect(downloadFirmwareFile(update, {}, fetcher)).resolves.toMatchObject({ name: update.artifactName, size: 3 })
    expect(fetcher.mock.calls[0]![0]).toBe(url)
    for (const path of ['release/firmware/gr551x_zkel/ota/app-ota.zip', repositoryPath.replace('app-ota.zip', '../app-ota.zip')]) {
      expect(() => firmwareBrowserDownloadUrl({ ...update, repositoryPath: path })).toThrow(/路径与板型/)
    }
    expect(firmwareBrowserDownloadUrl({ ...update, repositoryPath: undefined })).toContain('/firmware/gr5331_zkel/ota/' + update.artifactName)
  })
  it('handles the first release with no firmware', () => {
    const value = feed(); value.firmware = []; value.firmwareSelection.published = false
    expect(resolveFirmwareUpdate(value, target.family, '0.2.0', options).status).toBe('not-published')
  })
  it.each(['candidateOnly', 'releaseReady', 'channel', 'published'])('rejects unsafe component %s', key => {
    const value = feed() as unknown as { components: { webui: Record<string, unknown> } }
    value.components.webui[key] = key === 'candidateOnly' ? true : key === 'channel' ? 'preview' : false
    expect(() => validateFeed(value, 'stable')).toThrow()
  })
  it('rejects candidate snapshots, inconsistent firmware publication and wrong-channel responses', () => {
    expect(() => validateFeed({ ...feed(), candidateOnly: true }, 'stable')).toThrow()
    expect(() => validateFeed({ ...feed(), firmwareSelection: { published: false } }, 'stable')).toThrow()
    expect(() => validateFeed({ ...feed(), channel: 'preview' }, 'stable')).toThrow()
    expect(() => validateFeed({ schemaVersion: 2 }, 'stable')).toThrow()
  })
  it.each(['https://example.com/file.zip', 'https://github.com/solar2ain/ink-tag-evil/releases/download/v0.2.1/InkTag-test-v0.2.1-ota.zip',
    'https://github.com/solar2ain/ink-tag/releases/download/v0.2.1/../file.zip',
    'https://github.com/solar2ain/ink-tag/releases/download/v0.2.1/InkTag-test-v0.2.1-ota.zip?secret=1',
    'https://github.com/solar2ain/ink-tag/releases/download/v0.2.1/%2e%2e%2ffile.zip'])('rejects untrusted download URL %s', downloadUrl => {
    expect(() => validateArtifact({ ...artifact(), downloadUrl }, 'v0.2.1')).toThrow()
  })
  it('checks bytes and SHA256 without replacing publisher-signature checks', async () => {
    await expect(verifyDownload(new TextEncoder().encode('abc').buffer, artifact())).resolves.toBeUndefined()
    await expect(verifyDownload(new TextEncoder().encode('abd').buffer, artifact())).rejects.toThrow(/SHA256/)
    await expect(verifyDownload(new TextEncoder().encode('abcd').buffer, artifact())).rejects.toThrow(/大小/)
  })
  it('requests only stable by default, with no device/account data or credentials', async () => {
    const fetcher = vi.fn<FetchLike>(async () => new Response(JSON.stringify(feed()), { headers: { 'Content-Type': 'application/json' } }))
    await loadUpdates(undefined, fetcher)
    expect(fetcher.mock.calls[0][0]).toBe('https://solar2ain.github.io/ink-tag/updates/stable.json')
    const init = fetcher.mock.calls[0][1] as RequestInit
    expect(init.credentials).toBe('omit'); expect(init.referrerPolicy).toBe('no-referrer'); expect(init.body).toBeUndefined()
    expect(init.headers).toEqual({ Accept: 'application/json' })
  })
  it('rejects retired preview selection before making any network request', async () => {
    const fetcher = vi.fn<FetchLike>()
    await expect(loadUpdates('preview' as 'stable', fetcher)).rejects.toThrow(/正式版/)
    expect(fetcher).not.toHaveBeenCalled()
  })
  it('bounds an unresponsive request even when fetch ignores abort', async () => {
    await expect(loadUpdates('stable', () => new Promise(() => {}), 5)).rejects.toThrow(/超时/)
  })
  it('requires an explicit matching BASIC or Pro edition for firmware', () => {
    const mixed = feed()
    mixed.firmware.push({ ...mixed.firmware[0]!, edition: 'pro', version: '0.2.2', releaseVersion: 202 })
    expect(resolveFirmwareUpdate(mixed, target.family, '0.2.0', options).update).toMatchObject({ edition: 'basic', latestVersion: '0.2.1' })
    expect(resolveFirmwareUpdate(mixed, target.family, '0.2.0', { ...options, edition: 'pro' }).update).toMatchObject({ edition: 'pro', latestVersion: '0.2.2' })
    expect(resolveFirmwareUpdate(feed(), target.family, '0.2.0', { ...options, edition: 'pro' }).status).toBe('not-published')
  })
  it('treats a missing Pro feed as unpublished without requesting BASIC', async () => {
    const fetcher = vi.fn<FetchLike>(async () => new Response('not found', { status: 404 }))
    await expect(checkFirmwareUpdate(target.family, '0.2.0', { ...options, edition: 'pro' }, fetcher)).resolves.toMatchObject({ status: 'not-published', update: null })
    expect(fetcher).toHaveBeenCalledOnce()
    expect(fetcher.mock.calls[0][0]).toBe('https://solar2ain.github.io/ink-tag/updates/pro/stable.json')
  })
  it('reports malformed response and HTTP errors', async () => {
    await expect(loadUpdates('stable', async () => new Response('bad', { status: 503 }))).rejects.toThrow(/503/)
    await expect(loadUpdates('stable', async () => new Response('bad', { headers: { 'Content-Type': 'text/html' } }))).rejects.toThrow(/格式/)
    await expect(loadUpdates('stable', async () => new Response('bad', { headers: { 'Content-Type': 'application/json' } }))).rejects.toThrow()
  })
  it('caches each feed independently', async () => {
    const fetcher = vi.fn<FetchLike>(async () => new Response(JSON.stringify(feed()), { headers: { 'Content-Type': 'application/json' } }))
    vi.stubGlobal('fetch', fetcher)
    await loadUpdates(); await loadUpdates()
    await loadUpdates('stable', fetcher, undefined, 'pro'); await loadUpdates('stable', fetcher, undefined, 'pro')
    expect(fetcher).toHaveBeenCalledTimes(2)
  })
})
