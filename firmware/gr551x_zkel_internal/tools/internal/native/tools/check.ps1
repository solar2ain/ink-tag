[CmdletBinding()]
param([switch]$PythonOnly,[Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments)
$body=Join-Path $PSScriptRoot 'internal/standalone-entry.ps1'
if(!(Test-Path -LiteralPath $body)){$body=Join-Path $PSScriptRoot '../../common/tools/internal/standalone-entry.ps1'}
. $body
try {Invoke-InkTagGoodixEnvironment -EntryRoot $PSScriptRoot -Action check -Parameters $PSBoundParameters;exit $LASTEXITCODE}
catch {Write-Error $_.Exception.Message;exit 1}
