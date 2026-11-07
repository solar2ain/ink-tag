# Called only by an explicit setup.ps1 invocation. Offline and bundle-local.
param([string]$ToolsDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $ToolsDirectory
. (Join-Path $ToolsDirectory 'internal/full-bundle-guard.ps1')
Assert-InkTagCompleteBundleFiles -ToolsDirectory $ToolsDirectory -AllowMissingRuntime
$target=Join-Path $ToolsDirectory '.runtime/python'
$lock=Get-Content (Join-Path $ToolsDirectory 'full-dependencies.lock.json') -Raw|ConvertFrom-Json
if (Test-Path -LiteralPath $target) {
    & (Join-Path $target 'python.exe') -I -B (Join-Path $ToolsDirectory 'internal/full-runtime.py') check --tools $ToolsDirectory
    if($LASTEXITCODE -ne 0){throw 'Runtime is incomplete or changed. Re-extract the original bundle; no overwrite or download.'}
    exit 0
}
foreach($entry in $lock.archives) {
    $path=Join-Path $root ('dependencies/'+$entry.file)
    if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item $path).Length -ne $entry.bytes -or (Get-FileHash $path).Hash.ToLowerInvariant() -cne $entry.sha256){throw 'Pinned dependency archive missing/changed; re-extract the original bundle. No download.'}
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
New-Item -ItemType Directory -Path $target -Force|Out-Null
foreach($entry in $lock.archives) {
    $path=Join-Path $root ('dependencies/'+$entry.file)
    $output=if($entry.kind -eq 'python'){$target}else{Join-Path $target 'Lib/site-packages'}
    New-Item -ItemType Directory -Path $output -Force|Out-Null
    $archive=[IO.Compression.ZipFile]::OpenRead($path)
    try {foreach($member in $archive.Entries) {
        $candidate=[IO.Path]::GetFullPath((Join-Path $output $member.FullName))
        if(!$candidate.StartsWith([IO.Path]::GetFullPath($output)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Archive escapes runtime'}
    }}finally{$archive.Dispose()}
    [IO.Compression.ZipFile]::ExtractToDirectory($path,$output)
}
Copy-Item -LiteralPath (Join-Path $ToolsDirectory 'internal/python313._pth') -Destination (Join-Path $target 'python313._pth') -Force
& (Join-Path $target 'python.exe') -I -B (Join-Path $ToolsDirectory 'internal/full-runtime.py') check --tools $ToolsDirectory
if($LASTEXITCODE -ne 0){throw 'Portable runtime import/integrity failed; partial data retained.'}
