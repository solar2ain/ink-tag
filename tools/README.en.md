# Public tooling

- `findmy/`: local-client and Find query installation/authentication/UI resources required for local use.
- Root Python scripts: manual generation, release feeds, attachment verification and Pages checks. These maintain the public repository; publication is not performed by default and still requires explicit execution and independent approval.
- `tests/`: offline regressions for public maintenance scripts, without device-development diagnostics.

Board flashing tools are in `firmware/<controller_board>/tools/`. See [complete flashing](../docs/en/flash.md). Private firmware source, SDKs, private keys, user data and internal device diagnostics do not belong here.
