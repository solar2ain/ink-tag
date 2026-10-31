"""Portable GR Config+signed-base preparation; no SDK, ELF or signing keys.

Full preserves valid same-device Pro and clears MCU user data after confirmation,
not GR5513 external image Flash. Uncertain authority stops before erase. The authenticated
package initializes native Recovery state; installed epoch/layout is not an
admission rule. No candidate/source/compiler allowlist or stock backup is needed.
Exact MCU/capacity, signature and necessary identity/Factory checks remain.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import shutil
from pathlib import Path
import struct
import uuid
import sys

import goodix_factory_init as factory
import goodix_pin_config as pins
import goodix_release_full as release_full

PORTABLE_EXECUTION_QUALIFIED = False
# These are explicitly user-authorized pilot entry paths, NOT physical-test or
# release-qualification results. Exact package/capacity checks remain mandatory.
EXECUTION_CANDIDATE_CHIPS = frozenset(('GR5331', 'GR5513', 'GR5515'))
BOOT_BYTES = 0x4000

def require_execution_family(report):
    # Signed family/config pairing is checked by prepare. GIV1 uses the same
    # writer and fresh identity/vendor review, not a separate approval mode.
    factory.require(report.get('chip') in EXECUTION_CANDIDATE_CHIPS,
                    'Unknown Full MCU; no hardware accessed')
    factory.require(report.get('firmwareFamily') != factory.INTERNAL_FAMILY or
                    report['chip']=='GR5513',
                    'GIV1 Full requires GR5513; no hardware accessed')


def prepare(base: Path, config: Path, *, progress=None):
    progress = progress or (lambda stage, message: None)
    config_hash = factory.sha(config.read_bytes())
    config_review = pins.read_board_config(config)
    factory.require(factory.sha(config.read_bytes()) == config_hash, 'Config changed during preparation')
    hardware = config_review['backend']
    facts = pins.FACTS[hardware]
    chip = 'GR5331' if facts['mcu'] == 0x31 else 'GR5513' if facts['mcu'] == 0x13 else 'GR5515'
    progress(1, 'Config validated; expected MCU '+chip+' (not detected)')
    review, data = factory.verified_payloads(base, chip)
    factory.require(review['chip'] == chip, 'Exact chip/base/PCB mismatch')
    factory.require_release_consumer(review['metadata'], data['app'])
    progress(2, 'App, Recovery and every packaged factory initializer authenticated')
    cfg = factory.profile(chip, review['firmwareFamily'])
    internal = review['firmwareFamily'] == factory.INTERNAL_FAMILY
    factory.require(internal == (hardware == 'gr5513_zkc21v'),
                    'GIV1 family and internal-only no-NFC/no-NOR GR5513 ZKC21V backend must match')
    signed, recovery = data['app'], data['recovery']
    capacity = 0x100000 if chip == 'GR5515' else 0x80000
    app_limit = 0x64000 if internal else (0x60000 if chip == 'GR5331' else 0x72000)
    factory.require(0x20000 + len(signed) <= app_limit, 'Signed App exceeds enforced payload limit')
    selector, profile = pins.factory_records(hardware,
        bytes.fromhex(config_review['pin_wire_hex']), bytes.fromhex(config_review['screen_wire_hex']))
    progress(3, 'Layout, payload limits and bounded board records validated')
    result = dict(schema='inktag-gr-configured-full-v1', backend=hardware,
        board_id=facts['board_id'], product_name=facts['product_name'], assembly=facts['assembly'],
        chip=chip, board_marking=facts['board_marking'], base_sha256=review['baseSha256'],
        config_sha256=config_hash,
        package_ids=[0x0204,0x0205,0x0208] if chip=='GR5331' else cfg['packages'],
        flash_bytes=capacity, layout=cfg['layout'], firmwareFamily=cfg['board'], app_bytes=len(signed)-904,
        signed_app_bytes=len(signed), app_limit_bytes=app_limit-0x20000-904,
        app_remaining_bytes=app_limit-0x20000-len(signed), recovery_bytes=len(recovery),
        recovery_limit_bytes=0x1C000, stock_backup_required=False,
        installed_image_required=False, installed_image_compared=False,
        portableExecutionQualified=PORTABLE_EXECUTION_QUALIFIED,
        executionAvailable=True,
        executionBlockReason='',
        runtimePinConfigQualified=False, hardware_started=False,
        retry_detect_default=False, automatic_write_retry=False)
    # Match the self-contained SDK Full builders: erased unused metadata,
    # fresh authenticated Recovery/App descriptors, no inherited DFU state.
    boot = bytearray(b'\xff' * BOOT_BYTES)
    address = cfg['recovery']
    header = struct.pack('<6I', len(recovery), sum(recovery) & 0xFFFFFFFF,
                         address, address, 3, 0x744)
    boot[:24] = header
    descriptor = struct.pack('<HH', 0x4744, 1)+header+b'inktag_boot'.ljust(12,b'\0')
    boot[0x40:0x70 if chip == 'GR5331' else 0x68] = descriptor.ljust(48,b'\0') if chip == 'GR5331' else descriptor
    boot[0xF00:0xF0C], boot[0xF20:0xF60] = selector, profile
    boot[0x2000:0x2030] = signed[-904:-856]
    boot[0x3FF8:0x4000] = struct.pack('<II', 0x31444649, ~0x31444649 & 0xFFFFFFFF)
    image = bytearray(b'\xff' * capacity)
    image[:BOOT_BYTES] = boot
    image[0x4000:0x4000+len(recovery)] = recovery
    image[0x20000:0x20000+len(signed)] = signed
    result.update(image_sha256=factory.sha(image),
                  immutable_binding_bytes=len(selector+profile),
                  protected_nvds_review_required=True,settings_clear_authorized=False,
                  full_scope_available=True,clear_scope='mcu-flash',external_flash_in_scope=False,
                  data_scope='full-all; pending-live-identity-and-Factory-review')
    progress(4, 'Offline image prepared; no stock backup or installed-image dependency, no hardware accessed')
    return result, bytes(image), data, review


def review_logger(family, verbose):
    # Reuse the owning Probe/Backup event schema, runner and primary-root
    # resolver. Importing the adapter does not probe or access a device.
    spec=importlib.util.spec_from_file_location('gr_full_review_log',
        Path(__file__).with_name('goodix-stock-workflow.py'))
    module=importlib.util.module_from_spec(spec)
    sys.modules[spec.name]=module
    spec.loader.exec_module(module)
    name=family or 'goodix'
    folder=module.catalog.primary_root(factory.ROOT)/'.local/backup-archive'/name/'pending'/('full-review-'+uuid.uuid4().hex)
    log=module.Log(folder,module.workflow.Board(name,name,'goodix'),'full',1,verbose=verbose)
    return module,folder,log


def default_console():
    """Resolve the shared installation, not an unrelated cwd or backup file."""
    spec=importlib.util.spec_from_file_location('gr_full_installation',
        Path(__file__).with_name('goodix-backup-catalog.py'))
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.primary_root(factory.ROOT)/'.tools/gprogrammer/_._/GR5xxx_console.exe'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--family', choices=('gr5331', 'gr551x'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--require-write', action='store_true')
    parser.add_argument('--snr',default='')
    parser.add_argument('--address',default='')
    parser.add_argument('--console',default='')
    parser.add_argument('--jlink',default='C:/Program Files/SEGGER/JLink_V788j/JLink.exe')
    parser.add_argument('--evidence',default='')
    parser.add_argument('--powershell',default='')
    parser.add_argument('--retry-detect',action='store_true')
    parser.add_argument('--detect-timeout',type=int,default=120)
    parser.add_argument('--frequency-hz',type=int,default=500000)
    parser.add_argument('--operation-timeout',type=int,default=300)
    parser.add_argument('--verbose',action='store_true')
    parser.add_argument('--console-progress',action='store_true')
    args = parser.parse_args(argv)
    logger=None; result={}; folder=None; hardware_started=False; started=None
    try:
        factory.require(1<=args.detect_timeout<=3600 and 30<=args.operation_timeout<=1800,
                        'Invalid detection/operation timeout; no hardware accessed')
        factory.require(1000<=args.frequency_hz<=1000000 and args.frequency_hz%1000==0,
                        'Invalid SWD diagnostic frequency; no hardware accessed')
        if args.console_progress:
            adapter,folder,logger=review_logger(args.family,args.verbose)
            started=adapter.workflow.utc()
        progress=(lambda stage,message: logger.emit('review',f'Offline review {stage}/4: {message}',level='notice')) if logger else None
        factory.require(not (args.output and args.require_write), 'Offline output and hardware execution are separate operations')
        report, image, payloads, verified = prepare(args.base, args.config,progress=progress)
        # Public Full DryRun is an execution preflight, not the generic archive
        # inspector. Do not report an old/unbound initializer as ready to flash.
        from goodix_portable_executor import require_host_recipe
        require_host_recipe(verified['metadata'],verified['chip'])
        result=dict(report,status='offline-review-passed')
        factory.require(not args.family or (report['chip']=='GR5331') == (args.family=='gr5331'), 'Wrong owning port')
        factory.require(not args.require_write or report['chip'] in EXECUTION_CANDIDATE_CHIPS,
                        'Portable GR Full device entry is not qualified for this MCU; no hardware accessed')
        if args.require_write:
            require_execution_family(report)
            release_full.require_backend(report['chip'])
            from goodix_portable_executor import execute
            args.chip=report['chip'].lower()
            args.powershell=args.powershell or shutil.which('pwsh') or shutil.which('powershell')
            factory.require(args.powershell and 1<=args.detect_timeout<=3600,'PowerShell/deadline required')
            args.evidence=args.evidence or str(factory.ROOT/'.local/gr-portable-full'/uuid.uuid4().hex)
            args.console=args.console or str(default_console())
            hardware_started=True
            report=execute(args.base,args.config,args)
            result=report
            if logger:
                # The public entry owns the final summary, not a nested logger.
                # execute has already persisted its hardware receipt.
                from goodix_portable_executor import report_full_success
                report_full_success(logger,report,Path(args.evidence)/'result.json')
        if args.output:
            with args.output.open('xb') as stream: stream.write(image)
        print(json.dumps(report, indent=2))
        if logger and not args.require_write:
            logger.emit('result','Offline review passed; no Full write or hardware qualification is implied.',level='result')
        return 0
    except (ValueError, OSError) as error:
        result.update(status='failed' if hardware_started else 'refused',error=str(error),hardware_started=hardware_started)
        if logger: logger.emit('result',str(error),level='error')
        else: print('ERROR: ' + str(error))
        return 1
    finally:
        if logger:
            try:
                receipt=dict(result,**logger.common)
                receipt.update(review_schema=result.get('schema'),started_utc=started,ended_utc=adapter.workflow.utc())
                adapter.json_file(folder/'result.json',receipt)
                logger.emit('result',f'Validation evidence: {folder}',level='notice')
            finally: logger.close()


if __name__ == '__main__':
    raise SystemExit(main())
