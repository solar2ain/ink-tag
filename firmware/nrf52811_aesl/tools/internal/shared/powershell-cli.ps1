function Show-InkTagCliHelp {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ScriptPath,
        [string]$Summary = "InkTag command-line tool"
    )

    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile(
        (Resolve-Path -LiteralPath $ScriptPath).Path,
        [ref]$tokens,
        [ref]$errors
    )

    Write-Host $Summary
    Write-Host ""
    Write-Host "Usage:"
    $launcher = if ($PSVersionTable.PSEdition -eq "Core") { "pwsh -NoProfile" } else { "powershell -NoProfile -ExecutionPolicy Bypass" }
    Write-Host ('  {0} -File "{1}" [options]' -f $launcher, $ScriptPath)
    Write-Host ""
    Write-Host "Options:"

    foreach ($parameter in $ast.ParamBlock.Parameters) {
        $name = $parameter.Name.VariablePath.UserPath
        if ($name -in @("Help", "RemainingArguments")) { continue }

        $typeName = if ($parameter.StaticType) { $parameter.StaticType.Name } else { "Object" }
        $valueHint = if ($typeName -eq "SwitchParameter") { "" } else { " <$typeName>" }
        $defaultHint = if ($parameter.DefaultValue) { " (default: $($parameter.DefaultValue.Extent.Text))" } else { "" }
        Write-Host ("  -{0}{1}{2}" -f $name, $valueHint, $defaultHint)
    }

    Write-Host "  -h, --help"
    if (@($ast.ParamBlock.Attributes | ForEach-Object { $_.Extent.Text }) -match 'CmdletBinding') {
        Write-Host "  -Verbose (show full vendor output; raw logs are always saved)"
    }
}

function Invoke-InkTagCliGuard {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ScriptPath,
        [bool]$HelpRequested,
        [AllowNull()]
        [object[]]$RemainingArguments,
        [string]$Summary = "InkTag command-line tool"
    )

    [object[]]$extra = @($RemainingArguments | Where-Object { $null -ne $_ })
    if ($HelpRequested -or ($extra -contains "--help")) {
        Show-InkTagCliHelp -ScriptPath $ScriptPath -Summary $Summary
        return 0
    }

    if ($extra.Length -gt 0) {
        [Console]::Error.WriteLine("Unknown argument(s): {0}" -f ($extra -join " "))
        Write-Host ""
        Show-InkTagCliHelp -ScriptPath $ScriptPath -Summary $Summary
        return 2
    }

    return $null
}

function Stop-InkTagCliUsage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ScriptPath,
        [Parameter(Mandatory = $true)]
        [string]$Message,
        [string]$Summary = "InkTag command-line tool"
    )

    [Console]::Error.WriteLine($Message)
    Write-Host ""
    Show-InkTagCliHelp -ScriptPath $ScriptPath -Summary $Summary
    return 2
}
