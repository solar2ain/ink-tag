"""Pure MCU Full image preparation, not OTA rollback admission.

An explicitly confirmed Full resets user data but preserves a valid same-unit
Pro record and necessary native identity/vendor data. Readable but unusable old
records warn and initialize defaults/Free, without claiming proven absence.
Physical capture failures and incoming-image protections remain mandatory.
The human excludes GR5513's separate external image Flash from Full clearing.
"""
import hashlib,struct,zlib
import goodix_host_nvds as nvds
import goodix_release_seed as seed
import goodix_activation_retention as activation
from goodix_offline.generated_release_v1 import (
    RECOVERY_JOURNAL_MAGIC,LAYOUT_GR5331,LAYOUT_GR551X,LAYOUT_GR5513_INTERNAL,
)

GEOMETRY={
 'GR5331':dict(bytes=0x80000,journal=0x68000,user=0x60000,nvds=0x7e000,marker=b'G3V1'),
 'GR5513':dict(bytes=0x80000,journal=0x7a000,user=0x72000,nvds=0x7c000,marker=b'G5V1'),
 'GR5515':dict(bytes=0x100000,journal=0x7a000,user=0x72000,nvds=0xfc000,marker=b'G5V1'),
}
PRODUCT=set(range(0x5201,0x5213))


def geometry(chip,family=None):
    nvds.extent(chip,family)  # rejects unknown and mixed tuples
    if family=='gr5513_zkel_internal':
        return dict(bytes=0x80000,journal=0x6c000,user=0x64000,nvds=0x7e000,marker=b'GIV1')
    return GEOMETRY[chip]

def sha(data):return hashlib.sha256(data).hexdigest()
def profile(image):return image[0xf00:0xf0c]+image[0xf20:0xf60]


def preservation_extent(chip,family=None):
    """Full reads the existing GR5513 vendor tail, not a target-layout guess.

    GIV1 still writes/runs with 8 KiB. Reading the 16 KiB superset before an
    explicit Full also covers readable records left by the earlier layout.
    This is not a new runtime layout, installed-image gate or archive input.
    """
    target=nvds.extent(chip,family)
    return nvds.extent(chip) if family=='gr5513_zkel_internal' else target


def require_backend(chip):
    nvds.require(chip in GEOMETRY,'Unknown exact Full MCU; no hardware accessed')


def initial_journal(chip,epoch,family=None):
    nvds.require(chip in GEOMETRY and type(epoch) is int and 0<=epoch<=0xffff,
                 'Invalid signed candidate Recovery state')
    g=geometry(chip,family)
    layout=int.from_bytes(g['marker'],'little')
    # Same native common-C serializer ABI as the qualified host seed oracle.
    # The selected signed candidate, never an old device floor, supplies epoch.
    body=struct.pack('<4IBBH2I',RECOVERY_JOURNAL_MAGIC,layout,1,1,3,1,
                     0xffff,epoch,0xffffffff)
    return (body+struct.pack('<I',zlib.crc32(body))).ljust(8192,b'\xff')


def _cleared(reason):
    return dict(format='unrecognized-cleared',identity=None,vendor=(),
                activation=None,activationAbsent=False,activationUnavailable=True,
                identitySource='factory-default',reason=reason,
                warnings=[reason+'; old identity/vendor/Pro information cannot be retained. Initializing defaults/Free.'])


def _full_records(data,chip):
    """Complete contiguous data log, with the last GC sector still erased.

    GR551x can append records across data sectors. Do not mistake the second
    data sector for GC. No resync, duplicate choice, partial salvage or GC replay.
    The ordinary diagnostic/OTA reader retains its stricter one-sector policy.
    """
    limit=len(data)-4096
    nvds.require(data[limit:]==b'\xff'*4096,'Nonempty GC reservation')
    if data==b'\xff'*len(data):return {},False
    nvds.require(data.startswith(nvds.MAGIC),'Unrecognized old NVDS format')
    results=[];errors=[]
    for alignment in ((1,) if chip=='GR5331' else (1,4)):
        try:results.append(nvds._linear_records(data,alignment,limit))
        except ValueError as error:errors.append(error)
    if not results:raise errors[0]
    nvds.require(all(r==results[0] for r in results),'Ambiguous old native framing')
    return results[0]


def read_prepared_identity(data,chip,family=None):
    """Strict Full-seed roundtrip; never an old-record/defaults fallback."""
    nvds.require(len(data)==nvds.extent(chip,family)[1], 'Incomplete prepared NVDS reservation')
    limit=nvds.full_data_limit(chip,family)
    nvds.require(data[limit:]==b'\xff'*(len(data)-limit), 'Prepared NVDS exceeds native data capacity')
    nvds.require(data.startswith(nvds.MAGIC), 'Unrecognized prepared NVDS format')
    found,deleted=nvds._linear_records(data,1,limit)
    nvds.require(not deleted and all(0<len(value)<=4076 for value in found.values()),
                 'Invalid prepared Full NVDS record')
    return nvds.identity_from_records(found,chip)


def review_current(chip,current_nvds,family=None):
    """Review only for an already-confirmed destructive Full.

    Never salvage a prefix of a damaged log. A six-byte legacy address uses
    the native public default only for Full; no historic type is inferred.
    An unrecognized old format is cleared, not reported as proven absence.
    Transport/short-read exceptions stay outside this bounded pure decoder.
    """
    nvds.require(chip in GEOMETRY and type(current_nvds) is bytes and
                 len(current_nvds) in (nvds.extent(chip,family)[1],
                                       preservation_extent(chip,family)[1]),
                 'Incomplete physical NVDS read; no erase')
    if family=='gr5513_zkel_internal' and len(current_nvds)==0x4000:
        # Reuse the existing strict complete-log decoder. The accepted old
        # linear log at 7C000 must not be hidden by selecting GIV1's 7E000.
        if current_nvds.startswith(nvds.MAGIC):
            if current_nvds[0x2000:0x2008]==nvds.MAGIC:
                return _cleared('Conflicting old/new NVDS log locations')
            return review_current(chip,current_nvds)
        # This exact stock layout has its sole complete linear log in the
        # last sector. All other reservation bytes must be erased: no GC
        # replay, prefix salvage or inferred generation/authority.
        single=next((o for o in range(0,0x4000,0x1000)
                     if current_nvds[o:o+8]==nvds.MAGIC and
                     current_nvds[:o]==b'\xff'*o and
                     current_nvds[o+0x1000:]==b'\xff'*(0x3000-o)),None)
        if single is not None:
            current_nvds=current_nvds[single:single+0x1000]+b'\xff'*0x1000
        else:
            # Current GIV1 owns 7C000..7DFFF as image data, not settings.
            # A complete log there is handled above; arbitrary image bytes
            # are not parsed as vendor records. Damaged/unknown mutable
            # logs retain the existing explicitly confirmed Full policy.
            current_nvds=current_nvds[0x2000:]
    # Known vendor single-sector layouts can place their sole complete log at
    # another sector within the already captured reservation. Reuse the exact
    # GIV1 authority rule: one whole log, every other byte erased, strict full
    # native decoding below. This is not a tag scan, GC replay or prefix repair.
    logical=current_nvds
    if not logical.startswith(nvds.MAGIC):
        single=next((o for o in range(0,len(logical),4096)
                     if logical[o:o+8]==nvds.MAGIC and logical[:o]==b'\xff'*o and
                     logical[o+4096:]==b'\xff'*(len(logical)-o-4096)),None)
        if single is not None:
            logical=logical[single:single+4096]+b'\xff'*(len(logical)-4096)
    if logical != b'\xff'*len(logical) and not logical.startswith(nvds.MAGIC):
        # No current native namespace was recognized. Human-confirmed Full
        # discards old user formats instead of requiring a format migration.
        # Never scan opaque bytes for a Pro token, copy unknown vendor data,
        # or describe this as proof that authorization was absent. A displaced
        # recognized native log still needs its integrity/authority checks.
        return _cleared('Unrecognized or conflicting old user format')
    try:
        records,deleted=_full_records(logical,chip)
    except ValueError as error:
        return _cleared('Old NVDS cannot be fully decoded: '+str(error).replace('; no erase',''))
    entitlement=records.get(activation.TAG)
    absent=entitlement is None
    warnings=[]
    # In a completely decoded linear native log, inactive (tag-zero) records
    # are not live authority. Ordinary Free settings updates create them too.
    # Do not resurrect their payloads or call unresolved GC/damage absence.
    if entitlement is not None:
        try:activation.native_token(entitlement)
        except activation.UnverifiableAuthorization as error:
            warnings.append(str(error)+'; not preserved, initializing Free.')
            entitlement=None
    vendor=tuple(sorted((tag,value) for tag,value in records.items()
                        if tag not in PRODUCT and tag!=0xc001))
    try:identity=nvds.identity_from_records(records,chip,full_defaults=True,deleted=deleted)
    except ValueError as error:
        warnings.append('Old BLE identity is invalid: '+str(error).replace('; no erase','')+
                        '; using verified factory/UID initialization.')
        identity=None
    source=('factory-default' if identity is None else
            'installed-typed' if 0x5212 in records else
            'installed-address-native-public-default' if 0xc001 in records else
            'factory-default')
    return dict(format='recognized',identity=identity,vendor=vendor,
                activation=entitlement,activationAbsent=absent,
                activationUnavailable=not absent and entitlement is None,warnings=warnings,
                identitySource=source,
                reason='' if identity is not None else
                    'legacy-reserved-address-reinitialized' if 0xc001 in records else
                    'proven-absent-live-address-record')


def select_full_identity(installed,factory_address,factory_type,*,chip,uid):
    """A Full may create a new identity; no guessed installed/absence type."""
    nvds.require(chip in GEOMETRY and type(uid) is bytes and len(uid)==16 and
                 uid not in (bytes(16),b'\xff'*16),
                 'Full requires a verified complete immutable UID; no erase')
    if installed is not None:
        nvds.valid_address(*installed)
        return (*installed,'installed-typed')
    if factory_address is not None:
        try:
            nvds.valid_address(factory_address,factory_type)
            # Never use the SDK's non-unique sample address for a real unit.
            if factory_address!=bytes.fromhex('eacb3ecf000f'):
                return factory_address,factory_type,'immutable-factory'
        except ValueError:
            pass
    namespace=0x0102 if chip=='GR5331' else 0x0101
    address,kind=nvds.derive(namespace,uid)
    return address,kind,'uid-hash-full-reset'


def compose(chip,current_nvds,candidate,address,kind,signed_recipe,candidate_epoch,*,family=None,
            raw_uid=None,signed_app=None):
    require_backend(chip)
    g=geometry(chip,family)
    nvds.require(type(candidate) is bytes and len(candidate)==g['bytes'],
                 'Incomplete candidate physical Full image')
    nvds.require(type(current_nvds) is bytes and
                 len(current_nvds) in (nvds.extent(chip,family)[1],
                                       preservation_extent(chip,family)[1]),
                 'Incomplete protected Factory/NVDS reservation')
    nvds.require(candidate[0x2028:0x202c]==g['marker'] and
        int.from_bytes(candidate[0x202c:0x2030],'little')==
        (~int.from_bytes(g['marker'],'little')&0xffffffff),
        'Candidate layout is not release-v1')
    configured=profile(candidate)
    seed.backend(chip,configured)
    old=review_current(chip,current_nvds,family)
    installed=old['identity']
    nvds.require(installed is None or installed==(address,kind),
                 'Installed identity differs; no erase')
    vendor=old['vendor']
    entitlement=old['activation']
    warnings=list(old['warnings'])
    unavailable=old['activationUnavailable']
    if entitlement is not None:
        try:entitlement=activation.preserve(entitlement,chip=chip,raw_uid=raw_uid,signed_app=signed_app)
        except activation.UnverifiableAuthorization as error:
            warnings.append(str(error)+'; not preserved, initializing Free.')
            entitlement=None
            unavailable=True
    result=bytearray(candidate)
    result[g['user']:]=b'\xff'*(g['bytes']-g['user'])
    result[g['nvds']:]=seed.prepare_full(chip,address,kind,signed_recipe,configured,
                                   retained_vendor=vendor,family=family,
                                   retained_activation=entitlement)
    journal=initial_journal(chip,candidate_epoch,family)
    result[g['journal']:g['journal']+8192]=journal
    nvds.require(read_prepared_identity(bytes(result[g['nvds']:]),chip,family)==(address,kind),
                 'Composed image lost immutable identity')
    return bytes(result),dict(mode='full-all',chip=chip,
        currentNvdsSha256=sha(current_nvds),resultingFlashSha256=sha(result),
        candidateEpoch=candidate_epoch,nativeRecoveryReinitialized=True,
        recoveryStateSha256=sha(journal),previousEpochChecked=False,
        previousLayoutInspected=False,preservedVendorTags=[hex(t) for t,_ in vendor],
        proPreserved=entitlement is not None,proAbsenceProven=old['activationAbsent'],
        proPreservationUnavailable=unavailable,warnings=warnings,
        addressTypeSource=old['identitySource'],
        previousNvdsFormat=old['format'],previousNvdsDiscardReason=old['reason'],
        clearScope='mcu-flash',externalFlashAccessed=False,
        externalFlashInScope=False,exactBoardQualified=False)
