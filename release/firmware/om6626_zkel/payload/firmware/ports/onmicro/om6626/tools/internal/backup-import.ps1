[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$')][string]$Board,
    [string]$Source,
    [string]$OutputDir,
    [string]$BoardNote=''
)
# Local immutable-file import only. No programmer, transport or inferred read.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../terminal-log.ps1')
$terminalState=Get-InkTagTerminalColour
try {
    if(!$Board -or !$Source -or !$OutputDir){throw 'Board, Source and OutputDir are required'}
    $item=Get-Item -LiteralPath $Source
    if($item.PSIsContainer -or $item.Length -ne 1048576){throw 'OM backup import requires exactly 1048576 bytes'}
    $before=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
    $directory=Join-Path $OutputDir ((Get-Date -Format 'yyyyMMddTHHmmssfff')+'-import-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $directory -ErrorAction Stop | Out-Null
    $target=Join-Path $directory 'stock-flash.bin'
    Copy-Item -LiteralPath $item.FullName -Destination $target -ErrorAction Stop
    $after=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if($before -ne $after){throw 'Imported backup hash mismatch; preserve both files'}
    @{board=$Board;board_note=$BoardNote;capture_method='manual-file-import-not-a-live-read';bytes=1048576;sha256=$after;source=$item.FullName} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $directory 'backup-info.json') -Encoding utf8
    Write-InkTagTerminalMessage "Backup import OK: $target (no device accessed)" -Level SUCCESS
    Write-InkTagTerminalMessage "SHA256: $after" -Level DEBUG
    exit 0
} catch {Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR;exit 1}
finally {Restore-InkTagTerminalColour $terminalState}
