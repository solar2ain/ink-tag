# Third-party components

InkTag host tool source uses the included root LICENSE (GPL-3.0). The isolated
CLI subset retains its separate current license and notices under
`tools/internal/runtime/cli/` (PolyForm Noncommercial1.0.0 for InkTag-owned CLI
source); the root notice must not relabel that subset. This is not a full CLI
or compiled-renderer distribution. Public export and mixed-component licensing
review remain separate from this local tools validation.
This archive contains only the necessary host/offline modules, not private
firmware C sources, SDK code, keys or the installed device CLI.

The six Python wheels retain their original license and metadata entries:

| Distribution | Version | License (see original files inside wheel) |
|---|---|---|
| cryptography | 50.0.0 | Apache-2.0 OR BSD-3-Clause; bundled third-party notices included |
| cffi | 2.1.1 | MIT-0; bundled libffi notices included |
| pycparser | 3.0 | BSD-3-Clause |
| qrcode | 8.2 | BSD |
| PyYAML | 6.0.3 | MIT |
| colorama | 0.4.6 | BSD |

Exact wheel names and SHA256 are recorded in the dependency lock. Each wheel
contains its original LICENSE/COPYING files; retain these when redistributing.
J-Link and Goodix native EXE/DLL/loaders are NOT included or sublicensed.
Users obtain them from the official URLs under their applicable licenses;
setup imports only the hash-pinned local files after an explicit request.
Python and PowerShell themselves are not included; obtain them from their
official providers under their respective licenses.
