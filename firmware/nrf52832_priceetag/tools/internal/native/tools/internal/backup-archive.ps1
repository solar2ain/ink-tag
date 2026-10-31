# Internal host-only paths/provenance helper; no transport or retry logic.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'runtime.ps1')
if (!(Get-Command Write-InkTagHardwareMessage -ErrorAction SilentlyContinue)) {
    . (Join-Path $PSScriptRoot 'terminal-log.ps1')
}
function Assert-NordicArchivePath([string]$Path) {
    $next = [IO.Path]::GetFullPath($Path)
    while ($next) {
        if ((Test-Path -LiteralPath $next) -and
            ((Get-Item -LiteralPath $next -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Linked backup path refused: $next"
        }
        $next = [IO.Path]::GetDirectoryName($next)
    }
}
function Get-NordicCanonicalRoot([string]$RepoRoot) {
    if (Test-NordicToolBundle) { return Split-Path -Parent $script:NordicToolDirectory }
    # A registered worktree's common Git directory belongs to its primary tree.
    $common = $null
    $gitExit = 1
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $savedPreference = $ErrorActionPreference
        try {
            # Windows PowerShell 5 models native stderr as ErrorRecords. A
            # source export legitimately has no .git; retain the safe fallback.
            $ErrorActionPreference = 'Continue'
            $common = & git -C $RepoRoot rev-parse --path-format=absolute --git-common-dir 2>$null
            $gitExit = $global:LASTEXITCODE
        } finally { $ErrorActionPreference = $savedPreference }
    }
    if ($gitExit -eq 0 -and $common -and (Split-Path -Leaf $common) -eq '.git') {
        $root = Split-Path -Parent $common
    } else { $root = [IO.Path]::GetFullPath($RepoRoot) }
    Assert-NordicArchivePath $root
    return $root
}
function Get-NordicCaptureDirectory([string]$Canonical, [string]$Category) {
    if ($Category -notin @('quarantine-source-unconfirmed', 'full-current')) { throw 'Unknown capture category' }
    $token = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N')
    $path = Join-Path $Canonical ".local/backup-archive/nrf52832/$Category/$token"
    Assert-NordicArchivePath $path
    if (Test-Path -LiteralPath $path) { throw 'Independent capture path already exists' }
    return $path
}
function Resolve-NordicBackup([string]$RepoRoot, [string]$Path) {
    $root = Get-NordicCanonicalRoot $RepoRoot
    $resolved = Invoke-NordicPython (Join-Path $RepoRoot 'tools/hardware/nordic-backup-archive.py') resolve --directory $Path --canonical $root
    if ($LASTEXITCODE -ne 0) { throw 'Historical backup path resolution refused' }
    return [string]$resolved
}
function Find-NordicStockBackup([string]$RepoRoot) {
    $root = Get-NordicCanonicalRoot $RepoRoot
    foreach ($sourceRoot in @($RepoRoot, $root) | Select-Object -Unique) {
        $tracked = Join-Path $sourceRoot 'factory_backups/nrf52832/PriceETagV1.7'
        Assert-NordicArchivePath $tracked
        if (Test-Path -LiteralPath (Join-Path $tracked 'stock-flash.bin')) { return $tracked }
    }
    $candidates = @()
    # Retain read-only discovery of unpromoted historical captures. New verified
    # factory captures are routed to factory_backups, never back into .local.
    foreach ($stock in @((Join-Path $root 'factory_backups/nrf52832/PriceETagV1.7'),
                         (Join-Path $root '.local/backups/nrf52832/nrf52832_priceetag'))) {
      Assert-NordicArchivePath $stock
      if (Test-Path -LiteralPath $stock) {
        foreach ($session in Get-ChildItem -LiteralPath $stock -Directory) {
            Assert-NordicArchivePath $session.FullName
            # Factory status comes from the exact reviewed binary triple,
            # never from operator Board/BoardNote labels.
            $facts = Invoke-NordicPython (Join-Path $RepoRoot 'tools/hardware/nordic-backup-archive.py') require-stock --directory $session.FullName
            if ($LASTEXITCODE -ne 0) { throw 'Unqualified file in factory tree; use an explicitly verified StockBackup' }
            $candidates += $session.FullName
        }
      }
    }
    if ($candidates.Count -ne 1) { throw 'Factory backup missing or ambiguous; provide exact -StockBackup. No device read performed.' }
    return $candidates[0]
}
function Complete-NordicCapture([string]$RepoRoot, [string]$Directory, [bool]$DefaultPath) {
    $facts = Invoke-NordicPython (Join-Path $RepoRoot 'tools/hardware/nordic-backup-archive.py') inspect --directory $Directory
    if ($LASTEXITCODE -ne 0) { throw 'Capture provenance refused; original capture retained in isolation' }
    $classification = $facts | ConvertFrom-Json
    $manifest = Join-Path $Directory 'backup-provenance.json'
    if (Test-Path -LiteralPath $manifest) { throw 'Provenance manifest already exists; no overwrite' }
    $facts | Set-Content -LiteralPath $manifest -Encoding UTF8
    $manifestHash = (Get-FileHash -LiteralPath $manifest).Hash
    if ($DefaultPath) {
        $root = Get-NordicCanonicalRoot $RepoRoot
        $staging = [IO.Path]::GetFullPath((Join-Path $root '.local/backup-archive/nrf52832/quarantine-source-unconfirmed'))
        $absoluteSource = [IO.Path]::GetFullPath($Directory)
        if ((Split-Path -Parent $absoluteSource) -ne $staging) {
            throw 'Capture promotion source is outside the exact staging category; no move'
        }
        $session = Split-Path -Leaf $Directory
        $relative = if ($classification.factory) {"factory_backups/nrf52832/PriceETagV1.7/$session"}
                    else {".local/backup-archive/nrf52832/$($classification.category)/$session"}
        $destination = [IO.Path]::GetFullPath((Join-Path $root $relative))
        if ($destination -ne [IO.Path]::GetFullPath($Directory)) {
            Assert-NordicArchivePath $Directory; Assert-NordicArchivePath $destination
            if (Test-Path -LiteralPath $destination) { throw 'Archive destination already exists; no merge' }
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Move-Item -LiteralPath $Directory -Destination $destination
            # Verify the complete binary set again after physical promotion.
            $check = Invoke-NordicPython (Join-Path $RepoRoot 'tools/hardware/nordic-backup-archive.py') inspect --directory $destination
            if ($LASTEXITCODE -ne 0 -or [string]$check -ne [string]$facts -or
                (Get-FileHash -LiteralPath (Join-Path $destination 'backup-provenance.json')).Hash -ne $manifestHash) {
                throw 'Moved capture verification failed; retain evidence'
            }
            $Directory = $destination
        }
    }
    Write-InkTagHardwareMessage "Capture classification: $($classification.category); retained at $Directory"
}
