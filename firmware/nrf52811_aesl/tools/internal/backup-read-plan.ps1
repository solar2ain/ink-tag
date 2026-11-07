function Get-InkTagNrfBackupReadPlan {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$TargetDir, [switch]$ConfigOnly)

    $ranges = @(
        [pscustomobject]@{ Name = 'aesl22-nrf-flash.bin'; Address = 0x00000000; Bytes = 196608 },
        [pscustomobject]@{ Name = 'aesl22-nrf-ficr.bin'; Address = 0x10000000; Bytes = 4096 },
        [pscustomobject]@{ Name = 'aesl22-nrf-uicr.bin'; Address = 0x10001000; Bytes = 4096 }
    )
    if ($ConfigOnly) { $ranges = @($ranges | Select-Object -Skip 1) }
    $reads = if ($ConfigOnly) { 1 } else { 2 }
    $commands = @('connect', 'halt')
    $files = @()
    for ($read = 1; $read -le $reads; $read++) {
        $directory = if ($read -eq 1) { $TargetDir } else { Join-Path $TargetDir 'read-2' }
        foreach ($range in $ranges) {
            $file = Join-Path $directory $range.Name
            $commands += ('savebin "{0}" 0x{1:X8} 0x{2:X8}' -f $file, $range.Address, $range.Bytes)
            $files += [pscustomobject]@{ Path = $file; Bytes = $range.Bytes; Read = $read; Address = $range.Address }
        }
    }
    $commands += 'go'
    [pscustomobject]@{ Commands = $commands; Files = $files; CompleteReads = $reads; ExternalFlashIncluded = $false }
}
