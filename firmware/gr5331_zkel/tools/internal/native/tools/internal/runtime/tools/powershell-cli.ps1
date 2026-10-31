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
    Write-Host ('  powershell -NoProfile -ExecutionPolicy Bypass -File "{0}" [options]' -f $ScriptPath)
    Write-Host ""
    Write-Host "Options:"

    foreach ($parameter in $ast.ParamBlock.Parameters) {
        $name = $parameter.Name.VariablePath.UserPath
        if ($name -in @("Help", "RemainingArguments")) { continue }

        $typeName = if ($parameter.StaticType) { $parameter.StaticType.Name } else { "Object" }
        $valueHint = if ($typeName -eq "SwitchParameter") { "" } else { " <$typeName>" }
        $defaultHint = if ($parameter.DefaultValue) { " (default: $($parameter.DefaultValue.Extent.Text))" } else { "" }
        Write-Host ("  -{0}{1}{2}" -f $name, $valueHint, $defaultHint)
        foreach ($attribute in $parameter.Attributes) {
            if ($attribute.TypeName.Name -notin @('Parameter', 'ParameterAttribute')) { continue }
            foreach ($argument in $attribute.NamedArguments) {
                if ($argument.ArgumentName -eq 'HelpMessage' -and
                    $argument.Argument -is [System.Management.Automation.Language.StringConstantExpressionAst]) {
                    Write-Host ("      {0}" -f $argument.Argument.Value)
                }
            }
        }
    }

    Write-Host "  -Help, -h, --help"
    Write-Host "      Show this help without accessing a device."
    Write-Host "  -Verbose"
    Write-Host "      Show detailed backend/diagnostic output; raw logs remain complete."
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
