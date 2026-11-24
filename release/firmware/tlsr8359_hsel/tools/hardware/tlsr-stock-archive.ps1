# TLSR evidence adapter only. The shared stock-backup.ps1 owns publication.
# No serial/debugger/BLE access and no read replay.
function Complete-TlsrStockArchive {
    param($Session, [switch]$FactorySourceConfirmed, [string]$Python = 'python')
    $results = @(Get-ChildItem -LiteralPath $Session.Directory -Filter result.json -Recurse -File)
    if ($results.Count -ne 1) { throw 'Expected one exited TLSR capture in this raw session.' }
    $unit = $results[0].Directory.FullName
    $result = Get-Content -LiteralPath $results[0].FullName -Raw | ConvertFrom-Json
    if ($result.schema -ne 'inktag-flash-workflow-v1' -or $result.action -ne 'backup' -or
        $result.status -ne 'tool-success' -or !$result.ended_utc) { throw 'Raw backup did not complete; no factory publication.' }
    $Session.Board = ConvertTo-InkTagBackupLabel ([string]$result.pcb_silkscreen)
    if ([string]$result.board_note -cne [string]$Session.BoardNote) { throw 'BoardNote differs from the actual capture.' }
    $backup = Join-Path $unit 'backup'
    $second = Join-Path $backup 'tlsr8359-flash-verify.bin'
    $requestFile = Join-Path $unit 'verification-request.json'
    $size = 0; $jedec = ''; $primaryHash = ''; $original = $null
    $segmented = $false
    $resumeRequest = Join-Path $unit 'resume-request.json'
    if (Test-Path -LiteralPath $resumeRequest) {
        $validator = Join-Path $PSScriptRoot 'tlsr_backup_resume.py'
        $validatedText = & $Python $validator --validate-complete $unit
        if ($LASTEXITCODE -ne 0) { throw 'Segmented resume evidence gate failed; no publication.' }
        $validated = $validatedText | ConvertFrom-Json
        if ($validated.chip -ne $Session.Chip -or $validated.pcb -ne $Session.Board -or
            $validated.boardNote -cne $Session.BoardNote -or $validated.segmented -ne $true) {
            throw 'Segmented resume identity mismatch; no publication.'
        }
        $first = [string]$validated.first; $second = [string]$validated.second
        $size = [long]$validated.sizeBytes; $jedec = [string]$validated.flashJedec
        $primaryHash = [string]$validated.sha256; $segmented = $true
        $logFiles = @() # All segment logs/hashes/coverage checked by offline validator.
    } elseif (Test-Path -LiteralPath $requestFile) {
        $request = Get-Content -LiteralPath $requestFile -Raw | ConvertFrom-Json
        $verified = Get-Content -LiteralPath (Join-Path $backup 'backup.json') -Raw | ConvertFrom-Json
        if ($request.schema -ne 'inktag-tlsr-backup-verify-request-v1' -or
            $request.chip -ne $Session.Chip -or $request.pcb -ne $Session.Board -or
            $request.boardNote -cne $Session.BoardNote -or
            $verified.status -ne 'MATCHED-INDEPENDENT-SECOND-READ' -or !$verified.finishedUtc -or
            $verified.error -or $verified.firstReadReplayed -ne $false -or $verified.flashWrites -ne $false -or
            $verified.requestSha256 -ne (Get-InkTagBackupSHA $requestFile)) { throw 'Verification-only receipt is not complete and bound.' }
        foreach ($item in @($request.primary) + @($request.evidence)) {
            Assert-InkTagBackupPath $item.path
            if ((Get-Item -LiteralPath $item.path).Length -ne $item.bytes -or
                (Get-InkTagBackupSHA $item.path) -ne $item.sha256) { throw 'Preserved first capture/evidence changed; no publication.' }
        }
        $size = [long]$request.sizeBytes; $jedec = [string]$request.flashJedec
        $primaryHash = [string]$request.primary.sha256
        $original = [string]$request.primary.path
        $first = Join-Path $Session.Directory 'preserved-first-read.bin'
        if (Test-Path -LiteralPath $first) { throw 'Preserved first-read copy already exists; no overwrite.' }
        [IO.File]::Copy($original, $first, $false)
        $logFiles = @((Join-Path $backup 'read-2.log'))
    } else {
        $info = @{}
        foreach ($line in Get-Content -LiteralPath (Join-Path $backup 'backup-info.txt')) {
            if ($line -match '^([^=]+)=(.*)$') {
                if ($info.ContainsKey($Matches[1])) { throw 'Duplicate backup metadata field.' }
                $info[$Matches[1]] = $Matches[2]
            }
        }
        if ($info.schema -ne 'inktag-tlsr8359-backup-v2' -or $info.verification -ne 'matched' -or
            $info.requested_chip_family -ne $Session.Chip -or $info.pcb_silkscreen -ne $Session.Board -or
            $info.start_address -ne '0x000000' -or $info.flash_sha256 -ne $info.verify_sha256) { throw 'Raw double-read metadata is not complete.' }
        $size = [long]$info.size_bytes; $jedec = [string]$info.flash_jedec
        $primaryHash = [string]$info.flash_sha256
        $first = Join-Path $backup 'tlsr8359-flash.bin'
        $logFiles = @((Join-Path $backup 'read-1.log'), (Join-Path $backup 'read-2.log'))
    }
    $geometry = @{ EB6013 = 524288; EB6014 = 1048576; '856013' = 524288 }
    if (!$geometry.ContainsKey($jedec) -or $size -ne $geometry[$jedec]) { throw 'No admitted complete JEDEC boundary.' }
    foreach ($file in $logFiles) {
        $raw = Get-Content -LiteralPath $file -Raw
        if ($raw -notmatch '(?m)^Done!\s*$' -or $raw -notmatch '(?:Reading|Read/verify) 100\.0%' -or
            $raw -notmatch ('Observed Flash JEDEC: ' + $jedec) -or $raw -match '(?im)^\s*(?:ERROR:|Error:|Traceback)') {
            throw 'Native read log did not complete successfully; no publication.'
        }
    }
    if ((Compare-InkTagBackupReads $first $second $size) -ne $primaryHash) { throw 'Final captures differ from their bound receipt.' }
    if (!(Test-Path -LiteralPath (Join-Path $backup 'analysis.txt') -PathType Leaf)) { throw 'Missing offline structural analysis.' }
    $classification = if ($FactorySourceConfirmed) { 'stock' } else { 'unknown' }
    # Unknown provenance stays at its original raw path; matching reads alone
    # cannot promote it or relocate references in its original receipts.
    if (!$FactorySourceConfirmed) { $Session.ExplicitOutput = $true }
    $identity = [pscustomobject]@{flashJedec=$jedec;sourceQualification=$(if ($FactorySourceConfirmed) {'human-declared-stock-only-backup-contract'} else {'not-established'});
        requested_chip=$Session.Chip;chip_marking=$(if ($Session.Chip -eq 'hs9118') {'HS9118'} else {$null});
        archive_namespace='tlsr8359';execution_backend='telink/tlsr8359';
        namespace_authority='human-confirmed-owning-TLSR-backup-tool-policy-20261014';die_identification_performed=$false;
        rawResult=$results[0].FullName;rawResultSha256=(Get-InkTagBackupSHA $results[0].FullName);preservedFirst=$original;
        boardQualified=$false;physicalCapacityQualified=$false;releaseQualified=$false;segmented=$segmented}
    $pair = [pscustomobject]@{Role='flash';First=$first;Second=$second;Bytes=$size}
    # Publication bucket is the owning tool, not an inference from PCB/marking.
    # Preserve requested-chip raw namespaces for VerifyOnly/Resume admission.
    $publicationSession = $Session.PSObject.Copy()
    $publicationSession.Chip = 'tlsr8359'
    return Complete-InkTagStockBackupSession -Session $publicationSession -Classification $classification -ReadPairs @($pair) `
        -Identity $identity -Ranges @([pscustomobject]@{address=0;bytes=$size;evidence='admitted-JEDEC-boundary'}) `
        -OriginalQualified ([bool]$FactorySourceConfirmed)
}
