[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required compatible Base CRC-only or publisher-signed Pro complete-firmware ZIP; never an OTA-only package.')][string]$Firmware = '',
    [Parameter(HelpMessage='Required board TOML/YAML/JSON preset.')][string]$Config = '',
    [Parameter(HelpMessage='Optional unique MAC. Omitting it requires the actual App/Recovery pair to support automatic persistent addresses.')][string]$Mac = '',
    [Parameter(HelpMessage='Explicit MAC type: Public or StaticRandom. Generic bins always initialize StaticRandom.')][ValidateSet('Public', 'StaticRandom')][string]$AddressType = 'Public',
    [Parameter(HelpMessage='Required new .bin path. Creates a512-KiB initialization image and companion .json; upper Flash is outside the image. Refuses overwrite.')][string]$Output = '',
    [Parameter(HelpMessage='Python executable with the tools setup dependencies installed.')][string]$Python = 'python',
    [Parameter(HelpMessage='Validate and print the offline image report without creating output files.')][switch]$DryRun,
    [Parameter(HelpMessage='Require explicit inputs; never open dialogs or prompt for missing values.')][switch]$NonInteractive,
    [Parameter(HelpMessage='Show options without device access.')][Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
$runtimeTools=$PSScriptRoot
$releaseRuntimeTools=Join-Path $PSScriptRoot 'internal/native/firmware/ports/telink/tlsr8359/tools'
if (Test-Path -LiteralPath (Join-Path $releaseRuntimeTools 'internal/terminal-log.ps1') -PathType Leaf) {
    $runtimeTools=$releaseRuntimeTools
}
. (Join-Path $runtimeTools 'internal/terminal-log.ps1')
$terminalColour = Get-InkTagTerminalColour
try {
    Set-InkTagTerminalEnglish
    $ErrorActionPreference = 'Stop'
    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $runtimeTools '../../../../../')).Path
    . (Join-Path $repoRoot 'tools/powershell-cli.ps1')
    $guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
        -RemainingArguments $RemainingArguments -Summary 'Generate a TLSR initialization bin offline for manual BDT writing; replaces original Factory/calibration and user data inside its extent; no device access'
    if ($null -ne $guard) { exit $guard }
    . (Join-Path $runtimeTools 'internal/file-selection.ps1')
    if (!$DryRun -and !$NonInteractive) {
        if ([string]::IsNullOrWhiteSpace($Config)) {
            $Config = Select-InkTagBoardConfig -Directory $PSScriptRoot
        }
        if ([string]::IsNullOrWhiteSpace($Firmware)) {
            $Firmware = Select-InkTagToolFile -Title 'Select InkTag complete firmware ZIP' -Filter 'InkTag complete firmware (*.zip)|*.zip' -Directory $PSScriptRoot
        }
        if ([string]::IsNullOrWhiteSpace($Output)) {
            $Output = Select-InkTagToolFile -Title 'Save new initialization BIN' -Filter 'Initialization BIN (*.bin)|*.bin' -Directory $PSScriptRoot -Save
        }
    }
    if (!$Firmware -or !$Config -or !$Output) { throw '-Firmware, -Config and -Output are required. No device accessed.' }
    $pythonCommand = Get-Command -Name $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1
    $canonicalType = if ($AddressType -eq 'Public') { 'Public' } else { 'StaticRandom' }
    $options = @('--firmware', $Firmware, '--config', $Config, '--address-type', $canonicalType, '--output', $Output)
    if ($Mac) { $options += @('--mac', $Mac) }
    if ($DryRun) { $options += '--dry-run' }
    $hardwareRoot=Join-Path $repoRoot 'tools/hardware'
    $exporter=Join-Path $hardwareRoot 'tlsr_generate_bin.py'
    if (!(Test-Path -LiteralPath $exporter -PathType Leaf)) {
        # The released0.2.1 native runtime remains byte-identical. A two-file
        # additive update keeps the new exporter outside its frozen manifest.
        $exporter=Join-Path $PSScriptRoot 'internal/tlsr_generate_bin.py'
    }
    if (!(Test-Path -LiteralPath $exporter -PathType Leaf)) { throw 'Missing matching internal/tlsr_generate_bin.py; install both exporter files. No device accessed.' }
    $bootstrap="import runpy,sys; sys.path.insert(0,sys.argv.pop(1)); runpy.run_path(sys.argv.pop(1),run_name='__main__')"
    & $pythonCommand.Source -c $bootstrap $hardwareRoot $exporter @options
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} catch {
    Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagTerminalColour $terminalColour }
