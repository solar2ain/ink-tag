"""Bounded ARM ELF32 reader for code guards; standard library only (also WSL)."""
import hashlib
import re
import struct

PROFILES = {
    'gr5331_zkc21v': (0x00204000, 0x00220000, 0x40000, 0x20001e00, 0x20018000),
    'gr551x_zkc42v': (0x01004000, 0x01020000, 0x52000, 0x30004000, 0x30020000),
    'gr5513_zkel_internal': (0x01004000, 0x01020000, 0x44000, 0x30004000, 0x30020000),
}
ALIASES = {'gr5331_zkel':'gr5331_zkc21v','gr551x_zkel':'gr551x_zkc42v',
           'gr551x_zkel_internal':'gr5513_zkel_internal'}


def profile(family):
    return PROFILES[ALIASES.get(family,family)]


class Elf:
    def __init__(self, raw):
        self.raw = raw
        if raw[:7] != b'\x7fELF\x01\x01\x01' or len(raw) < 52:
            raise ValueError('Expected little-endian ELF32')
        h = self.unpack('HHIIIIIHHHHHH', 16)
        if h[0] != 2 or h[1] != 40 or h[8] != 32 or h[10] != 40:
            raise ValueError('Expected unstripped linked ARM ELF32')
        self.segments = [self.unpack('8I', h[4]+i*32) for i in range(h[9])]
        self.loads = [s for s in self.segments if s[0] == 1 and s[4]]
        self.sections = [self.unpack('10I', h[5]+i*40) for i in range(h[11])]
        self.symbols = []
        for section in self.sections:
            if section[1] != 2:
                continue
            if section[9] != 16 or section[5] % 16 or section[6] >= len(self.sections):
                raise ValueError('Invalid symbol table')
            strings = self.sections[section[6]]
            names = self.slice(strings[4], strings[5])
            for offset in range(section[4], section[4]+section[5], 16):
                name, value, size, info, _, index = self.unpack('IIIBBH', offset)
                end = names.find(b'\0', name)
                if name >= len(names) or end < 0:
                    raise ValueError('Invalid symbol name')
                label = names[name:end].decode('ascii')
                if label:
                    self.symbols.append(dict(name=label, address=value, bytes=size, type=info & 15, section=index))

    def slice(self, offset, size):
        if offset < 0 or size < 0 or offset+size > len(self.raw):
            raise ValueError('Truncated ELF')
        return self.raw[offset:offset+size]

    def unpack(self, fmt, offset):
        return struct.unpack('<'+fmt, self.slice(offset, struct.calcsize('<'+fmt)))

    def symbol(self, name):
        found = [s for s in self.symbols if re.sub(r'\.lto_priv\.\d+$', '', s['name']) == name]
        if len(found) != 1:
            raise ValueError('Missing or ambiguous protected symbol: '+name)
        return found[0]

    def offset(self, address, size):
        found = [s[1]+address-s[2] for s in self.loads if s[2] <= address and address+size <= s[2]+s[4]]
        if len(found) != 1:
            raise ValueError('Range not uniquely file backed')
        self.slice(found[0], size)
        return found[0]

    def image(self):
        start = min(s[3] for s in self.loads)
        end = max(s[3]+s[4] for s in self.loads)
        if not 0 < end-start <= 0x100000:
            raise ValueError('Unbounded load extent')
        out = bytearray(end-start)
        for s in self.loads:
            out[s[3]-start:s[3]-start+s[4]] = self.slice(s[1], s[4])
        return start, bytes(out)


def inspect(raw, role, family='gr5513_zkel_internal', *, source_profile='pro'):
    if source_profile not in ('pro', 'base'):
        raise ValueError('Explicit free/pro source profile required')
    elf = Elf(raw)
    recovery = role == 'recovery'
    recovery_base,app_base,slot,ram_start,ram_end = profile(family)
    crc_recovery = False
    if recovery:
        policy = [s for s in elf.symbols if s['name'] == 'inktag_recovery_policy_info']
        if policy:
            if len(policy) != 1 or policy[0]['bytes'] != 6 or elf.slice(elf.offset(policy[0]['address'], 6), 6) != b'IKSP\x01\x02':
                raise ValueError('Invalid linked Recovery policy')
            crc_recovery = True
            forbidden = ('sign_verify', 'bl_check_fw_security', 'BOOTLOADER_PUBLIC_KEY_HASH',
                         'rsa_', 'pkc_', 'bypass_sign_', 'public_key_hash')
            for symbol in elf.symbols:
                if symbol['name'].startswith(forbidden):
                    raise ValueError('CRC Recovery retains publisher verification: '+symbol['name'])
    names = ['', '_ram'] if recovery and not crc_recovery else ['']
    table = elf.symbol('inktag_code_digests')
    if table['bytes'] != 32*len(names):
        raise ValueError('Unexpected guard table size')
    position = elf.offset(table['address'], table['bytes'])
    table_load = next(s for s in elf.loads if s[2] <= table['address'] and table['address']+table['bytes'] <= s[2]+s[4])
    if table_load[6] & 2:
        raise ValueError('Digest table must not make the Flash ELF segment writable')
    base = recovery_base if recovery else app_base
    flash_end = app_base if recovery else app_base+slot
    if not base <= table['address'] < table['address']+table['bytes'] <= flash_end:
        raise ValueError('Digest table must be in this image Flash')
    regions = []
    for suffix in names:
        start = elf.symbol('__inktag_guard'+suffix+'_start')['address']
        end = elf.symbol('__inktag_guard'+suffix+'_end')['address']
        low, high = (ram_start,ram_end) if suffix else (base,flash_end)
        if not low <= start < end <= high or end-start > 4096:
            raise ValueError('Invalid executed guard range')
        if start < table['address']+table['bytes'] and table['address'] < end:
            raise ValueError('Self-referencing guard')
        offset = elf.offset(start, end-start)
        load = next(s for s in elf.loads if s[2] <= start and end <= s[2]+s[4])
        physical = load[3]+start-load[2]
        if ((not suffix and physical != start) or
            (suffix and not base <= physical < physical+end-start <= app_base)):
            raise ValueError('Execution/load mapping does not belong to this image')
        functions = [dict(s, address=s['address'] & ~1) for s in elf.symbols
                     if s['type'] == 2 and start <= s['address'] & ~1 < end]
        if any(f['address']+f['bytes'] > end for f in functions):
            raise ValueError('Truncated protected function')
        regions.append(dict(address=start, loadAddress=physical, bytes=end-start, fileOffset=offset,
            sha256=hashlib.sha256(elf.slice(offset,end-start)).hexdigest(), functions=functions))
    required = (['bootloader_app_fw_verify','bootloader_jump_task','inktag_code_measure','inktag_gr_code_sha256','sign_verify','bl_check_fw_security'] if recovery else
        ['parse_and_verify','verify_signature','apply_verified_record','verified_features',
         'inktag_activation_has_feature',
         'inktag_code_measure','inktag_access_brand_should_render','inktag_access_brand_edition','image_brand_edition'])
    if crc_recovery:
        required = ['bootloader_app_fw_verify', 'bootloader_jump_task',
                    'inktag_code_measure', 'inktag_gr_code_sha256',
                    'inktag_gr_app_integrity_verify']
    if not recovery and source_profile == 'base':
        required = ['inktag_code_measure','inktag_access_brand_should_render',
                    'inktag_access_brand_edition','image_brand_edition']
        exact = {'parse_and_verify','verify_signature','apply_verified_record',
                 'verified_features','k_inktag_activation_product_public_key',
                 'k_activation_domain'}
        prefixes = ('inktag_gr_find', 'inktag_gr_p224', 'inktag_findmy_',
                    'inktag_gr_nfc_', 'inktag_nfc_', 'inktag_gt_',
                    'gr_clock_partial_', 'process_nfc_', 'nfc_command_',
                    's_nfc_', 's_gt_', 'inktag_owner_auth_')
        for symbol in elf.symbols:
            name = symbol['name'].split('.')[0]
            if name in exact or name.startswith(prefixes):
                raise ValueError('base image contains Pro implementation/state: '+symbol['name'])
    # CLOCK_PARTIAL=0 is a supported build control: an omitted backend must not
    # make an otherwise legal App unbuildable. If linked, its actual gate is required.
    if not recovery and any(s['name'].split('.')[0]=='gr_clock_partial_available' for s in elf.symbols):
        required.append('gr_clock_partial_available')
    for name in required:
        function = elf.symbol(name)
        address = function['address'] & ~1
        expected = regions[1] if name == 'bl_check_fw_security' else regions[0]
        if function['type'] != 2 or not function['bytes'] or not expected['address'] <= address < address+function['bytes'] <= expected['address']+expected['bytes']:
            raise ValueError('Unmeasured executed critical function: '+name)
        if any(s['name'].startswith(name+'.') and s['type']==2 and re.sub(r'\.lto_priv\.\d+$','',s['name']) != name for s in elf.symbols):
            raise ValueError('Cloned protected function: '+name)
    if not recovery and source_profile == 'pro':
        # These wrappers may inline only calls to the noipa canonical predicate.
        # Every remaining out-of-line implementation/clone must be measured.
        for name in ('inktag_activation_is_pro','inktag_access_is_pro'):
            for function in elf.symbols:
                if function['type'] != 2 or function['name'].split('.')[0] != name:continue
                address=function['address'] & ~1
                if not regions[0]['address'] <= address < address+function['bytes'] <= regions[0]['address']+regions[0]['bytes']:
                    raise ValueError('Unmeasured Pro query wrapper: '+name)
        key = elf.symbol('k_inktag_activation_product_public_key')
        if key['bytes'] != 64 or not regions[0]['address'] <= key['address'] < key['address']+64 <= regions[0]['address']+regions[0]['bytes']:
            raise ValueError('Unmeasured product public key')
    if not recovery:
        rules = elf.symbol('s_feature_rules')
        if not rules['bytes'] or rules['bytes'] % 8 or not regions[0]['address'] <= rules['address'] < rules['address']+rules['bytes'] <= regions[0]['address']+regions[0]['bytes']:
            raise ValueError('Unmeasured canonical product policy')
    start, image = elf.image()
    limit = app_base-recovery_base if recovery else slot-904
    if start != base or len(image) > limit:
        raise ValueError('Unchanged image limit exceeded')
    expected = b''.join(bytes.fromhex(r['sha256']) for r in regions)
    if any(bytes.fromhex(r['sha256']) == bytes(32) for r in regions):
        raise ValueError('All-zero digest cannot distinguish the unsealed placeholder')
    return elf, position, expected, regions


def verify(raw, role, binary=None, family='gr5513_zkel_internal', *, source_profile='pro'):
    elf, position, expected, regions = inspect(raw, role, family, source_profile=source_profile)
    if raw[position:position+len(expected)] != expected:
        raise ValueError('Stale/unsealed code fingerprints')
    start, image = elf.image()
    if binary is not None and image != binary:
        raise ValueError('ELF/BIN mismatch after fingerprint generation')
    return dict(role=role,family=family,sourceProfile=source_profile,rawBytes=len(image),rawSha256=hashlib.sha256(image).hexdigest(),
        tableAddress=elf.symbol('inktag_code_digests')['address'],tableBytes=len(expected),
        measuredBytes=sum(r['bytes'] for r in regions),regions=regions)
