# GitHub Pages deployment

[简体中文](../zh-CN/github-pages.md)

The intended site URL is <https://solar2ain.github.io/ink-tag/>. This remains a private preview. This update does not enable or deploy Pages, change repository visibility, or change account settings.

Only `webui/` is deployed: compiled assets and HTML manuals. CLI, firmware, recovery backups, device data, and agent instructions are excluded. Pushes and pull requests validate static content without deploying.

When deployment is explicitly enabled later, select `main` under Actions → Deploy InkTag Web → Run workflow. Resource references, sensitive files, hidden content, links, and source maps are checked before uploading `webui/`. No application source or Node.js build environment is required.

Private-repository Pages and Actions availability depends on the GitHub account plan and billing state. Workflow configuration does not prove the service is available; check Settings → Pages before enabling it. The supplied local package requires neither a public repository nor a plan change.

Pages serves static content without a local data or Find backend. Device connections still require Web Bluetooth support and explicit user authorization.
