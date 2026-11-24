# InkTag WebUI

## Responsive layout contract

- `800px` is the first desktop width; `799px` and below use the mobile shell.
  A mobile browser UA also selects the mobile shell at wider CSS widths.
- Compact toolbars use icons or short labels. Keep the full command name in
  `title` and `aria-label`; do not let command text wrap to make a row fit.
- Put nonessential explanatory copy behind a keyboard-accessible `?` help
  control beside its setting, with a mobile-safe click/tap disclosure. Keep
  safety, privacy, permission and failure notices visible rather than hiding
  them behind help.
- Device canvases retain their real pixel dimensions internally. On narrow
  screens, scale the canvas element as one fixed-aspect bitmap instead of
  reflowing text inside the template.
- Before handoff, check all six primary routes at `390px`, `799px` and `800px`.
  The document must not be wider than the viewport, and visible controls must
  not depend on wrapped button labels.

InkTag WebUI has two frontend builds and three supported deployment modes.
Both builds come from the same React and TypeScript source tree. It is the
maintained frontend; Web Bluetooth talks directly
to the tag and IndexedDB stores pages and assets without requiring a server.

After a device connects, WebUI checks the update feed for its firmware edition.
An update requires a newer version and an exact target match. If the feed is
unavailable, Bluetooth operations continue. In Device → Firmware, you can
download the offered package and confirm the upgrade after package checks.

For local development:

```powershell
cd webui
npm ci
npm run dev
```

## Build outputs

Run:

```powershell
npm ci
npm run build
```

The build creates:

- `dist/static/`: hashed HTML, CSS, JavaScript and lazy-loaded chunks for an
  HTTP(S) host;
- `dist/standalone/InkTag-WebUI.html`: an offline page for direct `file://`
  use, with optional fonts and images in the sibling `assets/` directory.

Release checkouts include both output directories alongside the source. See
the [WebUI build guide](../docs/build-webui.en.md) for rebuilding and reinstalling
the CLI against the updated `dist/static/`.

The build verifier rejects a static build that accidentally inlines everything
and a standalone build that still depends on an external script or stylesheet.

## Deployment modes

### Static hosting

Publish the contents of `dist/static/` to GitHub Pages, Nginx, Caddy or another
static host. GitHub Pages deployment uses this output directly.

### Local server

Use the maintained Python CLI for the WebUI, local data API and optional Find
backend on one origin. No Node.js runtime is required after the frontend build.
From the source root, with the CLI installed:

```powershell
inktag webui serve --repo . --port 8787
```

The service uses your operating system's InkTag data directory. For a separate
development profile, pass an unused `--port` and an absolute `--data-dir`.

For phone/LAN access use `inktag webui serve --lan`; it prepares HTTPS and an
access password. A trusted HTTPS origin is needed for browser Bluetooth away
from localhost. `--tls-cert` and `--tls-key` may select existing certificates;
`--allowed-host` adds an explicit trusted hostname where needed. See the CLI
manual for installation, certificate trust and platform-specific Find setup.

Find authentication, identities and queries stay in the local Python service.
Static hosting and standalone mode do not provide that backend. Neither a
frontend build nor these commands imply that Apple login or a device was tested.

### Standalone

Open `dist/standalone/InkTag-WebUI.html` directly and keep `assets/` beside it.
Browser support for Bluetooth and storage under `file://` varies.

## Runtime behavior

Hash routing keeps both builds usable below a repository sub-path. Viewports
below 800px use mobile navigation, while 800px and wider use the compact
desktop layout. A mobile browser hint also selects the mobile layout on wide
phone viewports. Theme selection supports system, light and dark modes and is
persisted locally when browser storage is available.

GitHub Pages runs the WebUI entirely as static files. Device access, page
documents, assets, user screen profiles and per-device drafts stay in the
browser. The optional local server must not proxy or gate direct Web Bluetooth
control.

## Editor architecture

New creation uses the fixed-resolution scene editor at `/create/canvas/:id`.
Leafer provides the browser scene and input adapter; versioned documents,
widget geometry, selection, structure edits and undo/redo are owned by
`packages/inktag-editor-core/`, independently of React or a browser canvas.

The user-facing resource model is deliberately smaller than its historical
storage tables:

- A **template** is an immutable starting structure, not a saved user work.
  Selecting one creates a new page; starting with a blank canvas does the same.
- **My pages** contains editable saved works, each with its own identity and
  fixed pixel dimensions. Saving a new work never overwrites another work
  merely because both use the same template and size. Continuing a saved work
  updates that work. Preview PNGs are optional derived data and omit the
  device-owned edition placeholder.
- **Image assets** are reusable source images. Inserting one copies it into
  the page; changing the page does not modify the source asset.

The `pages` IndexedDB table stores only current scene documents. The retired
Fabric/native editing pages, routes, helpers and Fabric dependency are removed.
Database upgrade 8 removes the old editing documents, preserves current scene
pages saved during the pilot, and upgrade 9 drops the pilot draft table. This
owner-approved format cut does not clear image assets, screen profiles, device
preferences, device records or Find data. There is no old-editor import path.
Template thumbnails use the same document and scene renderer as the editor.

The creation page offers **New blank page**, **Start from a template** and
**My pages**. Saving a work explicitly chooses **Page** or **Template**, a name
and a content category. Pages go to Resources / My pages; user templates go to
the creation catalogue's My templates source. Both lists can filter by content
type, fixed resolution, orientation and name. My pages additionally sorts by
last save, name or resolution. Database upgrade 10 adds a separate `templates`
table and a category index without clearing current pages or assets.

Starting from a user template copies its scene into a new independent page.
Neither edits nor saving a derived template overwrite the source template.
Deleting a user template leaves previously created pages untouched. Preset
templates remain read-only. Templates are fixed-size; filtering by another
resolution does not stretch them or saved pages.

When a device is connected, **New blank page** uses its screen dimensions,
orientation and color mode directly; it does not open a setup dialog. Without
a connected device, the setup dialog remains available for offline creation.

On mobile, the canvas stays foremost. A bottom button opens the controls or
properties sheet, with switches and a collapse button at its upper edge.
The sheet occupies layout space rather than overlaying the canvas; resize
observation fits and centers the canvas within the remaining stage without
changing document dimensions, font sizes or exported pixels.

Saving stays local; it does not send data to a device. The current send path
exports an exact-resolution image through the shared client, not a firmware
widget/layout program. Shared editor documents do not imply support for the
reserved device custom-layout commands.

## Device settings boundaries

- Screen profiles contain controller, resolution, color, transform and an
  optional visible viewport. Board pins remain a separate board setting.
- The Lenmory/图公主 `H364F067` profile is a six-color `BWRYGB` target. The
  image workbench previews and dithers against all six pigments, then sends
  two full 107,920-byte packed-2bpp stages: Area0 for blue/white/green/red and,
  without a controller reset, Area6 for black/yellow. Test pages use the same
  palette instead of falling back to the four-color test pattern. The
  760-by-568 controller canvas remains intact until the likely two-column
  visible crop is physically qualified on InkTag hardware.
- The `0.2.0` refinement uses concrete panel presets and adds a controller-
  scoped `panel_recipe` field, with recipe zero and an unknown-value fallback
  both selecting `STANDARD`. It does not add physical-size control fields or a
  generic recipe interpreter. See
  [`docs/panel-profile-architecture.md`](../docs/panel-profile-architecture.md).
- Previously authorized Bluetooth devices are obtained from the browser with
  `getDevices()`; first authorization always uses the browser chooser.
- Device aliases, LED policies and NFC tap behavior are persisted as local
  drafts until matching firmware commands are published.
- One NFC field detection maps to one `NFC_TAP`. The selected behavior is next
  page, a fixed device image slot, or a standard NDEF control URL. The exact
  ZKC42V test firmware can read and consume a bounded `IKNC1` Text record that
  a phone wrote through RF, but the WebUI does not write those phone-side
  command records and the device does not advertise MCU-side `NDEF_WRITE`.
- NFC control URLs are limited to 192 UTF-8 bytes. HTTPS is accepted globally;
  HTTP is limited to private/link-local networks and `.local` hosts. Loopback
  addresses are rejected because a tap opens the URL on the phone itself.
- Firmware activation uses an offline BLE transport. WebUI writes an activation
  code into device Flash; firmware validates it locally and permanently enables
  the advanced edition. Browser state never grants firmware capabilities, and
  no activation server or expiry model is required.

The WebUI release version is `webuiVersion` in the repository `version.json`.
Firmware continues to use `version` and its matching numeric fields. Run
`python tools/version/sync-version.py` after changing either version.
