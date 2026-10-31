# Native tools may include J-Link's informational ExitOnError banner.
# Remove only that exact full line from validation; keep original logs intact.
function Get-InkTagGoodixCheckedNativeOutput {
    param([string]$Output)
    return (($Output -split '\r?\n' | Where-Object {
        $_ -cne 'J-Link Commander will now exit on Error'
    }) -join "`n")
}
