# Third-party components

InkTag-owned host tool source and the included offline CLI subset use AGPL-3.0-only. The tool and CLI license notices accompany the source.

This directory contains the necessary host/offline modules and the existing
project default native-font template used to initialize the GUI resource area.
The template is data, not an App/Recovery image or renderer executable. Its
fonts retain their original terms; this tool update does not relicense them.
Private firmware C sources, SDK code, keys and the installed device CLI are
not included. Native generation does not need the legacy Python wheels below.

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
