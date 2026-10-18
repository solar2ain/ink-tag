# GitHub Pages deployment

[简体中文](../zh-CN/github-pages.md)

The site URL is <https://solar2ain.github.io/ink-tag/>. Only `webui/` is uploaded: compiled assets and HTML manuals. CLI, firmware, recovery backups, device data and agent instruction files are excluded.

Changes to `webui/`, the deployment workflow or its static verifier deploy automatically after merging into `main`. For a manual deployment, select `main` under Actions → Deploy InkTag Web → Run workflow. Pull requests validate without deploying.

The workflow checks static files, sensitive files and resource references before using the official GitHub Pages Actions. No Node.js build environment or application source checkout is required.

Private repositories require a GitHub plan supporting private-repository Pages. On 2026-10-18, GitHub rejected site creation with HTTP 422 because the current plan does not support it. Repository visibility remains unchanged. Once the plan supports Pages, rerun the workflow to attempt automatic enablement, or select GitHub Actions under Settings → Pages → Source.

The site is static and provides no local data or Find backend. Device control requires a browser supporting Web Bluetooth and explicit device permission.
