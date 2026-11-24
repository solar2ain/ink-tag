// Native Windows host codecs for the existing HW56 formats. No transport,
// activation issuer, firmware builder, or Python/CLI dependency.
using System;
using System.IO;
using System.IO.Compression;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Runtime.Serialization.Json;
using System.Text;
using System.Xml;

public static class InkTagNordicImage {
    public const int FlashBytes = 0x80000, Settings = 0x56000;
    public static byte[] Part(byte[] b, int at, int n) {
        if(at<0 || n<0 || at>b.Length-n) throw new InvalidDataException("Truncated native data");
        byte[] v=new byte[n]; Buffer.BlockCopy(b,at,v,0,n); return v;
    }
    public static byte[] Fill(int n, byte value) { byte[] b=new byte[n]; if(value!=0) for(int i=0;i<n;i++) b[i]=value; return b; }
    public static uint U32(byte[] b,int p) { Part(b,p,4); return BitConverter.ToUInt32(b,p); }
    public static ushort U16(byte[] b,int p) { Part(b,p,2); return BitConverter.ToUInt16(b,p); }
    public static void Put32(byte[] b,int p,uint v) { Buffer.BlockCopy(BitConverter.GetBytes(v),0,b,p,4); }
    public static void Put16(byte[] b,int p,ushort v) { Buffer.BlockCopy(BitConverter.GetBytes(v),0,b,p,2); }
    public static bool Equal(byte[] a,byte[] b) { return a.Length==b.Length && a.SequenceEqual(b); }
    public static string Hash(byte[] b) { using(SHA256 h=SHA256.Create()) return BitConverter.ToString(h.ComputeHash(b)).Replace("-","").ToLowerInvariant(); }
    public static byte[] Unhex(string s) { if((s.Length&1)!=0) throw new InvalidDataException("Invalid hex"); byte[] b=new byte[s.Length/2]; for(int i=0;i<b.Length;i++) b[i]=Convert.ToByte(s.Substring(i*2,2),16); return b; }
    public static uint Crc(byte[] b) { uint c=0xffffffff; foreach(byte v in b) { c^=v; for(int k=0;k<8;k++) c=(c>>1)^((c&1)!=0?0xedb88320u:0); } return c^0xffffffff; }
    public static uint Fnv(byte[] b) { uint v=2166136261; unchecked { foreach(byte c in b) v=(v^c)*16777619; } return v; }
    public static XmlDocument Json(byte[] b) {
        if(b.Length>=3 && b[0]==239 && b[1]==187 && b[2]==191) b=Part(b,3,b.Length-3);
        if(b.Length>16384) throw new InvalidDataException("JSON exceeds native bound");
        XmlDocument d=new XmlDocument();
        using(XmlDictionaryReader r=JsonReaderWriterFactory.CreateJsonReader(b,XmlDictionaryReaderQuotas.Max)) d.Load(r);
        foreach(XmlElement e in d.SelectNodes("//*[@type='object']")) {
            HashSet<string> seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(XmlNode child in e.ChildNodes) if(!seen.Add(child.Name)) throw new InvalidDataException("Duplicate JSON member");
        }
        return d;
    }
    static string CanonicalJson(byte[] b) {
        XmlDocument d=Json(b);
        foreach(XmlElement e in d.SelectNodes("//*[@type='object']")) {
            XmlNode[] children=e.ChildNodes.Cast<XmlNode>().OrderBy(x=>x.Name,StringComparer.Ordinal).ToArray();
            foreach(XmlNode n in children) e.AppendChild(n);
        }
        return d.OuterXml;
    }
    public static bool SameJson(byte[] a,byte[] b) { return CanonicalJson(a)==CanonicalJson(b); }
    public static Dictionary<string,byte[]> Zip(byte[] data,bool outer) {
        Dictionary<string,byte[]> files=new Dictionary<string,byte[]>(StringComparer.Ordinal);
        if(data.Length>2097152) throw new InvalidDataException("Firmware archive exceeds bound");
        using(MemoryStream m=new MemoryStream(data,false)) using(ZipArchive z=new ZipArchive(m,ZipArchiveMode.Read)) {
            if(z.Entries.Count!=3) throw new InvalidDataException("Exactly three native ZIP members required");
            foreach(ZipArchiveEntry e in z.Entries) {
                string n=e.FullName;
                int limit=outer?(n=="base.json"?4096:n=="full.hex"?1500000:n=="app-ota.zip"?1048576:0):
                    (n=="manifest.json"?4096:n.EndsWith(".bin",StringComparison.Ordinal)?163840:n.EndsWith(".dat",StringComparison.Ordinal)?512:0);
                if(limit==0 || n.IndexOfAny(new char[]{'/','\\',':'})>=0 || n=="." || n==".." || e.Length<=0 || e.Length>limit || files.ContainsKey(n) || ((e.ExternalAttributes>>16)&0xf000)==0xa000)
                    throw new InvalidDataException("Unsafe, duplicate or oversized native ZIP member");
                using(Stream s=e.Open()) using(MemoryStream dst=new MemoryStream()) { s.CopyTo(dst); byte[] raw=dst.ToArray(); if(raw.Length!=e.Length) throw new InvalidDataException("Short ZIP member"); files.Add(n,raw); }
            }
        }
        return files;
    }
    public static byte[] FullZip(byte[] metadata,byte[] hex,byte[] ota) {
        using(MemoryStream m=new MemoryStream()) {
            using(ZipArchive z=new ZipArchive(m,ZipArchiveMode.Create,true)) {
                string[] names={"base.json","full.hex","app-ota.zip"};byte[][] data={metadata,hex,ota};
                for(int i=0;i<names.Length;i++) { ZipArchiveEntry e=z.CreateEntry(names[i],CompressionLevel.Optimal); e.LastWriteTime=new DateTimeOffset(1980,1,1,0,0,0,TimeSpan.Zero); using(Stream s=e.Open()) s.Write(data[i],0,data[i].Length); }
            }
            return m.ToArray();
        }
    }
    public static byte[] ReadHex(byte[] data) {
        byte[] image=Fill(FlashBytes,255); bool[] seen=new bool[FlashBytes]; uint high=0; bool eof=false;
        foreach(string line in Encoding.ASCII.GetString(data).Split(new char[]{'\n','\r'},StringSplitOptions.RemoveEmptyEntries)) {
            if(eof || !line.StartsWith(":")) throw new InvalidDataException("Invalid Intel HEX ordering");
            byte[] b=Unhex(line.Substring(1)); if(b.Length<5 || b.Length!=b[0]+5 || b.Sum(x=>(int)x)%256!=0) throw new InvalidDataException("Intel HEX length/checksum");
            int low=(b[1]<<8)|b[2]; int type=b[3];
            if(type==4 && b[0]==2 && low==0) high=(uint)((b[4]<<8)|b[5])<<16;
            else if(type==0) for(int i=0;i<b[0];i++) { uint at=high+(uint)low+(uint)i; if(at>=0x60000 || seen[(int)at]) throw new InvalidDataException("Overlapping/outside native Full HEX"); seen[(int)at]=true; image[(int)at]=b[4+i]; }
            else if(type==1 && b[0]==0 && low==0) eof=true;
            else if(type!=5 || b[0]!=4 || low!=0) throw new InvalidDataException("Unsupported native HEX record");
        }
        if(!eof || !seen[0] || !seen[0x26000] || !seen[0x58000] || !seen[0x5e000] || !seen[0x5f000]) throw new InvalidDataException("Incomplete Full HEX");
        return image;
    }
    static string HexLine(int address,int type,byte[] bytes) {
        List<byte> line=new List<byte>(); line.Add((byte)bytes.Length); line.Add((byte)(address>>8)); line.Add((byte)address); line.Add((byte)type); line.AddRange(bytes); line.Add((byte)(-line.Sum(x=>(int)x)));
        return ":"+BitConverter.ToString(line.ToArray()).Replace("-","");
    }
    public static string WriteHex(byte[] image) {return WriteHex(image,FlashBytes);}
    public static string TemplateHex(byte[] image) {return WriteHex(image,0x60000);}
    static string WriteHex(byte[] image,int extent) {
        if(image.Length!=FlashBytes) throw new InvalidDataException("Full main-Flash size");
        StringBuilder s=new StringBuilder();
        for(int i=0;i<extent;i+=16) { if((i&65535)==0) s.AppendLine(HexLine(0,4,new byte[]{(byte)(i>>24),(byte)(i>>16)})); s.AppendLine(HexLine(i&65535,0,Part(image,i,16))); }
        s.AppendLine(":00000001FF"); return s.ToString();
    }
    static uint Varint(byte[] b,ref int at) { uint v=0; for(int n=0;n<5 && at<b.Length;n++) { byte c=b[at++]; if(n==4 && c>15) break; v|=(uint)(c&127)<<(n*7); if(c<128) return v; } throw new InvalidDataException("Native init varint"); }
    static Dictionary<int,object> Fields(byte[] b) {
        if(b.Length>512) throw new InvalidDataException("Native init bound");
        Dictionary<int,object> d=new Dictionary<int,object>(); int at=0;
        while(at<b.Length) { uint tag=Varint(b,ref at); int k=(int)(tag>>3),wire=(int)(tag&7); if(k==0 || d.ContainsKey(k) || (wire!=0 && wire!=2)) throw new InvalidDataException("Duplicate/unknown init field"); uint n=Varint(b,ref at); if(wire==0) d.Add(k,n); else { if(n>(uint)(b.Length-at)) throw new InvalidDataException("Truncated init field"); d.Add(k,Part(b,at,(int)n)); at+=(int)n; } }
        return d;
    }
    static byte[] B(Dictionary<int,object> d,int k) { if(!d.ContainsKey(k) || !(d[k] is byte[])) throw new InvalidDataException("Native init bytes missing"); return (byte[])d[k]; }
    static uint N(Dictionary<int,object> d,int k,uint fallback) { if(!d.ContainsKey(k)) return fallback; if(!(d[k] is uint)) throw new InvalidDataException("Native init integer"); return (uint)d[k]; }
    public static bool P256(byte[] key,byte[] message,byte[] sig) {
        if(key.Length!=64 || sig.Length!=64) return false;
        byte[] blob=new byte[72]; Put32(blob,0,0x31534345); Put32(blob,4,32); Buffer.BlockCopy(key,0,blob,8,64);
        using(CngKey k=CngKey.Import(blob,CngKeyBlobFormat.EccPublicBlob)) using(ECDsaCng e=new ECDsaCng(k)) { e.HashAlgorithm=CngAlgorithm.Sha256; return e.VerifyData(message,sig); }
    }
    public static bool SignedOta(byte[] data) {
        Dictionary<string,byte[]> f=Zip(data,false); XmlDocument m=Json(f["manifest.json"]);
        XmlNode n=m.SelectSingleNode("/root/manifest/application/dat_file");
        if(n==null || !f.ContainsKey(n.InnerText)) throw new InvalidDataException("OTA DAT missing");
        Dictionary<int,object> p=Fields(f[n.InnerText]);
        if(p.Count!=1 || (!p.ContainsKey(1) && !p.ContainsKey(2))) throw new InvalidDataException("OTA command envelope");
        return p.ContainsKey(2);
    }
    public static uint VerifyOta(byte[] data,byte[] image,bool requireSigned) {
        Dictionary<string,byte[]> files=Zip(data,false); XmlDocument manifest=Json(files["manifest.json"]);
        XmlNode appNode=manifest.SelectSingleNode("/root/manifest/application");
        if(appNode==null || appNode.ChildNodes.Count!=2 || appNode.ParentNode.ChildNodes.Count!=1 || appNode.ParentNode.ParentNode.ChildNodes.Count!=1) throw new InvalidDataException("OTA manifest");
        string bn=appNode["bin_file"].InnerText,dn=appNode["dat_file"].InnerText;
        if(bn==dn || !files.ContainsKey(bn) || !files.ContainsKey(dn) || !bn.EndsWith(".bin",StringComparison.Ordinal) || !dn.EndsWith(".dat",StringComparison.Ordinal)) throw new InvalidDataException("OTA members");
        byte[] app=files[bn]; Dictionary<int,object> packet=Fields(files[dn]),command; byte[] signature=null;
        if(packet.Count==1 && packet.ContainsKey(2)) { Dictionary<int,object> signed=Fields(B(packet,2)); if(signed.Count!=3 || N(signed,2,99)!=0) throw new InvalidDataException("Signed init"); signature=B(signed,3); command=Fields(B(signed,1)); }
        else if(!requireSigned && packet.Count==1 && packet.ContainsKey(1)) command=Fields(B(packet,1));
        else throw new InvalidDataException("Required publisher signature missing");
        if(command.Count!=2 || N(command,1,0)!=1 || (!requireSigned && signature!=null)) throw new InvalidDataException("Explicit Base/Pro init policy");
        byte[] initRaw=B(command,2); Dictionary<int,object> init=Fields(initRaw),digest=Fields(B(init,8)),validation=Fields(B(init,10)); uint version=N(init,1,0);
        byte[] hash; using(SHA256 h=SHA256.Create()) hash=h.ComputeHash(app); Array.Reverse(hash);
        if(init.Keys.Any(x=>x<1 || x>10) || version>>16!=1 || (version&65535)==0 || N(init,2,0)!=56 || N(init,4,0)!=0 || N(init,5,0)!=0 || N(init,6,0)!=0 || N(init,7,0)!=app.Length || N(init,9,0)!=0 || app.Length<8 || app.Length>163840 || !Equal(B(init,3),new byte[]{0x81,2}) || digest.Count!=2 || N(digest,1,0)!=3 || !Equal(B(digest,2),hash) || validation.Count!=2 || N(validation,1,0)!=1 || B(validation,2).Length!=0 || U32(image,0x5f008)!=version || !Equal(app,Part(image,0x26000,app.Length))) throw new InvalidDataException("OTA native HW/epoch/hash/Full pairing");
        if(signature!=null) {
            byte[] key=Unhex("0edf3d346f2be4a0a641930c3bba0f21fff2bf1913ed68d1a81b47a87e0bf9aec2a2697fc77f10daba686704f3323fed849591ec9539b288ae5be98f221fe7d1");
            Array.Reverse(key,0,32); Array.Reverse(key,32,32); if(signature.Length!=64) throw new InvalidDataException("Publisher signature size"); Array.Reverse(signature,0,32); Array.Reverse(signature,32,32);
            if(!P256(key,initRaw,signature)) throw new InvalidDataException("Publisher signature invalid");
            if(U32(image,0x5f000+682)==0x50424631) {
                byte[] dat=files[dn]; uint proofSize=U32(image,0x5f000+678);
                if(proofSize!=dat.Length || !Equal(Part(image,0x5f000+92,dat.Length),dat) || Part(image,0x5f000+92+dat.Length,512-dat.Length).Any(x=>x!=255) || Part(image,0x5f000+56,36).Any(x=>x!=0)) throw new InvalidDataException("Retained signed boot proof differs from OTA DAT");
            }
        }
        return version;
    }
    public static Dictionary<int,byte[]> Journal(byte[] pair,bool optional) {
        try {
            if(pair.Length!=8192) throw new InvalidDataException("Settings pair size");
            int active=-1; uint generation=0;
            for(int p=0;p<8192;p+=4096) {
                if(U32(pair,p)==0x31505349 && U32(pair,p+8)==~U32(pair,p+4) && U32(pair,p+12)==0xffffffff) { uint g=U32(pair,p+4); if(active>=0 && (g==generation || unchecked(g-generation)==0x80000000)) throw new InvalidDataException("Ambiguous journal generation"); if(active<0 || unchecked((int)(g-generation))>0) {active=p;generation=g;} }
                else if(!optional && Part(pair,p,4096).Any(x=>x!=255)) throw new InvalidDataException("Invalid template page");
            }
            if(active<0) throw new InvalidDataException("No committed settings page");
            Dictionary<int,byte[]> records=new Dictionary<int,byte[]>(); int at=active+16;
            while(at+12<=active+4096) {
                if(U32(pair,at)==0xffffffff) { if(!optional && Part(pair,at,active+4096-at).Any(x=>x!=255)) throw new InvalidDataException("Unclean journal tail"); break; }
                int type=U16(pair,at+4),n=U16(pair,at+6),size=16+((n+3)&~3);
                if(U32(pair,at)!=0x31525349 || type<1 || type>32 || n>420 || at+size>active+4096 || U32(pair,at+size-4)!=0x31435349) { if(optional) break; throw new InvalidDataException("Uncommitted journal record"); }
                byte[] data=Part(pair,at+12,n),check=new byte[4+n]; Buffer.BlockCopy(pair,at+4,check,0,4); Buffer.BlockCopy(data,0,check,4,n);
                if(Fnv(check)!=U32(pair,at+8)) { if(optional) break; throw new InvalidDataException("Journal checksum"); }
                if(n==0) records.Remove(type); else records[type]=data; at+=size;
            }
            if(!optional && records.ContainsKey(32)) throw new InvalidDataException("Pending owner operation in selected template");
            return records;
        } catch(InvalidDataException) { if(optional) return new Dictionary<int,byte[]>(); throw; }
    }
    public static byte[] EncodeJournal(Dictionary<int,byte[]> records,uint generation) {
        byte[] pair=Fill(8192,255); Put32(pair,0,0x31505349); Put32(pair,4,generation); Put32(pair,8,~generation); int at=16;
        foreach(int type in records.Keys.OrderBy(x=>x)) { byte[] data=records[type]; int n=data.Length,size=16+((n+3)&~3); if(type<1 || type>32 || n<1 || n>420 || at+size>4096) throw new InvalidDataException("Journal capacity/type"); Put32(pair,at,0x31525349); Put16(pair,at+4,(ushort)type); Put16(pair,at+6,(ushort)n); byte[] check=new byte[4+n]; Buffer.BlockCopy(pair,at+4,check,0,4); Buffer.BlockCopy(data,0,check,4,n); Put32(pair,at+8,Fnv(check)); Buffer.BlockCopy(data,0,pair,at+12,n); Array.Clear(pair,at+12+n,((n+3)&~3)-n); Put32(pair,at+size-4,0x31435349); at+=size; }
        return pair;
    }
    static void Text(byte[] data,int max) {
        string s=new UTF8Encoding(false,true).GetString(data); int n=0;
        for(int i=0;i<s.Length;i++) { if(s[i]<32 || s[i]==127) throw new InvalidDataException("Control in native text"); if(!char.IsLowSurrogate(s[i])) n++; }
        if(n>max) throw new InvalidDataException("Native text capacity");
    }
    static void BoardIo(byte[] pins,byte[] aux) {
        if(pins.Length!=16 || pins[0]!=1 || pins[1]!=0 || pins[2]!=0 || pins[3]!=0 || pins.Skip(10).Any(x=>x!=255)) throw new InvalidDataException("Native pin authority");
        byte[] display=Part(pins,4,6); Array.Sort(display);
        if(!Equal(display,new byte[]{8,9,10,11,12,13}) && display.Any(x=>x!=255)) throw new InvalidDataException("Unsupported display routing");
        if(aux.Length!=12 || aux[0]!=1 || (aux[1]!=2 && aux[1]!=255) || aux[2]>5 || (aux[1]==255 && aux[2]!=0) || !Equal(Part(aux,3,7),new byte[]{255,0,255,0,0,255,255})) throw new InvalidDataException("Native button/reserved layout");
    }
    public static byte[] ValidateTemplate(Dictionary<int,byte[]> r,byte[] f) {
        if(!r.ContainsKey(1) || !r.ContainsKey(7) || !r.ContainsKey(12)) throw new InvalidDataException("Full default records missing");
        byte[] b=r[1];
        if(b.Length!=53 || Encoding.ASCII.GetString(b,0,4)!="NFP1" || Fnv(Part(b,0,49))!=U32(b,49)) throw new InvalidDataException("Native board profile");
        BoardIo(Part(b,4,16),Part(b,20,12)); byte[] wire=Part(b,32,17); bool configured=wire.Any(x=>x!=0);
        HashSet<int> expected=new HashSet<int>(new int[]{1,7,12});if(r.ContainsKey(5))expected.Add(5);
        if(configured) {
            expected.Add(3); byte[] screen=new byte[28]; screen[0]=83;screen[1]=80;screen[2]=1;screen[3]=17;Buffer.BlockCopy(wire,0,screen,4,17);Put32(screen,24,Fnv(Part(screen,0,24)));
            if(!r.ContainsKey(3) || !Equal(r[3],screen)) throw new InvalidDataException("Native screen record mismatch");
        }
        if(!expected.SetEquals(r.Keys)) throw new InvalidDataException("Unit/unknown records in generic Full");
        byte[] marker=new byte[16];Put32(marker,0,0x3153424e);marker[4]=1;marker[5]=(byte)(configured?1:0);Put32(marker,8,U32(f,0x5f018));Put32(marker,12,U32(f,0x5f01c));
        if(!Equal(marker,r[12])) throw new InvalidDataException("First-display/App binding mismatch");
        byte[] u=r[7];
        if(u.Length!=112 || Encoding.ASCII.GetString(u,0,4)!="NUS1" || u[4]!=1 || u[78]!=0 || u[79]!=0 || Fnv(Part(u,0,108))!=U32(u,108) || u[5]>19 || u[25]>24 || u[56]>21 || Part(u,6+u[5],19-u[5]).Any(x=>x!=0) || Part(u,26+u[25],24-u[25]).Any(x=>x!=0) || Part(u,57+u[56],21-u[56]).Any(x=>x!=0) || Part(u,80,16).Any(x=>x!=0) || !Equal(Part(u,96,12),Part(b,20,12))) throw new InvalidDataException("Native default user state");
        if(u[5]>0) { string name=Encoding.ASCII.GetString(u,6,u[5]); if((!name.StartsWith("IKT_") && !name.StartsWith("InkTag_")) || name=="IKT_" || name=="InkTag_" || Part(u,6,u[5]).Any(x=>x<32 || x==127)) throw new InvalidDataException("Native name"); }
        Text(Part(u,26,u[25]),24);
        foreach(int i in new int[]{50,52,54}) if(u[i]>10 || u[i]==8) throw new InvalidDataException("Native gesture action");
        byte[] displayWire=Part(u,57,u[56]);
        if(displayWire.Length<9 || displayWire[0]!=2 || displayWire[1]!=1 || displayWire[2]>1 || (displayWire[3]&~0x11)!=0 || displayWire[8]>12 || displayWire.Length!=9+displayWire[8] || (U32(displayWire,4)==0)!=(displayWire[8]==0)) throw new InvalidDataException("Native default display state");
        Text(Part(displayWire,9,displayWire[8]),4);
        if(r.ContainsKey(5)) {
            byte[] qr=r[5];
            if(qr.Length<9 || qr[0]!=1 || qr[1]!=0 || qr[2]<21 || qr[2]>57 || (qr[2]-21)%4!=0 || U16(qr,3)!=(qr[2]*qr[2]+7)/8 || qr.Length!=9+U16(qr,3) || Crc(Part(qr,9,qr.Length-9))!=U32(qr,5)) throw new InvalidDataException("Native template QR/password state");
            int padding=(qr.Length-9)*8-qr[2]*qr[2];if(padding>0 && (qr[qr.Length-1]&((1<<padding)-1))!=0) throw new InvalidDataException("QR padding");
        }
        if(Part(f,0x4e000,32768).Any(x=>x!=255)) throw new InvalidDataException("Generic Full must not contain old user fonts");
        return wire;
    }
    public static void VerifyNative(byte[] f) {
        if(f.Length!=FlashBytes || U32(f,0xff8)!=0x58000 || U32(f,0xffc)!=0x5e000 || !Equal(Part(f,0x5e000,803),Part(f,0x5f000,803)) || U32(f,0x5f004)!=2 || U32(f,0x5f00c)!=0 || U32(f,0x5f020)!=1 || U32(f,0x5f014)!=0 || U32(f,0x5f02c)!=0 || U32(f,0x5f000)!=Crc(Part(f,0x5f004,88)) || U32(f,0x5f25c)!=Crc(Part(f,0x5f260,195))) throw new InvalidDataException("Native DFU/selectors/pending/CRC");
        uint n=U32(f,0x5f018),crc=U32(f,0x5f01c),sd=U32(f,0x5f034);
        if(n<8 || n>163840 || Crc(Part(f,0x26000,(int)n))!=crc || f[0x5f2a1]!=1 || U32(f,0x5f2a2)!=crc || sd<8 || sd>0x25000 || f[0x5f260]!=1 || U32(f,0x5f261)!=Crc(Part(f,0x1000,(int)sd)) || U32(f,0x3008)!=0x26000 || U16(f,0x300c)!=0x0101) throw new InvalidDataException("Native App/SoftDevice fingerprint");
        int recEnd=0x5e000; while(recEnd>0x58000 && f[recEnd-1]==255) recEnd--;
        if(recEnd<0x58008) throw new InvalidDataException("Missing Recovery payload");
        foreach(int start in new int[]{0x26000,0x58000}) { uint sp=U32(f,start),reset=U32(f,start+4); int end=start==0x26000?0x26000+(int)n:recEnd; if((sp&7)!=0 || sp<=0x20004000 || sp>0x20010000 || (reset&1)==0 || (reset&~1u)<start || (reset&~1u)>=end) throw new InvalidDataException("Native vector range"); }
    }
}
