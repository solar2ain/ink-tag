import { Download } from 'lucide-react'
import { Link } from 'react-router-dom'
import { PageHeader } from '../components/PageHeader'

const sections = [
  { title: '1. 安装 InkTag CLI', text: '需要 Python 3.11 或以上版本。从 InkTag 发布包安装 CLI；如果已经安装，可直接继续。发布后的 WebUI 不需要 Node.js。', code: 'python -m pip install <下载的 InkTag-cli 压缩包路径>\ninktag --help' },
  { title: '2. 启动本地服务', text: '在终端中运行以下命令，服务将在本机后台运行。启动服务不会自动连接蓝牙设备或发送画面。', code: 'inktag webui start --port 8787' },
  { title: '3. 打开 WebUI', text: '服务启动后，在同一台电脑的浏览器中打开 http://127.0.0.1:8787。推荐使用支持 Web Bluetooth 的 Chrome 或 Edge；首次连接仍需你选择并授权设备。', code: 'inktag webui status' },
  { title: '4. 管理服务与查看日志', text: '查看日志可帮助定位启动、登录或查询错误。停止服务不删除本地页面、身份库和其他数据。', code: 'inktag webui log --follow\ninktag webui restart\ninktag webui stop' },
]
const note = 'Find 查询需要本地服务后端，静态站点和单文件模式不提供该后端。启动服务后，仍需在 Find 页面按提示完成账户登录与身份配置。请勿把账户、密码、身份私钥或本地数据目录上传到公开网站。'
const readme = '# 如何本地部署 InkTag WebUI 服务\n\n' + sections.map(s => `## ${s.title}\n\n${s.text}\n\n\`\`\`text\n${s.code}\n\`\`\`\n`).join('\n') + '\n## Find 与隐私\n\n' + note + '\n'

export function LocalWebuiHelpPage() {
  return <div className="page local-webui-help">
    <PageHeader title="本地部署 WebUI 服务" actions={<a className="button secondary" download="InkTag-WebUI-README.md" href={`data:text/markdown;charset=utf-8,${encodeURIComponent(readme)}`}><Download />下载 README</a>} />
    <p>在自己的电脑上运行 InkTag WebUI，使用本地数据存储和 Find 查询服务。</p>
    <article>{sections.map(section => <section key={section.title}><h2>{section.title}</h2><p>{section.text}</p><pre><code>{section.code}</code></pre></section>)}<section><h2>Find 与隐私</h2><p>{note}</p></section></article>
    <Link className="button secondary" to="/find">返回查找</Link>
  </div>
}
