[CmdletBinding()]
param(
    [ValidateSet('DA14585')][string]$Chip='DA14585',
    [string]$Firmware='', [string]$Config='', [string]$PythonExe='python',
    [ValidateRange(1,300)][int]$OperationTimeoutSeconds=60,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'internal/context.ps1')
$context=Get-InkTagDaToolContext -ToolsRoot $PSScriptRoot
. (Join-Path $PSScriptRoot 'internal/host-common.ps1')
. (Resolve-InkTagDaToolFile $context 'tools/powershell-cli.ps1')
$cli=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'DA14585 file-only Full template/signature/config validation. No helper, vendor process, probe, target or programming. This does not qualify a writer.'
if($null -ne $cli){exit $cli}
if(!$Firmware -or !$Config){throw 'Both -Firmware <Full bin> and -Config <board TOML> are required.'}
$python=Resolve-InkTagDaPython $context $PythonExe
Assert-InkTagDaToolEnvironment $context $python -OperationTimeoutSeconds $OperationTimeoutSeconds
$firmwareHash=(Get-FileHash -LiteralPath $Firmware -Algorithm SHA256).Hash
$configHash=(Get-FileHash -LiteralPath $Config -Algorithm SHA256).Hash
$directory=New-InkTagDaToolLogDirectory $context
$auth=Invoke-InkTagDaLoggedProcess -Exe $python -Arguments @('-I','-B',(Resolve-InkTagDaToolFile $context 'tools/hardware/da14585-image-verify.py'),$Firmware,'--kind','full-template') -Log (Join-Path $directory 'native-package.log') -OperationTimeoutSeconds $OperationTimeoutSeconds -Label 'Validate signed native template' -Detailed:([bool]$PSBoundParameters['Verbose'])
if($auth.ExitCode -ne 0){throw 'Native package/signature validation failed; no target accessed.'}
$receipt=$auth.Text|ConvertFrom-Json
$configResult=Invoke-InkTagDaLoggedProcess -Exe $python -Arguments @('-I','-B',(Resolve-InkTagDaToolFile $context 'tools/hardware/da14585-board-config.py'),'--chip',$Chip,'--config',$Config,'--firmware',$Firmware) -Log (Join-Path $directory 'board-config.log') -OperationTimeoutSeconds $OperationTimeoutSeconds -Label 'Validate board/config pairing' -Detailed:([bool]$PSBoundParameters['Verbose'])
if($configResult.ExitCode -ne 0){throw 'Board/config validation failed; no target accessed.'}
$plan=$configResult.Text|ConvertFrom-Json
if($receipt.appPublisherVerified -isnot [bool] -or !$receipt.appPublisherVerified -or $receipt.fullBytes -ne 262144 -or $receipt.layout -ne 'DAV1' -or $receipt.fullSha256 -ne $firmwareHash -or $plan.base.sha256 -ne $firmwareHash -or $plan.firmware_family -ne 'da14585_aesl' -or (Get-FileHash -LiteralPath $Firmware).Hash -ne $firmwareHash -or (Get-FileHash -LiteralPath $Config).Hash -ne $configHash){throw 'Incomplete/changed package/config proof; no target accessed.'}
Write-InkTagDaLog INFO 'File-only validation passed. Physical writer/helper/fresh same-unit capture are not qualified by this command.'
[ordered]@{packageValidated=$true;deviceAccess=$false;writerReady=$false;
    actualEpoch=$receipt.compatibilityEpoch;nativeLayout=$receipt.layout;board=$plan.board_marking;
    firmwareSha256=$firmwareHash.ToLowerInvariant();configSha256=$configHash.ToLowerInvariant();
    cleanupProfile=$receipt.diagnosticCleanupProfile;cleanupScope=$receipt.cleanupScope;
    evidence=$directory}|ConvertTo-Json -Depth 8 -Compress
