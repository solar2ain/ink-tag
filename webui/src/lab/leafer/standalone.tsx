import { createRoot, type Root } from 'react-dom/client'
import { RichTextSelectionProvider } from './RichTextEditor'
import { Lab } from './main'

const root = (import.meta.hot?.data.root as Root | undefined) ?? createRoot(document.getElementById('leafer-lab-root')!)
if (import.meta.hot) import.meta.hot.data.root = root
root.render(<RichTextSelectionProvider><Lab /></RichTextSelectionProvider>)
import.meta.hot?.prune(() => root.unmount())
