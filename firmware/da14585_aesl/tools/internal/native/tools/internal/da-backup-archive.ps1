# Host-only archival policy. Import never attaches a debugger or reads a target.
function ConvertTo-InkTagDaArchiveBoard {
    param([Parameter(Mandatory)][string]$Board)
    switch ($Board.ToLowerInvariant()) {
        'da14585_asel29' { return 'aesl29_da' }
        'aesl29_da' { return 'aesl29_da' }
        'da14585_asel42' { return 'aesl42_da' }
        'aesl42_da' { return 'aesl42_da' }
        default { throw "Unknown registered DA board: $Board" }
    }
}

function Get-InkTagDaCanonicalRoot {
    param([Parameter(Mandatory)][string]$RepoRoot)
    $root = [IO.Path]::GetFullPath($RepoRoot)
    $gitFile = Join-Path $root '.git'
    if (Test-Path -LiteralPath $gitFile -PathType Leaf) {
        $line = (Get-Content -LiteralPath $gitFile -Raw).Trim()
        if ($line -notmatch '^gitdir: (.+)$') { throw 'Invalid worktree git directory' }
        $gitDir = if ([IO.Path]::IsPathRooted($Matches[1])) { [IO.Path]::GetFullPath($Matches[1]) }
                  else { [IO.Path]::GetFullPath((Join-Path $root $Matches[1])) }
        $common = Join-Path $gitDir 'commondir'
        if (!(Test-Path -LiteralPath $common -PathType Leaf)) { throw 'Worktree common directory missing' }
        $commonDir = [IO.Path]::GetFullPath((Join-Path $gitDir (Get-Content -LiteralPath $common -Raw).Trim()))
        if ((Split-Path -Leaf $commonDir) -ne '.git') { throw 'Unsupported canonical repository layout' }
        $root = Split-Path -Parent $commonDir
    }
    return $root
}

function Assert-InkTagDaNoArchiveLinks {
    param([Parameter(Mandatory)][string]$Path)
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Archive path contains a link: $current"
            }
        }
        $parent = Split-Path -Parent $current
        if (!$parent -or $parent -eq $current) { break }
        $current = $parent
    }
}

function New-InkTagDaCaptureSession {
    param([Parameter(Mandatory)][string]$RepoRoot, [string]$OutputDir = '',
          [string]$ExpectedBoard = '', [string]$BoardNote = '',
          [ValidateSet('quarantine','debug','flash-history')][string]$Category = 'quarantine')
    if ($BoardNote -and ($BoardNote -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$' -or
        $BoardNote -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])$')) { throw 'BoardNote is a safe production-code tag, not a path or configuration' }
    $canonical = Get-InkTagDaCanonicalRoot $RepoRoot
    $root = if ($OutputDir -and [IO.Path]::IsPathRooted($OutputDir)) { [IO.Path]::GetFullPath($OutputDir) }
            elseif ($OutputDir) { [IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDir)) }
            else { Join-Path $canonical ('.local/backup-archive/da14585/'+$Category) }
    if (($root -split '[\\/]') -contains 'backups') { throw 'backups is reserved for qualified original manufacturer captures; use backup-archive for unqualified/current/debug data' }
    Assert-InkTagDaNoArchiveLinks $root
    $session = [DateTimeOffset]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N')
    $path = Join-Path $root $session
    New-Item -ItemType Directory -Path $path -ErrorAction Stop | Out-Null
    # ExpectedBoard is caller input, not verified PCB. Never infer silk from MAC.
    return [pscustomobject]@{path=$path;canonicalRoot=$canonical;session=$session;
        expectedChip='DA14585';verifiedChip=$null;expectedBoard=$ExpectedBoard;
        pcb='UNIDENTIFIED-PCB';boardNote=$BoardNote;category=$Category;stockQualified=$false}
}

function Get-InkTagDaCaptureClassification {
    param([Parameter(Mandatory)][string]$Path)
    Assert-InkTagDaNoArchiveLinks $Path
    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
    if ($text.Contains('board=da14585_aesl') -or $text.Contains('DA_EPD') -or
        $text.Contains('IKT_DA')) { return 'inktag-current' }
    # Missing InkTag markers, successful reads, CRC or stock filename alone
    # cannot establish manufacturer provenance. No automatic stock promotion.
    return 'quarantine'
}

function Write-InkTagDaCaptureReceipt {
    param([Parameter(Mandatory)]$Session, [Parameter(Mandatory)][string]$Capture,
          [Parameter(Mandatory)][uint32]$RangeStart, [Parameter(Mandatory)][uint32]$RangeBytes,
          [Nullable[int]]$NativeExitCode, [Nullable[bool]]$ReadSucceeded)
    Assert-InkTagDaNoArchiveLinks $Capture
    $root = [IO.Path]::GetFullPath($Session.path).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    $full = [IO.Path]::GetFullPath($Capture)
    if (!$full.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) { throw 'Capture escaped its independent session' }
    $receipt = Join-Path $Session.path 'capture-receipt.json'
    if (Test-Path -LiteralPath $receipt) { throw 'Capture receipt already exists; never overwrite a previous session' }
    $report = [ordered]@{schemaVersion=1;kind='DA-capture-archive';session=$Session.session;
        expectedChip=$Session.expectedChip;verifiedChip=$Session.verifiedChip;
        expectedBoard=$Session.expectedBoard;pcb=$Session.pcb;boardNote=$Session.boardNote;
        classification=(Get-InkTagDaCaptureClassification $Capture);stockQualified=$false;
        path=$full;bytes=(Get-Item -LiteralPath $full).Length;sha256=(Get-FileHash -LiteralPath $full).Hash;
        rangeStart=$RangeStart;rangeBytes=$RangeBytes;nativeExitCode=$NativeExitCode;readSucceeded=$ReadSucceeded;
        provenance='No qualified original-source/actual-chip/PCB evidence; retained current or quarantine capture only'}
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receipt -Encoding UTF8
    return [pscustomobject]$report
}

function Resolve-InkTagDaArchivedPath {
    param([Parameter(Mandatory)][string]$CanonicalRoot, [Parameter(Mandatory)][string]$Path)
    $old = [IO.Path]::GetFullPath($Path)
    if (Test-Path -LiteralPath $old -PathType Leaf) { Assert-InkTagDaNoArchiveLinks $old; return $old }
    $indexRoot = Join-Path $CanonicalRoot '.local/backup-archive/da14585/index'
    if (!(Test-Path -LiteralPath $indexRoot)) { throw "Archived path not found: $old" }
    Assert-InkTagDaNoArchiveLinks $indexRoot
    $archiveMatches = @()
    foreach ($directory in Get-ChildItem -LiteralPath $indexRoot -Directory) {
        Assert-InkTagDaNoArchiveLinks $directory.FullName
        $mapping = Join-Path $directory.FullName 'mapping.json'
        if (!(Test-Path -LiteralPath $mapping -PathType Leaf)) { continue }
        Assert-InkTagDaNoArchiveLinks $mapping
        $archiveMatches += @((Get-Content -LiteralPath $mapping -Raw | ConvertFrom-Json).mapping | Where-Object oldPath -eq $old)
    }
    if ($archiveMatches.Count -ne 1) { throw 'Archive mapping missing or ambiguous; do not pick a newest duplicate' }
    $entry = $archiveMatches[0]; $target = [IO.Path]::GetFullPath($entry.newPath)
    $allowed = [IO.Path]::GetFullPath((Join-Path $CanonicalRoot '.local/backup-archive/da14585')).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if (!$target.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Archive mapping escaped DA root' }
    Assert-InkTagDaNoArchiveLinks $target
    if (!(Test-Path -LiteralPath $target -PathType Leaf) -or
        (Get-Item -LiteralPath $target).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $target).Hash -ne $entry.sha256) { throw 'Archive file hash/size mismatch' }
    return $target
}

function Find-InkTagDaQualifiedStockBackup {
    param([Parameter(Mandatory)][string]$RepoRoot, [Parameter(Mandatory)][string]$Board)
    # There is currently no approved current-unit DA manufacturer catalog.
    # Root-organized historical templates in factory_backups retain their
    # original qualification; their legacy copies are kept for compatibility.
    # They are not current-unit stock discovery. No newest-name promotion.
    return ''
}

function Resolve-InkTagDaHistoricalFactoryTemplate {
    param([Parameter(Mandatory)][string]$RepoRoot,
          [string]$FactoryBackupRoot = '', [Parameter(Mandatory)][string]$Board)
    # Build-time historical content only. This does not qualify any current
    # unit's stock backup, OTP identity, Full preservation or restore operation.
    $internalBoard = ConvertTo-InkTagDaArchiveBoard $Board
    $baselineBoard = if ($internalBoard -eq 'aesl29_da') { 'da14585_aesl29' } else { 'da14585_aesl42' }
    $expectedHash = if ($internalBoard -eq 'aesl29_da') {
        '21EC46C7919A1687481658E647BA4A759DB2F0502B5078704E2DFA9080EC1662'
    } else { 'B3E3FF4D5F4E38E6A3639CE8458B85607E67D645C3061ABC76AFEC1E749EE097' }
    $root = if ($FactoryBackupRoot) { [IO.Path]::GetFullPath($FactoryBackupRoot) }
            else { Join-Path (Get-InkTagDaCanonicalRoot $RepoRoot) 'factory_backups/da14585' }
    Assert-InkTagDaNoArchiveLinks $root
    $boardRoot = Join-Path $root $baselineBoard
    if (!(Test-Path -LiteralPath $boardRoot -PathType Container)) { throw "Historical factory-template board root missing: $boardRoot" }
    Assert-InkTagDaNoArchiveLinks $boardRoot
    $flat = Join-Path $boardRoot 'stock-flash.bin'
    if ($FactoryBackupRoot -and (Test-Path -LiteralPath $flat -PathType Leaf)) {
        # Explicit legacy compatibility only, pinned to the same two frozen
        # contents. Never infer current-unit factory provenance from this path.
        $template = $flat
    } else {
        $sessions = @(Get-ChildItem -LiteralPath $boardRoot -Directory)
        if ($sessions.Count -ne 1) { throw 'Historical template session missing or ambiguous; no newest/dedup selection' }
        $session = $sessions[0].FullName
        Assert-InkTagDaNoArchiveLinks $session
        $receiptPath = Join-Path $session 'backup.json'
        Assert-InkTagDaNoArchiveLinks $receiptPath
        $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        if ($receipt.schema -ne 'inktag-historical-backup-v1' -or $receipt.chip -ne 'da14585' -or
            $receipt.board -ne $baselineBoard -or $receipt.classification -ne 'historical-factory-content-baseline' -or
            $receipt.currentDeviceQualified -isnot [bool] -or $receipt.currentDeviceQualified -ne $false -or
            $receipt.hardwareActions -ne 0 -or $receipt.boardNote -isnot [string]) {
            throw 'Historical template receipt does not preserve its original qualification'
        }
        $files = @($receipt.files)
        if ($files.Count -ne 1 -or $files[0].relative -ne 'stock-flash.bin' -or
            $files[0].bytes -ne 262144 -or $files[0].sha256 -ne $expectedHash) {
            throw 'Historical template receipt file/range/hash mismatch'
        }
        # BoardNote is receipt metadata only; there is no note directory.
        $template = Join-Path $session 'stock-flash.bin'
    }
    Assert-InkTagDaNoArchiveLinks $template
    if ((Get-Item -LiteralPath $template).Length -ne 262144 -or
        (Get-FileHash -LiteralPath $template).Hash -ne $expectedHash) { throw 'Historical factory-template bytes changed' }
    return $template
}
