# Browser OTA adapters

The current WebUI loads these adapters as raw source in `protocol/ota.ts`.
Node regression tests load the same files through their existing CommonJS/UMD
interface. The local package marker preserves that interface inside the ESM
WebUI project; no adapter implementation changed during relocation.

Keep signature, layout, exact-device reconnect, rejection and completion checks
in the maintained adapters. Do not add a dependency on a retired frontend tree.
