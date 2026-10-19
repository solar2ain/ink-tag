$python = Join-Path $PSScriptRoot '.venv/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { throw 'Run install.ps1 first (Python 3.11+).' }
& $python -m inktag_cli @args
exit $LASTEXITCODE
