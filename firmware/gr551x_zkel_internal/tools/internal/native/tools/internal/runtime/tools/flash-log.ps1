# Console tail only. The vendor's original log and its result parser are unchanged.
function Show-InkTagLogTail {
    param([string[]]$Paths, [hashtable]$Offsets)
    foreach ($path in $Paths) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $content = Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
        if ($null -eq $content) { continue }
        $offset = if ($Offsets.ContainsKey($path)) { [int]$Offsets[$path] } else { 0 }
        if ($offset -gt $content.Length) { $offset = 0 }
        if ($content.Length -gt $offset) {
            Write-Host -NoNewline $content.Substring($offset)
            $Offsets[$path] = $content.Length
        }
    }
}
