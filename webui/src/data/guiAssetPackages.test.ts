import {beforeEach,expect,it,vi} from 'vitest'
import {editableGuiFontPackage,editableGuiFonts,packageFonts,rememberGuiAssetPackage} from './guiAssetPackages'
import {defaultGuiFonts,type GuiAssetInfo,type GuiAssetPackage} from '../protocol/guiAssets'
import {buildFontRoles} from '../protocol/fontRoles'
const database=vi.hoisted(()=>({get:vi.fn(),put:vi.fn()}))
vi.mock('./db',()=>({db:{deviceGuiAssetPackages:database}}))
const info=(pkg?:GuiAssetPackage):GuiAssetInfo=>({version:1,schema:pkg?1:0,activeBank:pkg?0:null,storageMode:1,
  generation:pkg?.generation??0,activeSize:pkg?.totalSize??0,activeCrc32:pkg?.packageCrc32??0,
  maximumSize:32768,supportedSchemas:2,roleVersion:1,flags:3})
beforeEach(()=>{database.get.mockReset();database.put.mockReset()})
const record=(pkg:GuiAssetPackage)=>({generation:pkg.generation,crc32:pkg.packageCrc32,bytes:new Blob([pkg.bytes.slice().buffer])})
it('only a validated built-in font state implies catalog provenance',async()=>{
  expect(await editableGuiFontPackage('fixture',info())).toEqual({fonts:defaultGuiFonts(),chineseSource:'catalog'})
  await expect(editableGuiFontPackage('fixture',{...info(),activeSize:1})).rejects.toThrow()
  expect(database.get).not.toHaveBeenCalled()
})
it('binds installed bytes to live generation, size, schema and CRC, not just DB metadata',async()=>{
  const pkg=buildFontRoles(defaultGuiFonts(),{},4)
  database.get.mockResolvedValue({...record(pkg),chineseSource:'catalog'})
  expect(await editableGuiFontPackage('fixture',info(pkg))).toEqual({fonts:packageFonts(pkg),chineseSource:'catalog'})
  expect(await editableGuiFonts('fixture',info(pkg))).toEqual(packageFonts(pkg))
  await expect(editableGuiFontPackage('fixture',{...info(pkg),activeSize:pkg.totalSize+1})).rejects.toThrow('不一致')
  const different=buildFontRoles(defaultGuiFonts(),{},5)
  database.get.mockResolvedValue({...record(pkg),bytes:record(different).bytes})
  await expect(editableGuiFontPackage('fixture',info(pkg))).rejects.toThrow('不一致')
})
it('never guesses custom Chinese provenance, and refuses absent or stale local bytes',async()=>{
  const pkg=buildFontRoles(defaultGuiFonts(),{},4)
  database.get.mockResolvedValue(record(pkg))
  expect((await editableGuiFontPackage('fixture',info(pkg))).chineseSource).toBeUndefined()
  database.get.mockResolvedValue(undefined)
  await expect(editableGuiFontPackage('fixture',info(pkg))).rejects.toThrow('导入')
  database.get.mockResolvedValue({...record(pkg),generation:3})
  await expect(editableGuiFontPackage('fixture',info(pkg))).rejects.toThrow('导入')
})
it('remembers explicit catalog origin but never assumes it for imported/custom packages',async()=>{
  const pkg=buildFontRoles(defaultGuiFonts(),{},1)
  await rememberGuiAssetPackage('fixture',pkg,'catalog')
  expect(database.put.mock.calls[0][0].chineseSource).toBe('catalog')
  expect(new Uint8Array(await database.put.mock.calls[0][0].bytes.arrayBuffer())).toEqual(pkg.bytes)
  await rememberGuiAssetPackage('fixture',pkg)
  expect(database.put.mock.calls[1][0].chineseSource).toBeUndefined()
})
