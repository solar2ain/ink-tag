[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required compatible signed complete-firmware ZIP; never an OTA-only package.')][string]$Firmware = '',
    [Parameter(HelpMessage='Required board TOML/YAML/JSON preset.')][string]$Config = '',
    [Parameter(HelpMessage='Optional unique MAC. Omit for a generic bin: supported App/Recovery initialize and save an address at first boot.')][string]$Mac = '',
    [Parameter(HelpMessage='Explicit MAC type: Public or StaticRandom. Generic bins always initialize StaticRandom.')][ValidateSet('Public', 'StaticRandom')][string]$AddressType = 'Public',
    [Parameter(HelpMessage='Required new .bin path. Creates a 512-KiB image and companion .json; refuses overwrite.')][string]$Output = '',
    [Parameter(HelpMessage='Python executable with the tools setup dependencies installed.')][string]$Python = 'python',
    [Parameter(HelpMessage='Validate and print the offline image report without creating output files.')][switch]$DryRun,
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
        -RemainingArguments $RemainingArguments -Summary 'Generate a TLSR 512-KiB Free bin offline for manual BDT writing; replaces original Factory/calibration and lower user data when burned; no device access'
    if ($null -ne $guard) { exit $guard }
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
