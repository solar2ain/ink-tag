// Bounded codecs used by the PowerShell5.1 programmer. No transport or SDK.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

public sealed class NrfInitImage {
    public byte[] Bytes;
    public bool[] Present;
    public int AppBytes, RecoveryBytes;
    public uint Version;
}

public static class NrfInitBytes {
    public static byte[] Slice(byte[] b, int start, int count) {
        if (start < 0 || count < 0 || start > b.Length-count) throw new Exception("Truncated bytes");
        byte[] r=new byte[count]; Buffer.BlockCopy(b,start,r,0,count); return r;
    }
    public static byte[] Filled(int count, byte value) {
        byte[] b=new byte[count]; for(int i=0;i<count;i++) b[i]=value; return b;
    }
    public static uint U32(byte[] b,int offset) { return BitConverter.ToUInt32(Slice(b,offset,4),0); }
    public static void Put(byte[] b,int offset,uint value) { Buffer.BlockCopy(BitConverter.GetBytes(value),0,b,offset,4); }
    public static uint Crc(byte[] b,int start,int count) {
        uint c=0xffffffff;
        for(int i=start;i<start+count;i++) { c^=b[i]; for(int k=0;k<8;k++) c=(c>>1)^((c&1)!=0?0xedb88320u:0u); }
        return ~c;
    }
    public static uint Fnv(byte[] b) { unchecked { uint h=2166136261; foreach(byte x in b) h=(h^x)*16777619; return h; } }
    public static byte[] Sha(byte[] b) { using(SHA256 h=SHA256.Create()) return h.ComputeHash(b); }
    public static string Hash(byte[] b) { return BitConverter.ToString(Sha(b)).Replace("-","").ToLowerInvariant(); }
    public static bool Equal(byte[] a,byte[] b) {
        if(a.Length!=b.Length)return false; for(int i=0;i<a.Length;i++)if(a[i]!=b[i])return false; return true;
    }
    public static byte[] HexBytes(string s) {
        if(s.Length%2!=0)throw new Exception("Invalid hex"); byte[] b=new byte[s.Length/2];
        for(int i=0;i<b.Length;i++)b[i]=Convert.ToByte(s.Substring(i*2,2),16); return b;
    }
    public static void Check(bool ok,string message) { if(!ok)throw new Exception(message); }
    public static void CheckJson(string text) {
        // Duplicate keys must not silently overwrite a file/hash/policy binding.
        Stack<HashSet<string>> stack=new Stack<HashSet<string>>();
        for(int i=0;i<text.Length;i++) {
            char c=text[i];
            if(c=='{' || c=='[') { stack.Push(c=='{'?new HashSet<string>(StringComparer.Ordinal):null); continue; }
            if(c=='}' || c==']') { Check(stack.Count>0,"Malformed JSON");stack.Pop();continue; }
            if(c!='"')continue;
            StringBuilder value=new StringBuilder(); bool ended=false;
            while(++i<text.Length) {
                c=text[i]; if(c=='"'){ended=true;break;}
                if(c=='\\') {
                    Check(++i<text.Length,"Truncated JSON escape");c=text[i];
                    if(c=='u'){Check(i+4<text.Length,"Truncated Unicode");value.Append((char)Convert.ToUInt16(text.Substring(i+1,4),16));i+=4;continue;}
                    int at="\"\\/bfnrt".IndexOf(c);Check(at>=0,"JSON escape");value.Append("\"\\/\b\f\n\r\t"[at]);
                } else { Check(c>=32,"JSON control");value.Append(c); }
            }
            Check(ended,"Truncated JSON string");int next=i+1;while(next<text.Length&&Char.IsWhiteSpace(text[next]))next++;
            if(next<text.Length&&text[next]==':') {Check(stack.Count>0&&stack.Peek()!=null,"JSON key outside object");Check(stack.Peek().Add(value.ToString()),"Duplicate JSON member");}
        }
        Check(stack.Count==0,"Unclosed JSON");
    }
    public static Dictionary<string,byte[]> Zip(byte[] bytes,bool full) {
        Check(bytes.Length>0&&bytes.Length<=2097152,"ZIP size");
        int eocd=-1;for(int i=bytes.Length-22;i>=Math.Max(0,bytes.Length-65557);i--)if(U32(bytes,i)==0x06054b50){eocd=i;break;}
        Check(eocd>=0,"ZIP directory missing");
        Check(BitConverter.ToUInt16(bytes,eocd+4)==0&&BitConverter.ToUInt16(bytes,eocd+6)==0,"Split ZIP unsupported");
        int count=BitConverter.ToUInt16(bytes,eocd+10);Check(count==(full?4:3)&&BitConverter.ToUInt16(bytes,eocd+8)==count,"ZIP member count");
        Check(eocd+22+BitConverter.ToUInt16(bytes,eocd+20)==bytes.Length,"ZIP trailing bytes");
        int at=checked((int)U32(bytes,eocd+16)); HashSet<string> names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        Dictionary<string,uint> checksums=new Dictionary<string,uint>(StringComparer.Ordinal);
        for(int n=0;n<count;n++) {
            Check(at>=0&&at+46<=eocd&&U32(bytes,at)==0x02014b50,"ZIP directory");
            int flags=BitConverter.ToUInt16(bytes,at+8),method=BitConverter.ToUInt16(bytes,at+10),len=BitConverter.ToUInt16(bytes,at+28);
            Check((flags&1)==0&&(method==0||method==8)&&len>0&&len<=128,"ZIP encryption/method/name");
            string name=new UTF8Encoding(false,true).GetString(Slice(bytes,at+46,len));
            Check(name.IndexOfAny(new char[]{'/','\\',':'})<0&&name!="."&&name!=".."&&names.Add(name),"Unsafe/duplicate ZIP name");
            Check((U32(bytes,at+38)>>16&0xf000)!=0xa000,"Linked ZIP entry");
            checksums.Add(name,U32(bytes,at+16));
            at=checked(at+46+len+BitConverter.ToUInt16(bytes,at+30)+BitConverter.ToUInt16(bytes,at+32));
        }
        Dictionary<string,byte[]> result=new Dictionary<string,byte[]>(StringComparer.Ordinal);
        using(MemoryStream stream=new MemoryStream(bytes))using(ZipArchive archive=new ZipArchive(stream,ZipArchiveMode.Read)) {
            foreach(ZipArchiveEntry entry in archive.Entries) {
                int limit=full?(entry.FullName=="full.hex"?1500000:entry.FullName=="app-ota.zip"?1048576:entry.FullName=="gui-assets.bin"?12288:entry.FullName=="base.json"?4096:0):(entry.FullName=="manifest.json"?4096:entry.FullName.EndsWith(".dat",StringComparison.Ordinal)?512:entry.FullName.EndsWith(".bin",StringComparison.Ordinal)?69632:0);
                Check(limit>0&&entry.Length>0&&entry.Length<=limit,"ZIP entry size/name");
                byte[] data=new byte[(int)entry.Length];using(Stream input=entry.Open()){int read=0,k;while(read<data.Length&&(k=input.Read(data,read,data.Length-read))>0)read+=k;Check(read==data.Length&&input.ReadByte()==-1,"ZIP length");}
                Check(checksums.ContainsKey(entry.FullName)&&checksums[entry.FullName]==Crc(data,0,data.Length),"ZIP CRC mismatch");
                result.Add(entry.FullName,data);
            }
        }
        Check(result.Count==count,"ZIP entry count");return result;
    }
    static void WriteHexRecord(StringBuilder output, int offset, int type, byte[] bytes, int start, int count) {
        output.Append(':').Append(count.ToString("X2")).Append(offset.ToString("X4")).Append(type.ToString("X2"));
        int checksum = count + (offset >> 8) + (offset & 255) + type;
        for (int i = start; i < start + count; i++) {
            output.Append(bytes[i].ToString("X2"));
            checksum += bytes[i];
        }
        output.Append(((-checksum) & 255).ToString("X2")).Append('\n');
    }

    public static byte[] WriteIntelHex(byte[] bytes, int address) {
        StringBuilder output = new StringBuilder();
        int upper = -1;
        for (int start = 0; start < bytes.Length;) {
            int current = address + start;
            int segment = current >> 16;
            int offset = current & 65535;
            if (segment != upper) {
                byte[] extension = {(byte)(segment >> 8), (byte)segment};
                WriteHexRecord(output, 0, 4, extension, 0, 2);
                upper = segment;
            }
            int count = Math.Min(16, Math.Min(bytes.Length - start, 65536 - offset));
            WriteHexRecord(output, offset, 0, bytes, start, count);
            start += count;
        }
        output.Append(":00000001FF\n");
        return Encoding.ASCII.GetBytes(output.ToString());
    }

    public static NrfInitImage IntelHex(byte[] text) {
        NrfInitImage result=new NrfInitImage{Bytes=Filled(196608,255),Present=new bool[196608]};uint basis=0;bool ended=false;
        foreach(string raw in Encoding.ASCII.GetString(text).Split(new char[]{'\r','\n'},StringSplitOptions.RemoveEmptyEntries)) {
            Check(!ended&&raw.StartsWith(":",StringComparison.Ordinal),"HEX EOF/record");byte[] record=HexBytes(raw.Substring(1));
            Check(record.Length>=5&&record.Length==record[0]+5,"HEX length");int sum=0;foreach(byte v in record)sum+=v;Check((sum&255)==0,"HEX checksum");
            int count=record[0],offset=record[1]*256+record[2],kind=record[3];
            if(kind==0) { for(int i=0;i<count;i++){uint a=checked(basis+(uint)offset+(uint)i);Check(a<196608,"Full crosses internal Flash/FICR/UICR");Check(!result.Present[a]||result.Bytes[a]==record[4+i],"Conflicting HEX");result.Bytes[a]=record[4+i];result.Present[a]=true;} }
            else if(kind==2||kind==4){Check(count==2&&offset==0,"HEX base");basis=(uint)(record[4]*256+record[5])<<(kind==2?4:16);}
            else if(kind==1){Check(count==0&&offset==0,"HEX EOF");ended=true;}
            else Check((kind==3||kind==5)&&count==4&&offset==0,"HEX type");
        }
        Check(ended,"HEX EOF missing");return result;
    }
    public static void Boot(NrfInitImage image,byte[] assets) {
        byte[] b=image.Bytes;Check(b.Length==196608,"MCU size");
        Check(U32(b,0xff8)==0x2a000&&U32(b,0xffc)==0x2e000,"Recovery/MBR selection");
        foreach(int origin in new int[]{0x19000,0x2a000}) {
            uint sp=U32(b,origin),reset=U32(b,origin+4);Check(sp>0x20000000&&sp<=0x20006000&&sp%8==0&&(reset&1)!=0,"Stack/Thumb vector");
            Check((reset&~1u)>=origin&&(reset&~1u)<(origin==0x19000?0x2a000:0x2e000),"Reset outside component");
        }
        Check(Equal(Slice(b,0x2e000,0x323),Slice(b,0x2f000,0x323)),"DFU backup mismatch");
        foreach(int p in new int[]{0x2e000,0x2f000}) {
            int app=checked((int)U32(b,p+0x18)),sd=checked((int)U32(b,p+0x34));
            Check(app>0&&app<=69632&&sd>0&&sd<=0x18000,"App/SoftDevice sizes");
            Check(U32(b,p)==Crc(b,p+4,88)&&U32(b,p+4)==2&&U32(b,p+0x20)==1,"DFU settings CRC/schema/bank");
            Check(b[p+0x260]==1&&b[p+0x2a1]==1&&U32(b,p+0x25c)==Crc(b,p+0x260,195),"Boot validation types/CRC");
            Check(U32(b,p+0x261)==Crc(b,0x1000,sd),"SoftDevice CRC");
            Check(U32(b,p+0x2a2)==Crc(b,0x19000,app)&&U32(b,p+0x1c)==Crc(b,0x19000,app),"App CRC");
            image.AppBytes=app;image.Version=U32(b,p+8);
        }
        int end=0x2e000;while(end>0x2a000&&!image.Present[end-1])end--;image.RecoveryBytes=end-0x2a000;
        Check(image.RecoveryBytes>0,"Missing Recovery");
        Check(assets.Length==12288&&U32(assets,0)==0x44464b49&&U32(assets,4)==1&&U32(assets,8)==12228&&U32(assets,12)==Crc(assets,16,12228),"GUI CRC/schema");
        for(int i=12244;i<12288;i++)Check(assets[i]==255,"GUI padding");
    }
    static uint Var(byte[] b,ref int at) {
        uint value=0;for(int shift=0;shift<35;shift+=7){Check(at<b.Length,"Truncated varint");byte x=b[at++];Check(shift<28||(x&0xf0)==0,"Varint overflow");value|=(uint)(x&127)<<shift;if(x<128)return value;}throw new Exception("Varint overflow");
    }
    static Dictionary<int,object> Fields(byte[] b) {
        Check(b.Length<=512,"Init size");Dictionary<int,object> map=new Dictionary<int,object>();int at=0;
        while(at<b.Length){uint tag=Var(b,ref at);int key=(int)(tag>>3),wire=(int)(tag&7);Check(key>0&&!map.ContainsKey(key)&&(wire==0||wire==2),"Duplicate/unknown init wire");uint value=Var(b,ref at);if(wire==0)map.Add(key,value);else {byte[] data=Slice(b,at,checked((int)value));at+=data.Length;map.Add(key,data);}}return map;
    }
    static bool Num(Dictionary<int,object> m,int k,uint n,bool optional) {return !m.ContainsKey(k)?optional:m[k] is uint&&(uint)m[k]==n;}
    static byte[] ByteField(Dictionary<int,object> m,int k){Check(m.ContainsKey(k)&&m[k] is byte[],"Missing byte field");return (byte[])m[k];}
    public static uint CheckApp(byte[] app,byte[] dat,bool unsigned) {
        Dictionary<int,object> packet=Fields(dat),signed=null,command;
        if(packet.Count==1&&packet.ContainsKey(2)) {signed=Fields(ByteField(packet,2));Check(signed.Count==3&&Num(signed,2,0,false)&&ByteField(signed,3).Length==64,"Signed init shape");command=Fields(ByteField(signed,1));}
        else {Check(unsigned&&packet.Count==1&&packet.ContainsKey(1),"Signed App required");command=Fields(ByteField(packet,1));}
        Check(command.Count==2&&Num(command,1,1,false),"INIT command");byte[] initBytes=ByteField(command,2);Dictionary<int,object> init=Fields(initBytes),digest=Fields(ByteField(init,8)),validation=Fields(ByteField(init,10));
        foreach(int k in init.Keys)Check(k>=1&&k<=10,"Unknown init field");
        Check(Num(init,2,52,false)&&Num(init,4,0,true)&&Num(init,5,0,true)&&Num(init,6,0,true)&&Num(init,7,(uint)app.Length,false)&&Num(init,9,0,true),"HW52/App size/init context");
        Check(app.Length>=8&&app.Length<=69632&&Equal(ByteField(init,3),new byte[]{0xa6,2}),"S112/App context");
        byte[] hash=Sha(app);Array.Reverse(hash);Check(digest.Count==2&&Num(digest,1,3,false)&&Equal(ByteField(digest,2),hash),"App SHA256");
        Check(validation.Count==2&&Num(validation,1,1,false)&&ByteField(validation,2).Length==0,"Generated CRC boot check");
        Check(init.ContainsKey(1)&&init[1] is uint&&((uint)init[1]&65535)!=0,"App version");
        if(signed!=null) {
            byte[] keyLE=HexBytes("0edf3d346f2be4a0a641930c3bba0f21fff2bf1913ed68d1a81b47a87e0bf9aec2a2697fc77f10daba686704f3323fed849591ec9539b288ae5be98f221fe7d1");
            byte[] blob=new byte[72];Put(blob,0,0x31534345);Put(blob,4,32);byte[] signature=ByteField(signed,3);
            for(int i=0;i<32;i++){blob[8+i]=keyLE[31-i];blob[40+i]=keyLE[63-i];}
            byte[] sigBE=new byte[64];for(int i=0;i<32;i++){sigBE[i]=signature[31-i];sigBE[32+i]=signature[63-i];}
            using(CngKey key=CngKey.Import(blob,CngKeyBlobFormat.EccPublicBlob))using(ECDsaCng ecdsa=new ECDsaCng(key))Check(ecdsa.VerifyHash(Sha(initBytes),sigBE),"Invalid supplied App signature");
        }
        return (uint)init[1];
    }
}
