param(
    [ValidateSet('tlsr8359','hs9118','tlsr825x')][string]$Chip,
    [string]$Port,
    [string]$DeviceLabel = '',
    [string]$PcbSilkscreen,
    [string]$RequestFile,
    [string]$OutputDir,
    [string]$ToolPath = '',
    [string]$Python = 'python',
    [int]$Baud = 921600,
    [int]$ActivationBaud = 0,
    [int]$ActivationMs = 20000,
    [int]$ClockMHz = 0,
    [switch]$AllowUnqualifiedBaud,
    [switch]$RetryDetect,
    [int]$DetectTimeoutSeconds = 0,
    [int]$DetectIntervalSeconds = 1
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module Microsoft.PowerShell.Utility -ErrorAction Stop
. (Join-Path $PSScriptRoot 'tlsr8359-uart-sws-common.ps1')
$request = Get-Content -LiteralPath $RequestFile -Raw | ConvertFrom-Json
if ($request.schema -ne 'inktag-tlsr-backup-verify-request-v1' -or
    $request.chip -ne $Chip -or $request.port -ne $Port -or
    $request.pcb -ne $PcbSilkscreen -or $request.boardNote -ne $DeviceLabel -or
    $request.address -ne 0 -or $request.firstReadReplayed -ne $false -or $request.flashWrites -ne $false) {
    throw 'Verification request identity/operation mismatch; no device access.'
}
$geometry = @{ EB6013 = 524288; EB6014 = 1048576; '856013' = 524288 }
if (!$geometry.ContainsKey([string]$request.flashJedec) -or
    $request.sizeBytes -ne $geometry[[string]$request.flashJedec]) {
    throw 'Verification request is not a complete admitted JEDEC boundary.'
}
if ($Baud -eq 0) { $Baud = 921600 }
if ($Baud -lt 340000 -or ($ActivationBaud -ne 0 -and $ActivationBaud -lt 340000) -or
    $ActivationMs -le 0 -or $ClockMHz -lt 0 -or $ClockMHz -gt 192 -or
    ($Baud -notin @(460800,921600) -and !$AllowUnqualifiedBaud)) { throw 'Invalid/custom verification transport settings; no device access.' }
foreach ($item in @($request.primary) + @($request.evidence)) {
    if ((Get-Item -LiteralPath $item.path).Length -ne $item.bytes -or
        (Get-FileHash -LiteralPath $item.path -Algorithm SHA256).Hash -ne $item.sha256) {
        throw 'Original capture/evidence changed; no device access.'
    }
}
if ($request.primary.bytes -ne $request.sizeBytes) { throw 'Primary capture is incomplete.' }
if (Test-Path -LiteralPath $OutputDir) { throw 'Verification output already exists; no overwrite/replay.' }
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$verify = Join-Path $OutputDir 'tlsr8359-flash-verify.bin'
$readLog = Join-Path $OutputDir 'read-2.log'
$report = [ordered]@{
    schema = 'inktag-tlsr-independent-verification-v1'
    status = 'STARTED'; startedUtc = [DateTimeOffset]::UtcNow.ToString('o'); finishedUtc = $null
    chip = $Chip; pcbSilkscreen = $PcbSilkscreen; boardNote = $DeviceLabel; port = $Port
    originalUnit = $request.originalUnit; primary = $request.primary
    verificationPath = $verify; verificationBytes = 0; verificationSha256 = $null
    requestSha256 = (Get-FileHash -LiteralPath $RequestFile -Algorithm SHA256).Hash
    firstReadReplayed = $false; flashWrites = $false; error = $null
    qualification = $request.qualification
    transport = @{baud=$Baud;activationBaud=$ActivationBaud;activationMs=$ActivationMs;clockMHz=$ClockMHz;unqualifiedBaud=($Baud -ne 460800)}
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDir 'started.json') -Encoding UTF8
try {
    Write-Host 'BACKUP READ 2/2: independent verification of the preserved first read; no primary replay.'
    $options = @{
        Operation = 'read'; Port = $Port; Address = 0; SizeBytes = [uint32]$request.sizeBytes
        Baud = $Baud; ActivationBaud = $ActivationBaud; ActivationMs = $ActivationMs; ClockMHz = $ClockMHz; Run = $true; ReportFlashJedec = $true
        ExpectedFlashJedec = [string]$request.flashJedec; ExpectedReadFile = [string]$request.primary.path
        File = $verify; ToolPath = $ToolPath; Python = $Python
    }
    if ($RetryDetect) {
        $options.RetryDetect = $true; $options.DetectTimeoutSeconds = $DetectTimeoutSeconds
        $options.DetectIntervalSeconds = $DetectIntervalSeconds
    }
    & (Join-Path $PSScriptRoot 'invoke-tlsr8359-uart-sws.ps1') @options 2>&1 | Tee-Object -FilePath $readLog
    if ((Get-Item -LiteralPath $verify).Length -ne $request.sizeBytes) { throw 'Second read length mismatch.' }
    $report.verificationSha256 = (Get-FileHash -LiteralPath $verify -Algorithm SHA256).Hash
    if ($report.verificationSha256 -ne $request.primary.sha256 -or
        (Get-FileHash -LiteralPath $request.primary.path -Algorithm SHA256).Hash -ne $request.primary.sha256) {
        throw 'Independent captures differ/primary changed; retain all evidence, do not flash.'
    }
    $repoRoot = Get-Tlsr8359RepoRoot
    & (Resolve-TlsrPython -RequestedPython $Python) (Join-Path $repoRoot 'tools/hardware/analyze-tlsr8359-backup.py') `
        $request.primary.path --output (Join-Path $OutputDir 'analysis.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Offline structural analysis failed.' }
    $report.status = 'MATCHED-INDEPENDENT-SECOND-READ'
    Write-Host 'SUCCESS: complete independent second read matches the preserved first capture.'
} catch {
    $report.status = 'FAILED-PRESERVED-NO-AUTOMATIC-REPLAY'
    $report.error = $_.Exception.Message
    Write-Host ('ERROR: ' + $report.error)
} finally {
    $report.finishedUtc = [DateTimeOffset]::UtcNow.ToString('o')
    if (Test-Path -LiteralPath $verify) { $report.verificationBytes = (Get-Item -LiteralPath $verify).Length }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDir 'backup.json') -Encoding UTF8
}
if ($report.status -ne 'MATCHED-INDEPENDENT-SECOND-READ') { exit 1 }
