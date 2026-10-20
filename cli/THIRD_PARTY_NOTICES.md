# CLI third-party and compiled-component notices

Only InkTag-owned CLI source is granted under AGPL-3.0-only. See LICENSE and
LICENSING.md. Dependencies retain their own terms; this is not a blanket grant
for every byte in a wheel or an exception for linked or bundled components.

- epdoptimize: the bundled notice identifies 1.3.0, Apache-2.0; its full notice
  is also retained at `src/inktag_cli/_assets/EPDOPTIMIZE_LICENSE.txt`.
- Python dependencies are installed separately from the declared requirements;
  these client archives do not vendor their wheels. Their licenses and resolved
  environment must be reviewed when distributing a preinstalled environment.
- `fontRoles.js`, `nativePreview.js`, `nativePreviewWasm.js`, `hostScene.js` and
  `legacyDithering.js` are distributed runtime inputs, not proof of complete
  preferred-form Corresponding Source. In particular the embedded native GUI
  WASM is instantiated by mini-racer through the font adapter in the CLI process.

Public distribution remains BLOCKED pending exact provenance, compatible grants,
complete preferred-form sources, included/generated font data and their notices,
and reproducible compiler/linker/build scripts and flags for the embedded module.
The inspection of this repository cannot supply those missing materials. No
functionality has been removed to hide the problem. See the release licensing
gate in the repository root. Metadata and inclusion of AGPL text are necessary
packaging fixes, not a certification of the combined distribution's licensing.
