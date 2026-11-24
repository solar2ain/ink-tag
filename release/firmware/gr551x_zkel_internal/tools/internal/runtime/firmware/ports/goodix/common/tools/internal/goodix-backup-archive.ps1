# Host-only catalog/paths, not a hardware or new-public-interface state machine.
function Get-InkTagGoodixFileSHA256 {
    param([string]$Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}

function Assert-InkTagGoodixArchiveDirectory {
    param([string]$Path)
    $part = [IO.Path]::GetFullPath($Path)
    while ($part) {
        if (Test-Path -LiteralPath $part) {
            if ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Backup path cannot follow symlinks/junctions' }
        }
        $part = [IO.Path]::GetDirectoryName($part)
    }
}

function Invoke-InkTagGoodixBackupCatalog {
    param([string]$RepoRoot, [string[]]$CatalogArgs)
    $scriptPath = Join-Path $RepoRoot 'tools/hardware/goodix-backup-catalog.py'
    $output = & python $scriptPath @CatalogArgs --repo-root $RepoRoot
    if ($LASTEXITCODE -ne 0) { throw "Goodix backup provenance/path check failed: $output" }
    return ($output | ConvertFrom-Json)
}

function Get-InkTagGoodixBackupPrimaryRoot {
    param([string]$RepoRoot)
    return (Invoke-InkTagGoodixBackupCatalog -RepoRoot $RepoRoot -CatalogArgs @('primary-root')).primaryRoot
}

function Resolve-InkTagGoodixHistoricalBackupPath {
    param([string]$RepoRoot, [string]$Path)
    return (Invoke-InkTagGoodixBackupCatalog -RepoRoot $RepoRoot -CatalogArgs @('map-path', '--file', $Path)).path
}

function Resolve-InkTagGoodixStockBackup {
    param([string]$RepoRoot, [string]$Chip, [string]$Board, [string]$StockBackup = '')
    $arguments = @('find-stock', '--chip', $Chip, '--board', $Board)
    if ($StockBackup) { $arguments += @('--file', $StockBackup) }
    return (Invoke-InkTagGoodixBackupCatalog -RepoRoot $RepoRoot -CatalogArgs $arguments).path
}

function Preserve-InkTagGoodixCaptureFile {
    param([string]$SessionRoot, [string]$Path)
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $root = [IO.Path]::GetFullPath($SessionRoot).TrimEnd('\', '/')
    $file = [IO.Path]::GetFullPath($Path)
    if (!$file.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Capture preservation outside exact session' }
    Assert-InkTagGoodixArchiveDirectory -Path $file
    $saved = $file + '.failed-' + [Guid]::NewGuid().ToString('N')
    if (Test-Path -LiteralPath $saved) { throw 'Failed capture collision' }
    Move-Item -LiteralPath $file -Destination $saved
}

function Complete-InkTagGoodixBackupArchive {
    param([string]$RepoRoot, [string]$ArchiveRoot, [string]$SessionRoot,
          [string]$BackupFile, [string]$Chip, [string]$Board = '', [switch]$VerifiedChip)
    $arguments = @('route', '--chip', $Chip, '--file', $BackupFile)
    if ($Board) { $arguments += @('--board', $Board) }
    if ($VerifiedChip) { $arguments += '--verified-chip' }
    $record = Invoke-InkTagGoodixBackupCatalog -RepoRoot $RepoRoot -CatalogArgs $arguments
    $root = [IO.Path]::GetFullPath($ArchiveRoot).TrimEnd('\', '/')
    $source = [IO.Path]::GetFullPath($SessionRoot)
    if (!$source.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Backup session is outside the explicitly selected archive root'
    }
    $target = [IO.Path]::GetFullPath((Join-Path (Join-Path $root $record.relativeDirectory) (Split-Path $source -Leaf)))
    if (!$target.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid archive destination' }
    $paths = @($source, $target)
    foreach ($path in $paths) {
        $part = $path
        while ($part) {
            if (Test-Path -LiteralPath $part) {
                if ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Backup archive cannot use links' }
            }
            $part = [IO.Path]::GetDirectoryName($part)
        }
    }
    if (Test-Path -LiteralPath $target) { throw 'Archive collision; never overwrite/deduplicate prior capture' }
    $files = @(Get-ChildItem -LiteralPath $source -Recurse -Force)
    if (@($files | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Linked file inside backup session' }
    $inventory = @($files | Where-Object { !$_.PSIsContainer } | ForEach-Object {
        [pscustomobject]@{original=$_.FullName;relative=$_.FullName.Substring($source.Length + 1);
                         bytes=$_.Length;sha256=(Get-InkTagGoodixFileSHA256 -Path $_.FullName)}
    })
    # Freeze inventory before the bounded directory move; original per-read
    # reports remain byte-identical, with this receipt mapping their old paths.
    $receipt = Join-Path $source 'archive-index.json'
    if (Test-Path -LiteralPath $receipt) { throw 'Archive receipt already exists' }
    [pscustomobject]@{schema=1;classification=$record;source=$source;destination=$target;files=$inventory} |
        ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $receipt -Encoding utf8
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
    Move-Item -LiteralPath $source -Destination $target
    foreach ($file in $inventory) {
        $p = Join-Path $target $file.relative
        if ((Get-Item -LiteralPath $p).Length -ne $file.bytes -or
            (Get-InkTagGoodixFileSHA256 -Path $p) -ne $file.sha256) { throw 'Post-move backup verification failed' }
    }
    return [pscustomobject]@{SessionRoot=$target;Record=$record}
}
