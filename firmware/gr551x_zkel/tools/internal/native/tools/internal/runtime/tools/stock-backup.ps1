# Shared host-only backup paths and receipts. No debugger/device operations.
function Assert-InkTagBackupPath([string]$Path) {
    if (($Path -split '[\\/]') -contains '..') { throw 'Backup path must not contain parent traversal' }
    $item = [IO.Path]::GetFullPath($Path)
    while ($item) {
        if ((Test-Path -LiteralPath $item) -and ((Get-Item -LiteralPath $item -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Backup path must not follow a link: $item"
        }
        $item = [IO.Path]::GetDirectoryName($item)
    }
}

function Get-InkTagBackupRoot([string]$RepoRoot) {
    Assert-InkTagBackupPath $RepoRoot
    $root = [IO.Path]::GetFullPath($RepoRoot)
    if (!(Test-Path -LiteralPath $root -PathType Container)) { throw 'Backup installation root is missing' }
    $marker = Join-Path $root '.git'
    if (!(Test-Path -LiteralPath $marker)) { return $root }
    Assert-InkTagBackupPath $marker
    if (Test-Path -LiteralPath $marker -PathType Container) {
        if (!(Test-Path -LiteralPath (Join-Path $marker 'HEAD') -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $marker 'config') -PathType Leaf)) { throw 'Invalid source Git directory; no installation fallback' }
        return $root
    }
    $pointer = (Get-Content -LiteralPath $marker -Raw).Trim()
    if ($pointer -notmatch '^gitdir: (.+)$') { throw 'Invalid Git worktree pointer; no installation fallback' }
    $admin = if ([IO.Path]::IsPathRooted($Matches[1])) { $Matches[1] } else { Join-Path $root $Matches[1] }
    # Git's own commondir commonly contains ../..; normalize that trusted
    # administration pointer, then check every resulting ancestor for links.
    $admin = [IO.Path]::GetFullPath($admin)
    Assert-InkTagBackupPath $admin
    $commonFile = Join-Path $admin 'commondir'
    if (!(Test-Path -LiteralPath $commonFile -PathType Leaf)) { throw 'Missing Git common directory; no installation fallback' }
    Assert-InkTagBackupPath $commonFile
    $common = (Get-Content -LiteralPath $commonFile -Raw).Trim()
    if (!$common -or $common -match '[\r\n]') { throw 'Ambiguous Git common directory' }
    $common = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($common)) { $common } else { Join-Path $admin $common }))
    Assert-InkTagBackupPath $common
    if (!(Test-Path -LiteralPath $common -PathType Container) -or (Split-Path -Leaf $common) -ne '.git' -or
        !(Test-Path -LiteralPath (Join-Path $common 'HEAD') -PathType Leaf) -or !(Test-Path -LiteralPath (Join-Path $common 'config') -PathType Leaf)) { throw 'Invalid Git common directory' }
    return Split-Path -Parent $common
}

function ConvertTo-InkTagBackupLabel([string]$Value) {
    if (!$Value -or $Value.Length -gt 64 -or $Value -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*$' -or $Value.EndsWith('.') -or
        $Value -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'Chip, Board and BoardNote must be safe directory labels, not paths' }
    return $Value
}

function New-InkTagStockBackupSession {
    param([string]$RepoRoot, [string]$Chip, [string]$Board = 'UNIDENTIFIED-PCB', [string]$BoardNote = '', [string]$OutputDir = '')
    $Chip = ConvertTo-InkTagBackupLabel $Chip
    $Board = ConvertTo-InkTagBackupLabel $Board
    if ($BoardNote.Length -gt 256 -or $BoardNote -match '[\x00-\x1f\x7f]') { throw 'BoardNote must be at most 256 characters without control characters' }
    $root = Get-InkTagBackupRoot $RepoRoot
    $token = [DateTimeOffset]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $dir = if ($OutputDir) { [IO.Path]::GetFullPath($OutputDir) } else { Join-Path $root ".local/backup-archive/$Chip/pending/$token" }
    Assert-InkTagBackupPath $dir
    $segments = $dir -split '[\\/]'
    if ($segments -contains 'backups' -or $segments -contains 'factory_backups') { throw 'Unverified capture must start outside factory-only backup directories' }
    if (Test-Path -LiteralPath $dir) { throw "Backup session already exists; never overwrite: $dir" }
    New-Item -ItemType Directory -Path $dir -ErrorAction Stop | Out-Null
    return [pscustomobject]@{Root=$root;Directory=$dir;Chip=$Chip;Board=$Board;BoardNote=$BoardNote;Token=$token;ExplicitOutput=[bool]$OutputDir}
}

function Get-InkTagBackupSHA([string]$Path) {
    Assert-InkTagBackupPath $Path
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}

function Compare-InkTagBackupReads {
    param([string]$First, [string]$Second, [long]$ExpectedBytes)
    if ($ExpectedBytes -le 0 -or [IO.Path]::GetFullPath($First) -eq [IO.Path]::GetFullPath($Second)) { throw 'Two separate nonempty read files are required' }
    foreach ($file in @($First, $Second)) {
        Assert-InkTagBackupPath $file
        if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -ne $ExpectedBytes) {
            throw "Incomplete backup read: $file; both captures and failures are retained"
        }
    }
    $sha = Get-InkTagBackupSHA $First
    if ($sha -ne (Get-InkTagBackupSHA $Second)) { throw 'Independent backup reads differ; preserve both, do not flash' }
    return $sha
}

function Get-InkTagBackupFiles([string]$Directory) {
    Assert-InkTagBackupPath $Directory
    foreach ($entry in Get-ChildItem -LiteralPath $Directory -Force) {
        Assert-InkTagBackupPath $entry.FullName
        if ($entry.PSIsContainer) { Get-InkTagBackupFiles $entry.FullName } else { $entry }
    }
}

function Complete-InkTagStockBackupSession {
    param($Session, [string]$Classification, [object[]]$ReadPairs, [object]$Identity, [object]$Ranges, [bool]$OriginalQualified = $false)
    if ($Classification -notin @('stock','inktag-current','unknown','invalid-capture')) { throw 'Invalid capture classification' }
    if (($Classification -eq 'stock') -ne $OriginalQualified) { throw 'Stock classification requires separate source qualification, not just matching reads' }
    $sessionPrefix = [IO.Path]::GetFullPath($Session.Directory).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $pairs = @($ReadPairs | ForEach-Object {
        foreach ($file in @($_.First,$_.Second)) {
            if (![IO.Path]::GetFullPath($file).StartsWith($sessionPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Read pair is outside its independent backup session' }
        }
        [pscustomobject]@{role=(ConvertTo-InkTagBackupLabel $_.Role).ToLowerInvariant();first=[IO.Path]::GetFullPath($_.First).Substring($sessionPrefix.Length);
            second=[IO.Path]::GetFullPath($_.Second).Substring($sessionPrefix.Length);bytes=$_.Bytes;sha256=(Compare-InkTagBackupReads $_.First $_.Second $_.Bytes)}
    })
    if (!$pairs.Count) { throw 'No independently verified read pairs' }
    if (@($pairs.role | Select-Object -Unique).Count -ne $pairs.Count) { throw 'Duplicate read role; no files published' }
    $receipt = Join-Path $Session.Directory 'backup.json'
    if (Test-Path -LiteralPath $receipt) { throw 'Backup receipt already exists' }
    $target = $Session.Directory
    if (!$Session.ExplicitOutput) {
        $part = if ($OriginalQualified) { "factory_backups/$($Session.Chip)/$($Session.Board)" } else { ".local/backup-archive/$($Session.Chip)/$Classification" }
        $target = Join-Path $Session.Root ($part + '/' + $Session.Token)
    }
    Assert-InkTagBackupPath $target
    if ($target -ne $Session.Directory -and (Test-Path -LiteralPath $target)) { throw 'Archive destination collision' }
    if ($OriginalQualified -and $target -ne $Session.Directory) {
        # Factory Git contains only explicitly verified payloads and receipt.
        # Raw reads/logs/commands remain at the independent local session.
        $publishedPairs=@()
        $locks=@()
        try {
            foreach ($pair in $pairs) {
                foreach ($relative in @($pair.first,$pair.second)) {
                    $source=Join-Path $Session.Directory $relative
                    $locks+=@([IO.File]::Open($source,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
                }
            }
            New-Item -ItemType Directory -Path $target -ErrorAction Stop | Out-Null
            foreach ($pair in $pairs) {
                $sourceFirst=Join-Path $Session.Directory $pair.first
                $sourceSecond=Join-Path $Session.Directory $pair.second
                if ((Compare-InkTagBackupReads $sourceFirst $sourceSecond $pair.bytes) -ne $pair.sha256) { throw 'Read pair changed before publication; retain files, do not replay' }
                $first='read-1-'+$pair.role+'.bin'
                $second='read-2-'+$pair.role+'.bin'
                $canonical='capture-'+$pair.role+'.bin'
                foreach ($copy in @(@($sourceFirst,$first),@($sourceSecond,$second),@($sourceFirst,$canonical))) {
                    $destination=Join-Path $target $copy[1]
                    Assert-InkTagBackupPath $destination
                    [IO.File]::Copy($copy[0],$destination,$false)
                    if ((Get-Item -LiteralPath $destination).Length -ne $pair.bytes -or (Get-InkTagBackupSHA $destination) -ne $pair.sha256) { throw 'Published payload mismatch; retain all files, do not replay' }
                }
                $publishedPairs+=@([pscustomobject]@{role=$pair.role;first=$first;second=$second;canonical=$canonical;bytes=$pair.bytes;sha256=$pair.sha256})
            }
            $record=[pscustomobject]@{schema='inktag-stock-backup-v1';chip=$Session.Chip;board=$Session.Board;boardNote=$Session.BoardNote;
                verification='matched-double-read';classification=$Classification;originalQualified=$OriginalQualified;
                identity=$Identity;ranges=$Ranges;readPairs=$publishedPairs;sourceReadPairs=$pairs;
                capturedUtc=[DateTimeOffset]::UtcNow.ToString('o');originalDirectory=$Session.Directory;directory=$target;
                rawSessionRetained=$true}
            # Create completion receipts last: partial copy never becomes success.
            $record | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $target 'backup.json') -Encoding UTF8 -ErrorAction Stop
            $record | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $receipt -Encoding UTF8 -ErrorAction Stop
        } finally { foreach ($stream in $locks) { $stream.Dispose() } }
        return $target
    }
    [pscustomobject]@{schema='inktag-stock-backup-v1';chip=$Session.Chip;board=$Session.Board;boardNote=$Session.BoardNote;
        verification='matched-double-read';classification=$Classification;originalQualified=$OriginalQualified;
        identity=$Identity;ranges=$Ranges;readPairs=$pairs;capturedUtc=[DateTimeOffset]::UtcNow.ToString('o');
        originalDirectory=$Session.Directory;directory=$target} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $receipt -Encoding UTF8 -ErrorAction Stop
    if ($target -ne $Session.Directory) {
        $files = @(Get-InkTagBackupFiles $Session.Directory | ForEach-Object {
            Assert-InkTagBackupPath $_.FullName
            [pscustomobject]@{relative=$_.FullName.Substring($Session.Directory.Length+1);bytes=$_.Length;sha=(Get-InkTagBackupSHA $_.FullName)}
        })
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Move-Item -LiteralPath $Session.Directory -Destination $target
        foreach ($f in $files) { $p=Join-Path $target $f.relative; if ((Get-Item -LiteralPath $p).Length -ne $f.bytes -or (Get-InkTagBackupSHA $p) -ne $f.sha) { throw 'Moved backup verification failed; no replay' } }
    }
    return $target
}
