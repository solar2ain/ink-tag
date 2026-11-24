import { ToolRoute, type ToolLoader } from './ToolRoute'

type ImageToolLoader = ToolLoader
const loadImageTool:ImageToolLoader=()=>import('../pages/ImagePage').then(module=>({default:module.ImagePage}))

/** Keep the shell and its device connection mounted if this tool fails. */
export function ImageToolRoute({load=loadImageTool}:{load?:ImageToolLoader}){
  return <ToolRoute name="图片工具" load={load} />
}
