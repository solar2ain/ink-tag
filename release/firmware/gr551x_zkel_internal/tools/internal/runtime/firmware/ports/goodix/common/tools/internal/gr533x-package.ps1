# Decode only the SDK's initialized chip_trim0_t in documented SRAM.
# GR533x does not use GR551x's fixed eFuse-cache address. Its linker places
# this 96-byte cache differently in each App. Never infer identity from an
# installed application's name, a factory backup, CPUID or a guessed address.
function Get-InkTagGr533xCachedPackage {
    param([byte[]]$Sram)
    if($Sram.Length -ne 0x18000){throw 'Incomplete GR533x SRAM identity read'}
    $known=@{
        0x0204='GR5331CENI'
        0x0205='GR5331AENI'
        0x0208='GR5331DEBI'
    }
    $found=@()
    for($i=0;$i -le $Sram.Length-96;$i+=2) {
        # gr533x_chip_trim.h: pattern at 0; production fields at 8..19.
        if([BitConverter]::ToUInt16($Sram,$i) -ne 0x4744){continue}
        $chip=[BitConverter]::ToUInt16($Sram,$i+12)
        $package=[int][BitConverter]::ToUInt16($Sram,$i+14)
        $flash=[int][BitConverter]::ToUInt16($Sram,$i+16)
        $ram=[int][BitConverter]::ToUInt16($Sram,$i+18)
        $ate=[int][BitConverter]::ToUInt16($Sram,$i+8)
        $checksum=[BitConverter]::ToUInt32($Sram,$i+4)
        if($chip -ne 0x5331 -or !$known.ContainsKey($package) -or
           $flash -ne 512 -or $ram -ne 96 -or $ate -eq 0 -or $ate -eq 65535 -or
           $checksum -eq 0 -or $checksum -eq [uint32]::MaxValue){continue}
        $found+=@{chip='gr5331';chipConfirmed=$true;packageId=$package;
            packageName=$known[$package];flashBytes=$flash*1024;ramBytes=$ram*1024;
            cacheAddress=('0x{0:X8}' -f (0x20000000+$i));
            productionKey=[BitConverter]::ToString($Sram,$i,32);
            identitySource='SDK initialized chip_trim0_t cache: marker, production chip/package, Flash and SRAM sizes';
            trimChecksumIndependentlyVerified=$false}
    }
    if(!$found.Count){return $null}
    # ROM and SDK may retain separate caches. Only their unchanged production
    # header is identity; mutable ADC trim bytes are not. Require every valid
    # production header to agree, never choose between conflicting identities.
    $keys=@($found | ForEach-Object {$_.productionKey} | Select-Object -Unique)
    if($keys.Count -ne 1){return $null}
    $result=$found[0]
    $result.cacheAddresses=@($found | ForEach-Object {$_.cacheAddress})
    $result.Remove('productionKey')
    return $result
}
