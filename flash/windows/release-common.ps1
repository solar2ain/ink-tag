Set-StrictMode -Version Latest

function Get-InkTagReleaseRoot {
    return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
}

function Get-InkTagFlashRoot {
    return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
}

function Get-InkTagReleaseManifest {
    $paths = @(
        (Join-Path (Get-InkTagFlashRoot) "flash-manifest.json"),
        (Join-Path (Get-InkTagReleaseRoot) "firmware\manifest.json")
    )
    $path = @($paths | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1)
    if ($path.Count -ne 1) {
        throw "Release manifest not found. Keep flash-manifest.json beside the extracted windows/ and runtime/ directories."
    }
    return Get-Content -LiteralPath $path[0] -Raw | ConvertFrom-Json
}

function Resolve-InkTagFullArtifact {
    param(
        [Parameter(Mandatory = $true)][string]$Board,
        [string]$Firmware = ""
    )

    $manifest = Get-InkTagReleaseManifest
    $boardEntry = @($manifest.boards | Where-Object { $_.boardId -eq $Board })
    if ($boardEntry.Count -ne 1) {
        throw "Board '$Board' is not present exactly once in firmware/manifest.json."
    }
    $artifact = @($boardEntry[0].artifacts | Where-Object { $_.type -eq "full" })
    if ($artifact.Count -ne 1) {
        throw "This release does not contain one Full artifact for '$Board'."
    }

    if ($Firmware -eq "") {
        $nearScript = Join-Path $PSScriptRoot $artifact[0].name
        $nearFlashRoot = Join-Path (Get-InkTagFlashRoot) $artifact[0].name
        $nearReleaseRoot = Join-Path (Get-InkTagReleaseRoot) $artifact[0].name
        $local = @($nearScript, $nearFlashRoot, $nearReleaseRoot) | Where-Object {
            Test-Path -LiteralPath $_ -PathType Leaf
        }
        if ($local.Count -eq 1) {
            $Firmware = $local[0]
        } else {
            throw "Full artifact not found locally. Download '$($artifact[0].name)' from $($artifact[0].downloadUrl), then pass -Firmware <path>."
        }
    }
    if (!(Test-Path -LiteralPath $Firmware -PathType Leaf)) {
        throw "Full artifact not found: $Firmware"
    }
    $resolved = (Resolve-Path -LiteralPath $Firmware).Path
    $actual = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
    $expected = ([string]$artifact[0].sha256).ToLowerInvariant()
    if ($actual -ne $expected) {
        throw "SHA-256 mismatch for $resolved. Expected $expected, got $actual."
    }
    if ((Get-Item -LiteralPath $resolved).Length -ne [int64]$artifact[0].bytes) {
        throw "File length does not match firmware/manifest.json: $resolved"
    }
    return [pscustomobject]@{
        Board = $boardEntry[0]
        Artifact = $artifact[0]
        Path = $resolved
    }
}

function Assert-InkTagStockBackup {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int64[]]$AllowedSizes
    )

    if (!(Test-Path -LiteralPath $Path -PathType Leaf) -and
        !(Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Stock backup not found: $Path"
    }
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $length = (Get-Item -LiteralPath $Path).Length
        if ($length -notin $AllowedSizes) {
            throw "Stock backup has unexpected length $length bytes: $Path"
        }
    }
}

function Show-InkTagFlashHelp {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Usage,
        [Parameter(Mandatory = $true)][string[]]$Notes
    )
    Write-Host $Title
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  $Usage"
    Write-Host ""
    foreach ($note in $Notes) { Write-Host $note }
}
