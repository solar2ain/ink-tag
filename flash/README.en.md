# Flashing entry points

Windows candidate entry points are under `windows/` and are also packaged as `InkTag-flash-tools-v0.2.0.zip`. `runtime/` is called only by those wrappers and is not a user interface. Linux and macOS first-flash flows have not completed candidate hardware qualification, so no misleading entry point is shipped for them.

The release repository does not bundle vendor SDKs or proprietary programming applications. Each wrapper must:

- support `-Help`/`-h` (or `--help` on POSIX);
- wait for a target by default where that is safe;
- select firmware by exact board ID rather than MCU family alone;
- show erase/program/verify progress without flooding failed-connection logs;
- verify the release SHA-256 before programming;
- link to the official tool and state the tested version when external software is required;
- preserve the stock-backup and board-layout safety gates documented for that target.

See the [full flashing guide](../docs/en/flashing.md) for exact commands, dependencies, and recovery criteria. If this candidate attaches a Full image but provides no independently usable wrapper for its board, report an audit blocker. Do not silently replace the missing entry with an unreviewed vendor-GUI procedure.
