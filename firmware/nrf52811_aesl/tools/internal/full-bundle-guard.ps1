# Hash the portable interpreter and all executable inputs before starting Python.
# This is an integrity check of a trusted selected bundle, not publisher signing.
function Assert-InkTagCompleteBundleFiles {
    param([string]$ToolsDirectory,[switch]$AllowMissingRuntime)
    $root=[IO.Path]::GetFullPath((Split-Path -Parent $ToolsDirectory))
    function Assert-Inventory([string]$Base,$Rows,[string[]]$Extra=@(),[string]$SkipDirectory='') {
        $basePath=[IO.Path]::GetFullPath($Base)
        $expected=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $count=0
        foreach($entry in $Rows.PSObject.Properties) {
            $name=[string]$entry.Name;$record=$entry.Value;$count++
            if($count -gt 6000 -or !$name -or $name.Contains('\') -or $name.Contains(':') -or
                [IO.Path]::IsPathRooted($name) -or @($name.Split('/')|Where-Object {$_ -in @('','.','..')}).Count -or
                !$expected.Add($name) -or $record.sha256 -cnotmatch '^[0-9a-f]{64}$') {throw 'Malformed bundle inventory'}
            $path=[IO.Path]::GetFullPath((Join-Path $basePath $name))
            if(!$path.StartsWith($basePath+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
                !(Test-Path -LiteralPath $path -PathType Leaf)){throw ('Missing bundle file: '+$name)}
            $cursor=$path
            while($cursor -and $cursor.Length -ge $basePath.Length) {
                if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked bundle path refused'}
                $cursor=[IO.Path]::GetDirectoryName($cursor)
            }
            if((Get-Item -LiteralPath $path).Length -ne $record.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.sha256){throw ('Bundle byte binding mismatch: '+$name)}
        }
        foreach($name in $Extra){$null=$expected.Add($name)}
        $actual=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($basePath)
        while($pending.Count) {
            $directory=$pending.Pop()
            foreach($item in (Get-Item -LiteralPath $directory -Force).GetFileSystemInfos()) {
                if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked bundle content refused'}
                $relative=$item.FullName.Substring($basePath.Length+1).Replace('\','/')
                if($item.Attributes -band [IO.FileAttributes]::Directory) {
                    if($relative -ne $SkipDirectory){$pending.Push($item.FullName)}
                } else {
                    if(!$expected.Contains($relative)){throw ('Unexpected bundle file: '+$relative)}
                    $null=$actual.Add($relative)
                }
            }
        }
        if(!$actual.SetEquals($expected)){throw 'Bundle inventory is incomplete'}
    }
    $packageInventory=Join-Path $root 'package-files.json'
    if(!(Test-Path -LiteralPath $packageInventory -PathType Leaf)){throw 'Bundle inventory missing'}
    $rows=(Get-Content -LiteralPath $packageInventory -Raw -Encoding UTF8|ConvertFrom-Json).files
    Assert-Inventory $root $rows @('package-files.json') 'tools/.runtime'
    $runtime=Join-Path $ToolsDirectory '.runtime'
    if(!(Test-Path -LiteralPath $runtime)) {
        if($AllowMissingRuntime){return}
        throw 'Portable Python missing; run tools/setup.ps1'
    }
    $runtimeRows=(Get-Content -LiteralPath (Join-Path $ToolsDirectory 'internal/runtime-files.json') -Raw -Encoding UTF8|ConvertFrom-Json).files
    Assert-Inventory $runtime $runtimeRows
}
