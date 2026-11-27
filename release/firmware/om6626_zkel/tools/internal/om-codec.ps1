# Ordinary OM persistent formats. Native SDK replay remains the development oracle.
if(-not ('InkTag.OmCodec' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
namespace InkTag {
public static class OmCodec {
 public static byte[] Slice(byte[] b,int a,int n){if(a<0||n<0||a>b.Length-n)throw new ArgumentException("byte span");byte[] r=new byte[n];Buffer.BlockCopy(b,a,r,0,n);return r;}
 public static byte[] Fill(int n){byte[] r=new byte[n];for(int i=0;i<n;i++)r[i]=255;return r;}
 public static bool Equal(byte[] a,byte[] b){if(a==null||b==null||a.Length!=b.Length)return false;int x=0;for(int i=0;i<a.Length;i++)x|=a[i]^b[i];return x==0;}
 public static string Sha(byte[] b){using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(b)).Replace("-","").ToLowerInvariant();}
 public static int Find(byte[] b,byte[] p){for(int i=0;i<=b.Length-p.Length;i++){bool same=true;for(int j=0;j<p.Length;j++)if(b[i+j]!=p[j]){same=false;break;}if(same)return i;}return -1;}
 public static int Count(byte[] b,byte[] p){int n=0;for(int i=0;i<=b.Length-p.Length;i++){bool same=true;for(int j=0;j<p.Length;j++)if(b[i+j]!=p[j]){same=false;break;}if(same)n++;}return n;}
 public static uint Crc32(byte[] b){uint c=0xffffffff;foreach(byte v in b){c^=v;for(int k=0;k<8;k++)c=(c&1)!=0?(c>>1)^0xedb88320:c>>1;}return c^0xffffffff;}
 public static uint Fnv(byte[] b){uint c=2166136261;foreach(byte v in b)c=unchecked((c^v)*16777619);return c;}
 public static bool Descriptor(byte[] b,int at,uint board,uint layout,uint? release){
  if(at<268||at>b.Length-40||b[at]!=73||b[at+1]!=75||b[at+2]!=67||b[at+3]!=73||b[at+4]!=1||b[at+5]!=40||b[at+6]!=0||b[at+7]!=0)return false;
  return BitConverter.ToUInt32(b,at+8)==at&&BitConverter.ToUInt32(b,at+16)==board&&BitConverter.ToUInt32(b,at+20)==layout&&BitConverter.ToUInt32(b,at+24)==1&&(!release.HasValue||BitConverter.ToUInt32(b,at+28)==release.Value)&&BitConverter.ToUInt32(b,at+32)==0&&BitConverter.ToUInt32(b,at+36)==Crc32(Slice(b,at,36))&&BitConverter.ToUInt32(b,at+12)==Crc32(Slice(b,0,at));
 }
 public static bool Integrity(byte[] b,uint board,uint layout,uint release){if(!Descriptor(b,b.Length-40,board,layout,release))return false;for(int at=268;at<b.Length-40;at++)if(b[at]==73&&Descriptor(b,at,board,layout,null))return false;return true;}
 public static bool CanonicalSignature(byte[] signature){if(signature.Length!=64)return false;string r=BitConverter.ToString(Slice(signature,0,32)).Replace("-",""),s=BitConverter.ToString(Slice(signature,32,32)).Replace("-","");return r.Trim('0').Length>0&&s.Trim('0').Length>0&&String.CompareOrdinal(r,"FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551")<0&&String.CompareOrdinal(s,"7FFFFFFF800000007FFFFFFFFFFFFFFFDE737D56D38BCF4279DCE5617E3192A8")<=0;}
 public static byte Crc7(byte[] b){int c=0;foreach(byte v in b){int x=(c<<1)^v;for(int k=0;k<8;k++)x=(x&128)!=0?(x<<1)^0x12:x<<1;c=(x>>1)&127;}return (byte)c;}
 public static ushort Crc16(byte[] b){int c=0;foreach(byte v in b){c^=v<<8;for(int k=0;k<8;k++)c=(c&32768)!=0?(c<<1)^0x1021:c<<1;c&=65535;}return (ushort)c;}
 public static byte[] Record(int tag,byte[] value){if(tag<1||tag>254||value==null||value.Length>504)throw new ArgumentException("native record");byte[] b=new byte[8+value.Length];b[1]=(byte)tag;Buffer.BlockCopy(BitConverter.GetBytes((ushort)value.Length),0,b,2,2);b[4]=1;Buffer.BlockCopy(value,0,b,8,value.Length);b[0]=Crc7(Slice(b,1,b.Length-1));return b;}
 public static byte[] Pair(int[] tags,byte[][] values){
  if(tags.Length!=values.Length)throw new ArgumentException("native records");byte[] b=Fill(8192);Buffer.BlockCopy(new byte[]{78,86,68,83},0,b,0,4);Buffer.BlockCopy(new byte[]{66,75,85,80},0,b,4096,4);
  var seen=new HashSet<int>();var records=new List<byte[]>();int total=0,padded=256;
  for(int i=0;i<tags.Length;i++){if(!seen.Add(tags[i]))throw new ArgumentException("duplicate native record");byte[] r=Record(tags[i],values[i]);records.Add(r);total+=r.Length;padded+=((r.Length+255)/256)*256;}
  if(padded<=4096){int at=256;foreach(byte[] v in records){Buffer.BlockCopy(v,0,b,at,v.Length);at+=((v.Length+255)/256)*256;}}
  else {if(total>3840)throw new ArgumentException("native factory records exceed NVDS capacity");int at=4352;foreach(byte[] v in records){Buffer.BlockCopy(v,0,b,at,v.Length);at+=v.Length;}}
  return b;
 }
 public static Dictionary<int,byte[]> ReadPair(byte[] pair){
  var result=new Dictionary<int,byte[]>();if(pair==null||pair.Length!=8192)return result;
  // Packed backup first; latest live record overrides even when corrupt/deleted.
  foreach(bool packed in new bool[]{true,false}) {
   int origin=-1;for(int a=0;a<8192;a+=4096){byte[] m=Slice(pair,a,4);if(Equal(m,packed?new byte[]{66,75,85,80}:new byte[]{78,86,68,83})){if(origin!=-1)return new Dictionary<int,byte[]>();origin=a;}}
   if(origin<0)continue;for(int a=origin+4;a<origin+256;a++)if(pair[a]!=255)return new Dictionary<int,byte[]>();
   int cursor=256;while(cursor<4096){int at=origin+cursor;bool empty=true;for(int a=at;a<origin+4096;a++)if(pair[a]!=255){empty=false;break;}if(empty)break;
    if(packed&&pair[at]==255){int next=((cursor+255)/256)*256;if(next==cursor)next+=256;for(int a=at;a<origin+next;a++)if(pair[a]!=255)return new Dictionary<int,byte[]>();cursor=next;continue;}
    if(cursor>4088)return new Dictionary<int,byte[]>();int tag=pair[at+1],n=BitConverter.ToUInt16(pair,at+2);uint gen=BitConverter.ToUInt32(pair,at+4);
    if(tag<1||tag>254||n>504||gen==0||cursor+8+n>4096)return new Dictionary<int,byte[]>();
    int end=cursor+8+n,nextEnd=packed?end:((end+255)/256)*256;if(nextEnd>4096)return new Dictionary<int,byte[]>();
    bool padding=true;for(int a=origin+end;a<origin+nextEnd;a++)padding&=pair[a]==255;
    result[tag]=padding&&n>0&&Crc7(Slice(pair,at+1,7+n))==pair[at]?Slice(pair,at+8,n):null;cursor=nextEnd;
   }
  }return result;
 }
 public static bool Address(byte[] b){if(b==null||b.Length!=6)return false;ulong x=0;for(int i=5;i>=0;i--)x=(x<<8)|b[i];x&=0x3fffffffffffUL;return x!=0&&x!=0x3fffffffffffUL;}
 public static byte[] ZkcUidAddress(uint jedec, byte[] uid) {
  // Mirrors inktag_om_address.c and inktag_ble_address_derive_uid_compact.
  int length = ((jedec >> 16) == 0xe0 || (jedec >> 16) == 0x68) ? 8 : 16;
  byte[] root = new byte[] {2, 1, (byte)(length + 3), (byte)(jedec >> 16), (byte)(jedec >> 8), (byte)jedec};
  ulong hash = 0x6f9668559e352aedUL;
  foreach (byte value in root) hash = unchecked((hash ^ value) * 0x100000001b3UL);
  for (int i = 0; i < length; i++) hash = unchecked((hash ^ uid[i]) * 0x100000001b3UL);
  byte[] address = Slice(BitConverter.GetBytes(hash ^ (hash >> 46)), 0, 6);
  address[5] = (byte)((address[5] & 0x3f) | 0xc0);
  return address;
 }
 public static byte[] NfcLinkRecord(string url) {
  byte[] suffix = Encoding.ASCII.GetBytes(url.Substring("https://".Length));
  // Common binding v1, no tap action, public HTTPS short URI.
  return Join(new byte[] {1, 0, 0, (byte)(suffix.Length + 5),
      0xd1, 1, (byte)(suffix.Length + 1), 0x55, 4}, suffix);
 }
 public static byte[] DeviceQrRecord(string url) {
  byte[] pixels = OmDeviceQr.Encode(url);
  // Existing password-OFF wire record in the OM OQR1 envelope.
  byte[] wire = Join(new byte[] {1, 0, OmDeviceQr.Size},
      BitConverter.GetBytes((ushort)pixels.Length), BitConverter.GetBytes(Crc32(pixels)), pixels);
  return Join(new byte[] {79, 81, 82, 49, 1, 0, 0, 0},
      BitConverter.GetBytes((ushort)wire.Length), BitConverter.GetBytes(Crc32(wire)), wire);
 }
 private static byte[] Join(params byte[][] parts) {
  using (var stream = new MemoryStream()) {
   foreach (byte[] part in parts) stream.Write(part, 0, part.Length);
   return stream.ToArray();
  }
 }
 public static byte[] ExpectedBoot(byte[] before,byte[] recovery,byte[] app){
  if(before.Length!=16384||before[0x1182]!=0x60||before[0x11fe]!=0x55||before[0x11ff]!=0xaa)throw new ArgumentException("unsupported manufacturer ROM boot metadata at 0x1182/0x11FE; not optional user state");
  int recPages=(recovery.Length+4095)/4096,appPages=(app.Length+4095)/4096;byte[] stream=Fill((recPages+appPages)*4096);Buffer.BlockCopy(recovery,0,stream,0,recovery.Length);Buffer.BlockCopy(app,0,stream,recPages*4096,app.Length);
  byte[] entry=new byte[10];Buffer.BlockCopy(BitConverter.GetBytes(Crc16(stream)),0,entry,0,2);Buffer.BlockCopy(BitConverter.GetBytes(4U),0,entry,2,4);Buffer.BlockCopy(BitConverter.GetBytes((uint)stream.Length),0,entry,6,4);
  byte[] result=(byte[])before.Clone();if(!Equal(entry,Slice(before,0x1184,10))){Buffer.BlockCopy(before,0x1000,result,0x2000,512);result[0x21fe]=result[0x21ff]=0;Buffer.BlockCopy(entry,0,result,0x1184,10);}return result;
 }
 public static byte[] ScreenRecord(byte[] wire){if(wire.Length!=17)throw new ArgumentException("screen wire");byte[] b=new byte[28];b[0]=83;b[1]=80;b[2]=1;b[3]=17;Buffer.BlockCopy(wire,0,b,4,17);Buffer.BlockCopy(BitConverter.GetBytes(Fnv(Slice(b,0,24))),0,b,24,4);return b;}
 public static byte[] FactoryRecord(byte board,byte[] pins){byte[] b=new byte[28];Buffer.BlockCopy(new byte[]{79,77,70,49,1,board,1,0},0,b,0,8);Buffer.BlockCopy(pins,0,b,8,16);Buffer.BlockCopy(BitConverter.GetBytes(Fnv(Slice(b,0,24))),0,b,24,4);return b;}
 public static byte[] LegacyPins(byte[] wire){byte[] b=new byte[19];Buffer.BlockCopy(new byte[]{71,84,75,73,1},0,b,0,5);Buffer.BlockCopy(wire,4,b,5,6);for(int i=11;i<15;i++)b[i]=255;b[15]=wire[10];b[16]=b[17]=1;byte c=1;for(int i=5;i<18;i++)c^=b[i];b[18]=c;return b;}
 public static byte[] NativeMarker(uint layout){
  byte[] b=new byte[32];using(var m=new MemoryStream(b))using(var w=new BinaryWriter(m)){w.Write(new byte[]{73,82,74,49});w.Write(layout);w.Write(1U);w.Write(0U);w.Write((byte)7);w.Write((byte)1);w.Write((ushort)65535);w.Write(1U);w.Write(0xffffffffU);}Buffer.BlockCopy(BitConverter.GetBytes(Crc32(Slice(b,0,28))),0,b,28,4);return b;
 }
 public static bool P256(byte[] pub,byte[] message,byte[] signature){
  if(pub.Length!=65||pub[0]!=4||signature.Length!=64)throw new ArgumentException("P-256 public input");byte[] blob=new byte[72];Buffer.BlockCopy(BitConverter.GetBytes(0x31534345),0,blob,0,4);Buffer.BlockCopy(BitConverter.GetBytes(32),0,blob,4,4);Buffer.BlockCopy(pub,1,blob,8,64);
  using(var k=CngKey.Import(blob,CngKeyBlobFormat.EccPublicBlob))using(var e=new ECDsaCng(k)){e.HashAlgorithm=CngAlgorithm.Sha256;return e.VerifyData(message,signature);}
 }
}

// Version 4-M byte-mode QR for the fixed-length OM factory URL.
// Two 32-byte data blocks, 18 check bytes each, mask 0.
internal sealed class OmDeviceQr {
 public const byte Size = 33;
 private const int DataPerBlock = 32;
 private static readonly byte[] Generator =
     {239, 251, 183, 113, 149, 175, 199, 215, 240, 220, 73, 82, 173, 75, 32, 67, 217, 146};
 private readonly bool[,] modules = new bool[Size,Size];
 private readonly bool[,] reserved = new bool[Size,Size];

 public static byte[] Encode(string url) {
  byte[] payload = Encoding.ASCII.GetBytes(url);
  byte[] data = new byte[2 * DataPerBlock];
  if (payload.Length > data.Length - 2) throw new ArgumentException("OM device URL exceeds QR capacity");
  // Four mode bits, eight count bits, payload, four zero terminator bits.
  data[0] = (byte)(0x40 | (payload.Length >> 4));
  data[1] = (byte)(payload.Length << 4);
  for (int i = 0; i < payload.Length; i++) {
   data[i + 1] |= (byte)(payload[i] >> 4);
   data[i + 2] = (byte)(payload[i] << 4);
  }
  for (int i = payload.Length + 2; i < data.Length; i++)
   data[i] = ((i - payload.Length) & 1) == 0 ? (byte)0xec : (byte)0x11;

  byte[] left = CheckBytes(data, 0), right = CheckBytes(data, DataPerBlock);
  byte[] words = new byte[data.Length + left.Length + right.Length];
  for (int i = 0; i < DataPerBlock; i++) {
   words[2*i] = data[i];
   words[2*i+1] = data[i+DataPerBlock];
  }
  for (int i = 0; i < left.Length; i++) {
   words[data.Length+2*i] = left[i];
   words[data.Length+2*i+1] = right[i];
  }
  var qr = new OmDeviceQr();
  qr.DrawPatterns();
  qr.DrawData(words);
  return qr.Pack();
 }

 private static byte[] CheckBytes(byte[] data, int start) {
  byte[] check = new byte[Generator.Length];
  for (int i = start; i < start + DataPerBlock; i++) {
   int factor = data[i] ^ check[0];
   Array.Copy(check, 1, check, 0, check.Length - 1);
   check[check.Length - 1] = 0;
   for (int j = 0; j < check.Length; j++) check[j] ^= Multiply(Generator[j], factor);
  }
  return check;
 }

 private static byte Multiply(int a, int b) {
  int product = 0;
  while (b != 0) {
   if ((b & 1) != 0) product ^= a;
   b >>= 1;
   a <<= 1;
   if ((a & 0x100) != 0) a ^= 0x11d;
  }
  return (byte)product;
 }

 private void SetPattern(int x, int y, bool dark) {
  if (x < 0 || y < 0 || x >= Size || y >= Size) return;
  modules[y,x] = dark;
  reserved[y,x] = true;
 }

 private void DrawFinder(int centerX, int centerY) {
  for (int y = -4; y <= 4; y++) {
   for (int x = -4; x <= 4; x++) {
    int distance = Math.Max(Math.Abs(x), Math.Abs(y));
    SetPattern(centerX+x, centerY+y, distance != 2 && distance != 4);
   }
  }
 }

 private void DrawPatterns() {
  for (int i = 8; i < Size - 8; i++) {
   SetPattern(i, 6, i % 2 == 0);
   SetPattern(6, i, i % 2 == 0);
  }
  DrawFinder(3, 3);
  DrawFinder(Size-4, 3);
  DrawFinder(3, Size-4);
  for (int y = -2; y <= 2; y++)
   for (int x = -2; x <= 2; x++)
    SetPattern(26+x, 26+y, Math.Max(Math.Abs(x), Math.Abs(y)) != 1);

  const int format = 0x5412;  // Error correction M, mask 0 (BCH-encoded).
  for (int i = 0; i <= 5; i++) SetPattern(8, i, ((format >> i) & 1) != 0);
  SetPattern(8, 7, ((format >> 6) & 1) != 0);
  SetPattern(8, 8, ((format >> 7) & 1) != 0);
  SetPattern(7, 8, ((format >> 8) & 1) != 0);
  for (int i = 9; i < 15; i++) SetPattern(14-i, 8, ((format >> i) & 1) != 0);
  for (int i = 0; i < 8; i++) SetPattern(Size-1-i, 8, ((format >> i) & 1) != 0);
  for (int i = 8; i < 15; i++) SetPattern(8, Size-15+i, ((format >> i) & 1) != 0);
  SetPattern(8, Size-8, true);
 }

 private void DrawData(byte[] words) {
  int bit = 0;
  bool upwards = true;
  for (int right = Size - 1; right > 0; right -= 2) {
   if (right == 6) right--;
   for (int i = 0; i < Size; i++) {
    int y = upwards ? Size-1-i : i;
    for (int x = right; x >= right-1; x--) {
     if (reserved[y,x]) continue;
     bool dark = bit < words.Length * 8 && (words[bit/8] & (0x80 >> (bit%8))) != 0;
     modules[y,x] = dark ^ ((x+y) % 2 == 0);
     bit++;
    }
   }
   upwards = !upwards;
  }
 }

 private byte[] Pack() {
  byte[] result = new byte[(Size * Size + 7) / 8];
  for (int i = 0; i < Size * Size; i++)
   if (modules[i/Size,i%Size]) result[i/8] |= (byte)(0x80 >> (i%8));
  return result;
 }
}
}
'@
}
