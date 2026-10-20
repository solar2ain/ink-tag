# Public-release legal gate

The licensing decision is **AGPL-3.0-only for InkTag-owned CLI source** and
**compiled-only distribution for WebUI**. See [LICENSING.md](LICENSING.md).
This decision does not by itself qualify the current packages for public release.

Before changing repository visibility to public:

1. Align CLI package metadata, source ZIP, sdist and wheel with `cli/LICENSE`
   and `cli/LICENSING.md`; verify that the license is present in actual artifacts.
2. Resolve the bundled GUI WASM's license and combination with the AGPL CLI.
   Establish a complete, permitted Corresponding Source/build-material route,
   or an alternative reviewed packaging/licensing arrangement. Do not infer a
   linking exception or silently extend the grant to WebUI.
3. Confirm WebUI binary-use and redistribution terms with the rights holder.
   Keep the CLI grant clearly scoped rather than adding a blanket repository AGPL grant.
4. Complete the exact-version third-party inventory, copyrights and license
   texts for every distributed component, including Leafer packages/plugins,
   fonts, images and the local Find runtime. Supplementary upstream texts are
   not proof of the exact bundled version or a completed provenance audit.
5. Confirm whether GPL/AGPL derivative or combined works impose further source
   obligations; a rewrite, refactor or resource label alone does not remove them.
6. Keep the first release free of firmware, flashing packages, vendor SDKs,
   proprietary tools, credentials, user data and agent instruction files.
7. Rebuild all affected artifacts, generated manuals, standalone notices,
   manifests and checksums after the final changes; independently review the
   exact bytes. Existing preview archives and prior approval receipts do not
   cover subsequent documentation or code changes.

Do not mark the release ready or publish while these items remain unresolved.
No device-side security or physical OTA qualification is asserted by this checklist.

## Packaging implementation status (2026-10-20)

The local candidate now uses PEP639/AGPL-3.0-only metadata for CLI-owned source
and includes LICENSE, scope and notices in its source ZIP, sdist and actual wheel.
`tools/release_checks.py` checks those actual archive bytes; this closes a
packaging omission, not the renderer Corresponding Source review. Renderer,
plugin/font/image provenance and WebUI terms remain explicit public blockers in
`releases/readiness.json`. See `release-materials.md` for materials and steps.

Build locally with an environment containing `tools/requirements-package.txt`,
then run `python tools/pack-clients.py`. This only packages compiled WebUI already
present here; it does not rebuild WebUI source, sign firmware or publish.
The artifact binding records the current base commit plus a working-tree file
fingerprint. There is no final new commit or approval while commits are prohibited.
