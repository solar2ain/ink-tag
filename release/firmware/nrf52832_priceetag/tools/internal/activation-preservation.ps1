# Public IKAC1 verification only. Windows PowerShell 5.1/.NET CNG.
# No issuer, private key, device access or user token-import command.
if (-not ('InkTag.PublicActivation' -as [type])) {
    $activationTypeSource=@'
using System;
using System.IO;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
namespace InkTag {
public static class PublicActivation {
    static readonly byte[] Publisher = Hex("6cef365a284c497a548f93eee2397fe43673a2499c2494a52241122dbd17944278341110bb317e5ba5d7f49e71d71bcfb14731327643156e5a23ec9261d24b3b");
    static readonly BigInteger Order = Integer(Hex("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551"));
    public static byte[] Hex(string s) { if (s.Length%2!=0) throw new ArgumentException("hex length"); byte[] b=new byte[s.Length/2]; for(int i=0;i<b.Length;i++) b[i]=Convert.ToByte(s.Substring(i*2,2),16); return b; }
    static BigInteger Integer(byte[] b) { byte[] r=new byte[b.Length+1]; for(int i=0;i<b.Length;i++)r[i]=b[b.Length-1-i]; return new BigInteger(r); }
    static byte[] Slice(byte[] b,int at,int n) { byte[] r=new byte[n]; Buffer.BlockCopy(b,at,r,0,n); return r; }
    static uint Be(byte[] b,int at,int n) { uint r=0;for(int i=0;i<n;i++)r=(r<<8)|b[at+i];return r; }
    static bool Equal(byte[] a,byte[] b) { if(a==null||b==null||a.Length!=b.Length)return false;int d=0;for(int i=0;i<a.Length;i++)d|=a[i]^b[i];return d==0; }
    public static byte[] DeviceId(ushort platform,ushort product,byte[] uid) {
        if(platform==0||product==0||uid==null||uid.Length<1||uid.Length>32)throw new ArgumentException("invalid immutable activation identity");
        using(var s=new MemoryStream()) { byte[] domain=Encoding.ASCII.GetBytes("INKTAG-DEVICE-ID-V1\0");s.Write(domain,0,domain.Length);
            foreach(ushort v in new ushort[]{platform,product,(ushort)uid.Length}) {s.WriteByte((byte)(v>>8));s.WriteByte((byte)v);}s.Write(uid,0,uid.Length);
            using(var h=SHA256.Create())return Slice(h.ComputeHash(s.ToArray()),0,8);
        }
    }
    public static uint[] Verify(byte[] token,ushort product,uint mask,ushort epoch,byte[] id,byte[] fixtureKey,byte keyId) {
        if(token==null||token.Length!=96)throw new ArgumentException("activation token is incomplete");
        uint sequence=Be(token,4,4),features=Be(token,10,4);ushort actualProduct=(ushort)Be(token,8,2),minimum=(ushort)Be(token,14,2);
        byte[] identity=Slice(token,16,8);bool any=false;foreach(byte b in identity)any|=b!=0;
        if(token[0]!=73||token[1]!=65||token[2]!=1||token[3]!=keyId||sequence==0||features==0||!any)throw new ArgumentException("activation token format/key/sequence is invalid");
        BigInteger r=Integer(Slice(token,32,32)),sigS=Integer(Slice(token,64,32));
        if(r<=0||r>=Order||sigS<=0||sigS>Order/2)throw new ArgumentException("activation signature is not canonical low-S");
        byte[] pub=fixtureKey??Publisher;if(pub.Length!=64)throw new ArgumentException("public key length");
        byte[] blob=new byte[72];Buffer.BlockCopy(BitConverter.GetBytes(0x31534345),0,blob,0,4);Buffer.BlockCopy(BitConverter.GetBytes(32),0,blob,4,4);Buffer.BlockCopy(pub,0,blob,8,64);
        byte[] domainBytes=Encoding.ASCII.GetBytes("INKTAG-ACTIVATE-V1\0"),message=new byte[domainBytes.Length+32];Buffer.BlockCopy(domainBytes,0,message,0,domainBytes.Length);Buffer.BlockCopy(token,0,message,domainBytes.Length,32);
        using(var key=CngKey.Import(blob,CngKeyBlobFormat.EccPublicBlob))using(var verifier=new ECDsaCng(key)) {
            verifier.HashAlgorithm=CngAlgorithm.Sha256;if(!verifier.VerifyData(message,Slice(token,32,64)))throw new CryptographicException("activation publisher signature failed");
        }
        if(actualProduct!=product||(features&~mask)!=0||minimum>epoch)throw new ArgumentException("activation product/features/security epoch incompatible with App");
        if(id!=null&&(id.Length!=8||!Equal(identity,id)))throw new ArgumentException("activation belongs to a different immutable device identity");
        return new uint[]{sequence,keyId,features,actualProduct,minimum,id==null?0U:1U};
    }
}
}
'@
    if($PSVersionTable.PSEdition -eq 'Desktop'){Add-Type -ReferencedAssemblies System.Numerics,System.Security -TypeDefinition $activationTypeSource}
    else {Add-Type -TypeDefinition $activationTypeSource}
}
function Get-InkTagActivationDeviceId {
    param([UInt16]$PlatformId,[UInt16]$ProductId,[byte[]]$RawUid)
    return ,([InkTag.PublicActivation]::DeviceId($PlatformId,$ProductId,$RawUid))
}
function Test-InkTagActivationToken {
    param([byte[]]$Token,[UInt16]$ProductId=1,[UInt32]$FeatureMask=1,
          [UInt16]$SecurityEpoch=0,[byte[]]$DeviceId=$null,
          [byte[]]$PublicKey=$null,[byte]$KeyId=1)
    $r=[InkTag.PublicActivation]::Verify($Token,$ProductId,$FeatureMask,$SecurityEpoch,$DeviceId,$PublicKey,$KeyId)
    [pscustomobject]@{sequence=$r[0];keyId=$r[1];featureMask=$r[2];productId=$r[3];minActivationEpoch=$r[4];signatureVerified=$true;hostHardwareBindingVerified=($r[5] -eq 1)}
}
