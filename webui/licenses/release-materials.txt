# Compiled-component materials required before public release

Status: **BLOCKED**. This document concerns the actual client files in this
repository only. No other development repository was accessed for this review.

## Observed combination

`cli/src/inktag_cli/gui_font_roles.py` evaluates `fontRoles.js` inside mini-racer.
For native sizing it evaluates `nativePreviewWasm.js` and `nativePreview.js`;
the latter constructs a WebAssembly.Module/Instance and calls the GUI ABI
`nf_config`, `nf_region` and related exports. The render/geometry function is
an integral runtime path in the same CLI process, not a separate viewer used
only by WebUI. `host_scene.py` likewise evaluates the bundled `hostScene.js`.
Neither filenames nor separate archive members establish mere aggregation.

The repository supplies the compiled module and wrappers, but no preferred-form
native GUI sources, complete generated font inputs, compiler/linker recipe or
reproducible WASM build. The font replacement and native preview paths are kept
functional in this LOCAL, UNAPPROVED candidate. A wheel with Python sources and
AGPL metadata does not resolve the compiled module's Corresponding Source.

## Executable resolution plan

1. Supply a separately reviewable renderer source-material archive containing
   every source/definition used by the exact module, preferred-form font inputs,
   generator scripts, build/link scripts, toolchain versions and flags. Include
   upstream copyright, modifications, licenses and applicable NOTICE files.
2. Establish rights to distribute that exact combination with the AGPL CLI.
   Obtain explicit grants from the relevant rights holders when necessary. No
   linking exception or WebUI-wide license grant is supplied by this change.
3. Rebuild the module from those materials in an isolated build. Record exact
   input/output hashes, or explain and test any non-reproducibility. Bind the
   material archive to the shipped WASM and JS adapter hashes.
4. Decide whether renderer materials can be distributed as a minimal independent
   source package without exposing unrelated WebUI source; all actually required
   sources still have to be included. Merely moving code to a resource directory
   or a subprocess is not a determination of licensing compatibility.
5. Obtain independent combined-work/source review. Only after evidence exists,
   close the corresponding-source fields in `releases/readiness.json`, refresh
   all artifacts, and issue a NEW exact-commit/byte-bound publication receipt.

`python tools/verify-release.py --repo . --public` and the executing publication
path refuse while this review is unresolved. Regular candidate verification
may pass to permit offline testing; it is not permission to publish.

## Other missing provenance

- Leafer runtime exposes 2.2.10, but this does not identify every included plugin,
  its version, preferred sources or license. Supply the exact package/build
  inventory and plugin NOTICE files, if any. The upstream core MIT text is not
  proof of all plugin rights.
- Confirm all native/generated font families, modifications and source inputs.
  Embedded font shapes and a filename do not establish a license.
- Confirm the shipped WOFF2 origin and modifications against its font metadata
  and authoritative source; retain OFL notices. Confirm origins/rights of
  tutorial screenshots and sample images, including generated artwork.
- Confirm the separately packaged Find runtime/dependencies, runtime download
  permissions, and binary-use/redistribution terms for InkTag-owned WebUI.

References: GNU AGPLv3 sections 1/6/13 and the GNU GPL FAQ (both from the official
GNU site); setuptools PEP639 documentation for the packaging metadata only.
https://www.gnu.org/licenses/agpl-3.0.html
https://www.gnu.org/licenses/gpl-faq.html
https://setuptools.pypa.io/en/stable/userguide/license_migration.html
