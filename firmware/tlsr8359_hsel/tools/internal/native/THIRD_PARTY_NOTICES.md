# TLSR tools-only licenses and dependencies

InkTag-owned scripts and included pure format codecs use PolyForm Noncommercial
1.0.0 (LICENSE; also cli/LICENSE). Required Notice: Copyright InkTag contributors
(https://github.com/solar2ain/ink-tag). Previously published grants remain valid.
This archive contains no compiled renderer or MCU firmware source/binaries.

The included pvvx/TlsrComSwireWriter UART/SWS backend retains its original
Unlicense/public-domain terms. Its complete LICENSE and README are beside
TLSR825xComFlasher.py under .tools/telink/TlsrComSwireWriter/. Exact dependency
hashes are in tools-manifest.json and the package producer; no license is
replaced by InkTag's noncommercial terms.

Python (PSF), pyserial (BSD), cryptography (Apache-2.0 OR BSD-3-Clause), PyYAML
(MIT), cffi (MIT), pycparser (BSD), qrcode (BSD) and colorama (BSD) are separate
host prerequisites installed from their normal distributions. They are not
embedded in this tools directory. Their
original distribution license/NOTICE files remain applicable. Exact Python
package versions are in requirements-tools.txt; no firmware version is bound.
