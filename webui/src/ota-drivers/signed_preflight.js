/* Production public keys only. Native DA/Nordic offline admission, no BLE. */
(function (global) {
    'use strict';
    const daKey = '044b7e9a9e6d38e0caf507cb25dfbf859f3753b97e5b7cd21bd20d156f07a14672d71b4e32a19307f91654a45d4ce165a4207bab50443aee65e6ea6cc02911bbfa';
    const nrfKeyLE = '0edf3d346f2be4a0a641930c3bba0f21fff2bf1913ed68d1a81b47a87e0bf9aec2a2697fc77f10daba686704f3323fed849591ec9539b288ae5be98f221fe7d1';
    const hex = value => Uint8Array.from(value.match(/../g), p => parseInt(p, 16));
    const equal = (a,b) => a.length === b.length && a.every((v,i) => v === b[i]);
    const join = (...parts) => { const out = new Uint8Array(parts.reduce((n,p) => n+p.length,0)); let at=0; for (const p of parts) {out.set(p,at);at+=p.length;} return out; };
    const fail = text => { throw new Error(text); };
    async function verify(key, signature, message) {
        const subtle = global.crypto && global.crypto.subtle;
        if (!subtle) fail('当前浏览器无法验证固件签名，请使用支持 Web Crypto 的浏览器');
        const imported = await subtle.importKey('raw', key, {name:'ECDSA',namedCurve:'P-256'}, false, ['verify']);
        if (!await subtle.verify({name:'ECDSA',hash:'SHA-256'},imported,signature,message)) fail('固件发布者签名验证失败');
    }
    function fields(data) {
        if (!(data instanceof Uint8Array) || data.length > 512) fail('Nordic INIT 大小无效');
        let at=0; const result=new Map();
        const uint=()=>{let n=0;for(let shift=0;shift<35;shift+=7){if(at>=data.length)fail('截断的 Nordic INIT');const b=data[at++];n+=(b&127)*2**shift;if(!(b&128)){if(n>0xffffffff)fail('Nordic INIT 溢出');return n;}}fail('Nordic INIT varint 无效');};
        while(at<data.length){const tag=uint(), key=Math.floor(tag/8), wire=tag&7;if(!key||result.has(key)||![0,2].includes(wire))fail('Nordic INIT 字段不唯一');let value=uint();if(wire===2){if(value>data.length-at)fail('截断的 Nordic INIT');const size=value;value=data.slice(at,at+size);at+=size;}result.set(key,value);}
        return result;
    }
    const keys=(m,expected)=>m.size===expected.length&&expected.every(k=>m.has(k));
    async function inspectNordic(firmware, dat) {
        const packet=fields(dat);let signed=null,command;
        if(keys(packet,[2])) {
            signed=fields(packet.get(2));if(!keys(signed,[1,2,3])||signed.get(2)!==0||signed.get(3).length!==64)fail('需要 Nordic P256 签名');
            command=fields(signed.get(1));
        } else if(keys(packet,[1])) command=fields(packet.get(1));
        else fail('Nordic 包必须恰好包含一个签名或未签名命令');
        if(!keys(command,[1,2])||command.get(1)!==1)fail('需要 Nordic INIT 命令');
        const init=fields(command.get(2)), hash=fields(init.get(8)), hw=init.get(2), encoded=init.get(1);
        const profiles={52:['aesl22_nrf','N1V1',69632,[0xa6,2]],56:['nrf52832_priceetag','N3V1',163840,[0x81,2]]};
        const profile=profiles[hw];if(!profile)fail('Nordic HW／布局不受支持');
        const digest=new Uint8Array(await global.crypto.subtle.digest('SHA-256',firmware));digest.reverse();
        if([...init.keys()].some(k=>k<1||k>10)||(init.get(4)||0)!==0||init.get(5)||init.get(6)||init.get(9)||firmware.length<8||firmware.length>profile[2]||init.get(7)!==firmware.length||!equal(init.get(3)||[],profile[3])||!keys(hash,[1,2])||hash.get(1)!==3||!equal(hash.get(2),digest)||!Number.isInteger(encoded)||Math.floor(encoded/65536)!==1||!(encoded%65536))fail('Nordic 固件 HW／epoch／哈希／大小不匹配');
        const boot=fields(init.get(10));if(!keys(boot,[1,2])||boot.get(1)!==1||boot.get(2).length!==0)fail('Nordic 启动校验格式无效');
        if(signed) {
            const rawKey=hex(nrfKeyLE),key=join(new Uint8Array([4]),rawKey.slice(0,32).reverse(),rawKey.slice(32).reverse()),sig=signed.get(3);
            await verify(key,join(sig.slice(0,32).reverse(),sig.slice(32).reverse()),command.get(2));
        }
        return {publisherVerified:!!signed,signatureStatus:signed?'verified':'unsigned',board:profile[0],layout:profile[1],hwVersion:hw,compatibilityEpoch:1,releaseVersion:encoded};
    }
    async function nordic(firmware, dat) {
        const result=await inspectNordic(firmware,dat);
        if(!result.publisherVerified)fail('需要已签名 Nordic INIT');
        return result;
    }
    async function inspectDialog(image) {
        if(!(image instanceof Uint8Array)||image.length<=160||image.length>64+0x17600||!equal(image.slice(0,4),[0x70,0x51,0xaa,0xff]))fail('DA14585 固件格式／大小无效');
        const v=new DataView(image.buffer,image.byteOffset,image.byteLength),at=image.length-96,u=offset=>v.getUint32(offset,true),raw=image.slice(64,at),prefix=image.slice(at,at+32),sig=image.slice(at+32);
        let crc=0xffffffff;for(const byte of image.slice(64)){crc^=byte;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}if(((~crc)>>>0)!==u(8))fail('DA14585 原生 CRC 不匹配');
        const signed=u(at)===0x31534b49;
        if(u(4)!==image.length-64||u(at)!==(signed?0x31534b49:0x31554b49)||image[at+4]!==1||image[at+5]!== (signed?1:0)||v.getUint16(at+6,true)!==96||u(at+8)!==raw.length||!raw.length||u(at+12)!==1||u(at+16)!==0x42584144||u(at+20)!==0x31564144||u(at+24)!==(signed?0x314b4144:0x31554144)||u(40)!==0x31504b49||[8,12,16,20,24].some((n,i)=>u(44+i*4)!==u(at+n)))fail('DA14585 布局／epoch／上下文不匹配');
        const version=new TextDecoder('ascii',{fatal:true}).decode(image.slice(12,28)).split('\0')[0];
        if(!/^\d+\.\d+\.\d+\.\d+$/.test(version))fail('DA14585 版本格式无效');
        const parts=version.split('.').map(Number);if(parts[0]*10000+parts[1]*100+parts[2]!==u(at+28))fail('DA14585 版本与签名不匹配');
        if(signed) {
            const order=BigInt('0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551'),number=b=>BigInt('0x'+Array.from(b,x=>x.toString(16).padStart(2,'0')).join(''));
            if(number(sig.slice(0,32))<=0n||number(sig.slice(0,32))>=order||number(sig.slice(32))<=0n||number(sig.slice(32))>order/2n)fail('DA14585 签名编码无效');
            await verify(hex(daKey),sig,join(raw,prefix));
        } else {
            const digest=new Uint8Array(await global.crypto.subtle.digest('SHA-256',join(raw,prefix)));
            if(!equal(sig,join(digest,new Uint8Array(32))))fail('DA14585 未签名固件哈希／格式不匹配');
        }
        return {publisherVerified:signed,signatureStatus:signed?'verified':'unsigned',board:'da14585_aesl',layout:'DAV1',compatibilityEpoch:1,releaseVersion:u(at+28)};
    }
    async function dialog(image) {
        const result=await inspectDialog(image);
        if(!result.publisherVerified)fail('需要 DA14585 发布者签名');
        return result;
    }
    function recoveryPolicy(transport, data) {
        if(transport==='nordic'&&equal(data,[0xc1]))return 'optional';
        if(transport==='nordic'&&equal(data,[0xe1]))return 'crc-only';
        if(transport==='dialog'&&equal(data,[13,73,75,1,1,83,80,1,0]))return 'optional';
        if(transport==='dialog'&&equal(data,[13,73,75,1,1,83,80,1,2]))return 'crc-only';
        return 'required';
    }
    function recoverySafeResume(transport, data) {
        if(transport==='nordic')return equal(data,[0x81])||equal(data,[0xc1])||equal(data,[0xe1]);
        if(transport==='dialog')return equal(data,[13,73,75,1,1])||equal(data,[13,73,75,1,1,83,80,1,0])||equal(data,[13,73,75,1,1,83,80,1,1])||equal(data,[13,73,75,1,1,83,80,1,2]);
        return false;
    }
    function requireRecoverySignature(info, transport, data) {
        if(info.publisherVerified===true)return;
        const targets={nordic:['aesl22_nrf','nrf52832_priceetag'],dialog:['da14585_aesl']}[transport]||[];
        const policy=recoveryPolicy(transport,data);
        const allowed=policy==='crc-only'||(policy==='optional'&&['aesl22_nrf','da14585_aesl'].includes(info.board));
        if(!targets.includes(info.board)||info.signatureStatus!=='unsigned'||!allowed)
            fail('当前 Recovery 要求发布者签名，未发送未签名固件');
    }
    global.InkTagSignedOta={nordic,dialog,inspectNordic,inspectDialog,recoveryPolicy,recoverySafeResume,requireRecoverySignature};
})(window);
