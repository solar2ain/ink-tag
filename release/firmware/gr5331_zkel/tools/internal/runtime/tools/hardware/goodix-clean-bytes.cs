// InkTag GR host codecs. No target access, issuer or private key.
// Native layouts follow goodix_native_package/goodix_host_nvds and their
// independently tested Python oracles; Windows cryptography owns signatures.
using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Collections.Generic;
namespace InkTag {
public static class GoodixBytes {
    public static void Require(bool ok,string message) { if(!ok)throw new InvalidDataException(message); }
    public static byte[] Slice(byte[] b,int at,int n) {
        Require(b!=null&&at>=0&&n>=0&&at<=b.Length-n,"Binary extent outside input");
        byte[] r=new byte[n];Buffer.BlockCopy(b,at,r,0,n);return r;
    }
    public static byte[] Fill(int n,byte value) { byte[] r=new byte[n];for(int i=0;i<n;i++)r[i]=value;return r; }
    public static byte[] ReadFile(string path,int maximum) {
        using(var s=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
            Require(s.Length>0&&s.Length<=maximum,"File exceeds fixed size limit");byte[] b=new byte[(int)s.Length];int at=0;
            while(at<b.Length){int n=s.Read(b,at,b.Length-at);Require(n>0,"Truncated input file");at+=n;}
            Require(s.ReadByte()==-1,"Input grew while reading");return b;
        }
    }
    public static bool Equal(byte[] a,byte[] b) { if(a==null||b==null||a.Length!=b.Length)return false;int d=0;for(int i=0;i<a.Length;i++)d|=a[i]^b[i];return d==0; }
    public static bool Uniform(byte[] b,byte v) { foreach(byte x in b)if(x!=v)return false;return true; }
    public static uint U32(byte[] b,int at) { return BitConverter.ToUInt32(Slice(b,at,4),0); }
    public static ushort U16(byte[] b,int at) { return BitConverter.ToUInt16(Slice(b,at,2),0); }
    public static void Put(byte[] b,int at,byte[] value) { Require(at>=0&&value.Length<=b.Length-at,"Output extent outside image");Buffer.BlockCopy(value,0,b,at,value.Length); }
    public static void W32(byte[] b,int at,uint value) { Put(b,at,BitConverter.GetBytes(value)); }
    public static void W16(byte[] b,int at,ushort value) { Put(b,at,BitConverter.GetBytes(value)); }
    public static byte[] Reverse(byte[] b) { byte[] r=(byte[])b.Clone();Array.Reverse(r);return r; }
    public static byte[] Hex(string s) { Require(s!=null&&s.Length%2==0,"Invalid hex");byte[] r=new byte[s.Length/2];for(int i=0;i<r.Length;i++)r[i]=Convert.ToByte(s.Substring(i*2,2),16);return r; }
    public static string HexText(byte[] b) { return BitConverter.ToString(b).Replace("-","").ToLowerInvariant(); }
    public static string Sha(byte[] b) { using(var h=SHA256.Create())return HexText(h.ComputeHash(b)); }
    public static uint Sum(byte[] b) { uint s=0;foreach(byte x in b)s=unchecked(s+x);return s; }
    public static uint Fnv(byte[] b) { uint s=2166136261;foreach(byte x in b)s=unchecked((s^x)*16777619);return s; }
    public static uint Crc(byte[] b) { uint s=0xffffffff;foreach(byte x in b){s^=x;for(int i=0;i<8;i++)s=(s>>1)^((s&1)!=0?0xedb88320U:0);}return s^0xffffffff; }
    public static bool Contains(byte[] b,byte[] value) { return Count(b,value)>0; }
    public static int Count(byte[] b,byte[] value) { int n=0;for(int i=0;i<=b.Length-value.Length;i++){int j=0;while(j<value.Length&&b[i+j]==value[j])j++;if(j==value.Length)n++;}return n; }
    public static bool Address(byte[] b,int kind) {
        if(b==null||b.Length!=6||kind<0||kind>1||Uniform(b,0)||Uniform(b,255))return false;
        if(kind==0)return true;
        byte[] portion=(byte[])b.Clone();portion[0]&=63;
        return (b[0]&192)==192&&!Uniform(portion,0)&&!(portion[0]==63&&Uniform(Slice(portion,1,5),255));
    }
    public static byte[] AddressRecord(byte[] a,int kind) {
        Require(Address(a,kind),"Invalid typed BLE address");byte[] b=new byte[11];b[0]=(byte)(16|kind);Put(b,1,a);W32(b,7,Crc(Slice(b,0,7)));return b;
    }
    public static byte[] NvdsItem(ushort tag,byte[] p) {
        Require(tag!=0&&p!=null&&p.Length>0&&p.Length<=4076,"Invalid native NVDS record");byte[] b=new byte[8+p.Length];W16(b,0,tag);W16(b,2,(ushort)p.Length);b[4]=(byte)(tag+p.Length);b[5]=(byte)Sum(p);Put(b,8,p);return b;
    }
    public static void UniqueJsonKeys(string text) {
        // Conversion/parsing stays in PowerShell/.NET. This lexical pass only
        // rejects duplicate decoded keys that ConvertFrom-Json5.1 may replace.
        var objects=new Stack<HashSet<string>>();
        for(int i=0;i<text.Length;i++) {
            char c=text[i];if(c=='{'){objects.Push(new HashSet<string>(StringComparer.Ordinal));continue;}
            if(c=='}'){Require(objects.Count>0,"Unbalanced JSON object");objects.Pop();continue;}
            if(c!='"')continue;var value=new StringBuilder();bool ended=false;
            while(++i<text.Length) {
                c=text[i];if(c=='"'){ended=true;break;}
                if(c=='\\') {Require(++i<text.Length,"Truncated JSON escape");c=text[i];
                    if(c=='u'){Require(i+4<text.Length,"Truncated JSON unicode");value.Append((char)Convert.ToUInt16(text.Substring(i+1,4),16));i+=4;continue;}
                    string from="\"\\/bfnrt",to="\"\\/\b\f\n\r\t";int at=from.IndexOf(c);Require(at>=0,"Invalid JSON escape");c=to[at];
                }value.Append(c);
            }
            Require(ended,"Truncated JSON string");int next=i+1;while(next<text.Length&&Char.IsWhiteSpace(text[next]))next++;
            if(next<text.Length&&text[next]==':')Require(objects.Count>0&&objects.Peek().Add(value.ToString()),"Duplicate JSON field");
        }
        Require(objects.Count==0,"Unbalanced JSON object");
    }
    public static string JsonString(string text) {
        var b=new StringBuilder("\"");foreach(char c in text) {
            if(c=='"'||c=='\\'){b.Append('\\');b.Append(c);}
            else if(c=='\b')b.Append("\\b");else if(c=='\f')b.Append("\\f");
            else if(c=='\n')b.Append("\\n");else if(c=='\r')b.Append("\\r");else if(c=='\t')b.Append("\\t");
            else if(c<32||c>=127)b.Append("\\u"+((int)c).ToString("x4"));else b.Append(c);
        }return b.Append('"').ToString();
    }
    public static void VerifySignature(byte[] publicKey,byte[] signature,byte[] message,bool rsa) {
        if(rsa) {
            Require(publicKey.Length==520&&signature.Length==256,"Invalid RSA container");
            var p=new RSAParameters();p.Modulus=Reverse(Slice(publicKey,0,256));
            byte[] exponent=Reverse(Slice(publicKey,256,4));int start=0;while(start<3&&exponent[start]==0)start++;
            p.Exponent=Slice(exponent,start,4-start);
            using(var verifier=new RSACng()) {verifier.ImportParameters(p);Require(verifier.VerifyData(message,Reverse(signature),HashAlgorithmName.SHA256,RSASignaturePadding.Pss),"Publisher RSA-PSS signature failed");}
        } else {
            Require(publicKey.Length==65&&publicKey[0]==4&&signature.Length==64,"Invalid P-256 container");
            byte[] blob=new byte[72];W32(blob,0,0x31534345);W32(blob,4,32);Put(blob,8,Slice(publicKey,1,64));
            using(var key=CngKey.Import(blob,CngKeyBlobFormat.EccPublicBlob))using(var verifier=new ECDsaCng(key)) {
                verifier.HashAlgorithm=CngAlgorithm.Sha256;Require(verifier.VerifyData(message,signature),"Publisher P-256 signature failed");
            }
        }
    }
}
}
