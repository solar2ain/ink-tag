"""Export an explicit DA source snapshot. Local preparation is not publication approval."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import runpy
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[2]
_source_scope = runpy.run_path(str(Path(__file__).with_name('source_scope.py')))
implementation_kind = _source_scope['implementation_kind']
free_implementation_error = _source_scope['free_implementation_error']
source_path = _source_scope['source_path']
MANIFEST = Path('build-support/build/source-files.json')
ALLOWED_SUFFIXES={'.c','.h','.inc','.in','.py','.ps1','.json','.md','.txt','.csv'}
PUBLIC_DOCS = {'BUILDING.md'}
PUBLIC_IGNORE = '''# Local SDKs, build results and environments
.local/
.sdk/
.venv/
.pytest_cache/
__pycache__/
_build/
*.pyc
*.axf
*.o
*.bin
*.img
*.hex

# Signing secrets, agent instructions and device captures
.secrets/
.env
.env.*
.codex/
.claude/
[Aa][Gg][Ee][Nn][Tt].[Mm][Dd]
[Aa][Gg][Ee][Nn][Tt][Ss]*.[Mm][Dd]
CLAUDE.md
GEMINI.md
.github/copilot-instructions.md
backups/
factory_backups/
device_backups/
device-backups/
device_captures/
'''
PROJECTIONS = {
    '.gitignore': 'da-source-ignore-v1',
    'build-support/sdk/da_sdk_workspace.py': 'da-source-workspace-v1',
    'compat/common/epd/inktag_epd_ssd.c': 'da-source-ssd-normal-v1',
}
NO_E6_FILES = {
    'compat/common/epd/inktag_epd_jd.c',
    'compat/common/epd/inktag_epd_screen.c',
    'compat/common/epd/inktag_epd_runtime.c',
    'compat/common/epd/inktag_epd_driver.h',
    'compat/common/epd/inktag_epd_screen.h',
    'compat/common/gui/Adafruit_GFX.c',
    'compat/common/security/inktag_brand_overlay.c',
    'tests/test_gui_full_frame.c',
}
PROJECTIONS.update({name: 'da-source-no-e6-v1' for name in NO_E6_FILES})
STORAGE_FILES = {
    'compat/common/storage/inktag_settings_journal.c',
    'compat/common/storage/inktag_settings_journal.h',
}
PROJECTIONS.update({name: 'da-source-storage-scope-v2' for name in STORAGE_FILES})
NORMAL_ONLY_FILES = {
    'compat/dialog/src/user_custs1_impl.c',
    'compat/dialog/src/user_peripheral.c',
    'compat/dialog/src/user_peripheral.h',
    'compat/dialog/src/inktag_da_protocol_v2.inc',
    'compat/dialog/src/inktag_da_external_assets.c',
    'build-support/sdk/materialize-dialog-port.py',
}
PROJECTIONS.update({name: 'da-source-normal-only-v1' for name in NORMAL_ONLY_FILES})
UNUSED_STORAGE_API = (
    'find_public_history_safe', 'clear_preserving_password_qr', 'reset_settings_v2',
    'inactive_erased', 'scrub_inactive', 'replace_record_atomic', 'replace_all',
    'stage_open', 'stage_begin', 'stage_read', 'stage_write', 'stage_commit',
    'stage_abort', 'empty_media_safe', 'clear',
)

def plain(path):
    for part in (path,*path.parents):
        try: info=part.lstat()
        except FileNotFoundError: continue
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
            raise ValueError('Linked source/output refused')

def project_workspace(data):
    marker = '# DA workspace projection v1\n'
    text = data.decode('utf-8')
    if text.startswith(marker): return data
    start = text.index('def default_root():')
    end = text.index('VERSION_PATH =', start)
    text = text[:start] + text[end:]
    start = text.index("    if value.name.lower() == '.sdk'")
    end = text.index('    return value', start)
    text = text[:start] + """    # A Git-owned SDK must belong to the same target family, without
    # embedding any private repository names in this public projection.
    for owner in value.parents:
        if (owner / '.git').exists():
            if not (owner / 'app').is_dir():
                raise ValueError('SDK belongs to a different target family')
            break
""" + text[end:]
    text = text.replace('def materialize(repo, sdk_root, forwarded, helper=None, build_profile=None):',
                        'def materialize(repo, sdk_root, forwarded, build_profile=None):')
    lines = []
    for line in text.splitlines(True):
        if "parser.add_argument('--helper'" in line: continue
        if "command += ['--flash']" in line: continue
        if "script = 'materialize-dialog-readonly-helper.py'" in line:
            line = "    script = 'materialize-dialog-port.py'\n"
        line = line.replace("type=Path, default=DEFAULT_ROOT", "type=Path, required=True")
        line = line.replace('forwarded, args.helper, args.build_profile)', 'forwarded, build_profile=args.build_profile)')
        line = line.replace("build_profile != 'crc-only' or helper", "build_profile != 'crc-only'")
        line = line.replace("'utilities/secondary_bootloader', 'utilities/flash_programmer'",
                            "'utilities/secondary_bootloader'")
        lines.append(line)
    text = ''.join(lines)
    text = text.replace("if forwarded[:1] == ['--']: forwarded = forwarded[1:]",
        "if forwarded[:1] == ['--']: forwarded = forwarded[1:]\n"
        "    if forwarded not in ([], ['--board', 'da14585_aesl']):\n"
        "        parser.error('Only the DA board may be forwarded')")
    text = text.replace("data.get('sdk') != 'DA145xx_SDK6.0.24.1464' or inventory(base)",
        "data.get('sdk') != 'DA145xx_SDK6.0.24.1464' or not (base / MARKER).is_file() or inventory(base)")
    if 'args.helper' in text or 'helper=None' in text or 'DEFAULT_ROOT' in text:
        raise ValueError('Incomplete public workspace projection')
    return (marker + text).encode('utf-8')

def project_ssd(data):
    """Remove only branches excluded by the unchanged normal DA profile."""
    marker = '/* DA normal SSD projection v1 */\n'
    text = data.decode('utf-8').replace('\r\n', '\n')
    if text.startswith(marker): return data
    lines = text.splitlines(True)
    result=[]; active=True; stack=[]
    selected = {'#if !defined(INKTAG_EXCLUDE_SSD1675_154)': False,
                '#if INKTAG_EPD_ENABLE_SSD1677_GRAY4': False,
                '#if defined(INKTAG_BOARD_SSD1675_FLIP_Y)': False}
    for line in lines:
        directive=line.strip()
        if re.match(r'#\s*(?:if|ifdef|ifndef)\b',directive):
            known=directive in selected
            stack.append((active,known,selected.get(directive,True)))
            if known: active=active and selected[directive]; continue
        elif directive == '#else':
            parent,known,condition=stack[-1]
            if known: active=parent and not condition; continue
        elif directive == '#endif':
            parent,known,condition=stack.pop()
            if known: active=parent; continue
        if active: result.append(line)
    if stack: raise ValueError('Unbalanced SSD projection')
    text=''.join(result)
    text=text.replace(' * panel-specific stock waveforms below have unresolved redistribution provenance.\n',
                      ' * excluded stock waveforms are absent from this normal-profile projection.\n')
    guard='''#if !defined(INKTAG_EXCLUDE_SSD1675_154) || INKTAG_EPD_ENABLE_SSD1677_GRAY4
#error DA_source_SSD_source_requires_the_unchanged_normal_profile
#endif
'''
    text=text.replace('#include "inktag_epd_driver.h"\n', '#include "inktag_epd_driver.h"\n'+guard,1)
    if 'SSD1675_154_FULL_LUT' in text or 'SSD1677_426_GRAY4_LUT' in text:
        raise ValueError('Unremoved excluded waveform')
    return (marker+text).encode('utf-8')

def project_no_e6(data):
    """Omit disabled E6 implementations, retaining reserved IDs and source ABI."""
    marker = '/* DA normal profile: E6 implementations are not included. */\n'
    text = data.decode('utf-8').replace('\r\n', '\n')
    if text.startswith(marker): return data
    disabled = {
        'INKTAG_EPD_ENABLE_JD79665CA',
        'defined(INKTAG_EPD_ENABLE_JD79665CA) && INKTAG_EPD_ENABLE_JD79665CA',
        'INKTAG_EPD_ENABLE_JD79XXX && INKTAG_EPD_ENABLE_JD79665CA',
        'INKTAG_EPD_ENABLE_OSPREY_E6',
    }
    stack = []
    active = True
    output = []
    for line in text.splitlines(True):
        directive = line.strip()
        match = re.fullmatch(r'#\s*if\s+(.+)', directive)
        if re.match(r'#\s*(?:if|ifdef|ifndef)\b', directive):
            known = bool(match and match.group(1).strip() in disabled)
            stack.append((active, known))
            if known:
                active = False
                continue
        elif re.match(r'#\s*elif\b', directive):
            if stack[-1][1]:
                raise ValueError('Unexpected E6 elif; review the source projection')
        elif directive == '#else':
            parent, known = stack[-1]
            if known:
                active = parent
                continue
        elif directive == '#endif':
            parent, known = stack.pop()
            if known:
                active = parent
                continue
        if active: output.append(line)
    if stack: raise ValueError('Unbalanced E6 source projection')
    guard = '''#if INKTAG_EPD_ENABLE_JD79665CA || INKTAG_EPD_ENABLE_OSPREY_E6
#error DA_source_source_requires_E6_disabled
#endif
'''
    return (marker + guard + ''.join(output)).encode('utf-8')


def project_storage(name, data):
    """Remove other-platform helpers with no DA caller, not record guards."""
    marker = '/* DA storage API v2: only consumed settings operations. */\n'
    text = data.decode('utf-8').replace('\r\n', '\n')
    if text.startswith(marker): return data
    for suffix in UNUSED_STORAGE_API:
        function = 'inktag_settings_journal_' + suffix
        match = re.search(r'(?m)^bool ' + function + r'\s*\(', text)
        if match is None: raise ValueError('Missing unconsumed storage helper: ' + function)
        start = match.start()
        if name.endswith('.h'):
            end = text.index(';', start) + 1
            preceding = text[:start].rstrip()
            if preceding.endswith('*/'):
                start = preceding.rfind('/*')
        else:
            ending = re.search(r'(?m)^}\s*$', text[start:])
            if ending is None: raise ValueError('Unbalanced storage helper: ' + function)
            end = start + ending.end()
        text = text[:start] + text[end:]
    if name.endswith('.h'):
        for typename in ('inktag_settings_journal_record_t',
                         'inktag_settings_journal_stage_t'):
            pattern = r'(?ms)^typedef struct \{(?:(?!^}).)*^} ' + typename + r';\s*'
            text, count = re.subn(pattern, '', text)
            if count != 1: raise ValueError('Missing unused storage type: ' + typename)
        text = text.replace(
            '/* New settings scope, ordinary phase only: omit 2/3/6/7/11/13. The caller\n'
            ' * consumes fresh W first and commits authoritative password OFF/QR last via\n'
            ' * its existing backend. This function deliberately preserves ID5. */',
            '/* DA settings reset: omit ordinary settings records; preserve opaque ID5. */')
    else:
        pattern = r'(?ms)^static bool staged_page_header_valid\s*\(.*?^}\s*'
        text, count = re.subn(pattern, '', text)
        if count != 1: raise ValueError('Missing unused staged header validator')
    return (marker + text.rstrip() + '\n').encode('utf-8')


def project_normal_only(name, data):
    """Exclude private bench implementations/options; preserve normal inputs."""
    python = name.endswith('.py')
    marker = '# DA normal source projection v1\n' if python else '/* DA normal source projection v1 */\n'
    text = data.decode('utf-8').replace('\r\n', '\n')
    if text.startswith(marker): return data
    if python:
        flags = {'packbits_candidate', 'extended_sleep_candidate', 'a33_panel_candidate',
                 'connection_temperature_candidate', 'named_device_qr_candidate', 'gui_cache_lines'}
        lines = text.splitlines(True)
        cuts = set()
        tree = ast.parse(text)
        for node in ast.walk(tree):
            remove = False
            if isinstance(node, ast.Assign):
                remove = any(isinstance(t, ast.Name) and t.id in {
                    'PACKBITS_CANDIDATE_BOARD_OVERRIDES', 'EXTENDED_SLEEP_CANDIDATE_BOARD_OVERRIDES'} for t in node.targets)
            if isinstance(node, ast.If):
                remove = any(isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name)
                    and n.value.id == 'args' and n.attr in flags for n in ast.walk(node.test))
            if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call):
                call = node.value
                remove = (isinstance(call.func, ast.Attribute) and call.func.attr == 'add_argument'
                    and call.args and isinstance(call.args[0], ast.Constant)
                    and str(call.args[0].value).removeprefix('--').replace('-', '_') in flags)
            if isinstance(node, ast.For):
                remove = (isinstance(node.target, ast.Tuple)
                    and [x.id for x in node.target.elts if isinstance(x, ast.Name)] == ['slot', 'callback'])
            if remove: cuts.update(range(node.lineno-1, node.end_lineno))
        text = ''.join(line for i, line in enumerate(lines) if i not in cuts)
        text = text.replace('(" -DINKTAG_DA_A33_PANEL_CANDIDATE=1" if args.a33_panel_candidate else "")', '""')
        start = text.index('# Bench opt-in only:')
        end = text.index('def specialize_da_fixed_header', start)
        text = text[:start] + text[end:]
        parsed = ast.parse(text)
        if any(isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name)
               and n.value.id == 'args' and n.attr in flags for n in ast.walk(parsed)):
            raise ValueError('Unremoved private candidate build option')
    else:
        selected = {
            'INKTAG_DA_EXTENDED_SLEEP_CANDIDATE': False,
            'INKTAG_BOARD_EXTENDED_SLEEP_QUALIFIED || INKTAG_DA_EXTENDED_SLEEP_CANDIDATE': False,
            '!INKTAG_BOARD_EXTENDED_SLEEP_QUALIFIED && !INKTAG_DA_EXTENDED_SLEEP_CANDIDATE': True,
            'INKTAG_DA_CONNECTION_TEMPERATURE_CANDIDATE': False,
            'INKTAG_DA_NAMED_QR_CANDIDATE': False,
        }
        active = True; stack = []; output = []
        for line in text.splitlines(True):
            directive = line.strip()
            match = re.fullmatch(r'#\s*if\s+(.+)', directive)
            if re.match(r'#\s*(?:if|ifdef|ifndef)\b', directive):
                key = match.group(1).strip() if match else None
                known = key in selected
                stack.append((active, known, selected.get(key, True)))
                if known: active = active and selected[key]; continue
            elif re.match(r'#\s*elif\b', directive) and stack[-1][1]:
                raise ValueError('Unexpected normal-profile elif')
            elif directive == '#else':
                parent, known, condition = stack[-1]
                if known: active = parent and not condition; continue
            elif directive == '#endif':
                parent, known, condition = stack.pop()
                if known: active = parent; continue
            if active: output.append(line)
        if stack: raise ValueError('Unbalanced normal source projection')
        text = ''.join(output)
        if name.endswith('user_peripheral.h'):
            text, count = re.subn(r'(?m)^void user_app_sleep_probe_(?:enter|exit)\([^;]*\);\n', '', text)
            if count != 2: raise ValueError('Missing private sleep probe declarations')
    return (marker + text).encode('utf-8')


def project_file(name,data):
    if name in NORMAL_ONLY_FILES: return project_normal_only(name, data)
    if name == '.gitignore': return PUBLIC_IGNORE.encode('utf-8')
    if name == 'build-support/sdk/da_sdk_workspace.py': return project_workspace(data)
    if name == 'compat/common/epd/inktag_epd_ssd.c': return project_ssd(data)
    if name in NO_E6_FILES: return project_no_e6(data)
    if name in STORAGE_FILES: return project_storage(name,data)
    return data

def source_files(root):
    plain(root)
    value=json.loads((root/MANIFEST).read_text(encoding='utf-8'))
    files=value['files']
    projections=value.get('projections', {})
    if projections != PROJECTIONS:
        raise ValueError('Unknown/missing workspace projection policy')
    if files!=sorted(set(files)):raise ValueError('Allowlist must be sorted and unique')
    result=[]
    for name in files:
        rel=Path(name)
        if rel.is_absolute() or '..' in rel.parts or '\\' in name or ':' in name:
            raise ValueError('Source path escape')
        kind = implementation_kind(name)
        if kind:
            raise ValueError(kind + ' implementation cannot enter DA source')
        if rel.parts[0] not in {'app','boards','compat','build-support','tests','protocol'} and name not in PUBLIC_DOCS | {'.gitignore','LICENSE','Makefile','build.ps1','version.json','SOURCE_NOTICES.md','NOTICE.md','THIRD_PARTY_NOTICES.md','LICENSES/GPL-3.0.txt'}:
            raise ValueError('Source path outside admitted roots')
        if name not in {'LICENSE','.gitignore','Makefile'} and rel.suffix.lower() not in ALLOWED_SUFFIXES:
            raise ValueError('Binary/unadmitted file type')
        if any(x.lower() in {'.git','.sdk','.secrets','factory_backups','backups','device_backups','device-backups','__pycache__','agent.md','agents.md'} for x in rel.parts):
            raise ValueError('Private source member')
        if name.startswith(('app/', 'compat/dialog/')) and not (name.startswith('compat/dialog/src/') or name in {
            'app/inktag_secondary_crc32.c',
            'app/inktag_secondary_bootloader_585_armclang.sct.in'}):
            raise ValueError('Other platform or production tools are not source inputs')
        if re.search(r'(inktag_activation[._]|inktag_device_password|inktag_owner_auth|inktag_findmy|ota-signing|da_build_candidate|make-dialog.*image)',name):
            raise ValueError('Credential/production entry cannot enter source')
        path = source_path(root, name)
        plain(path)
        if not path.is_file():raise ValueError('Missing allowlisted source: '+name)
        data=path.read_bytes()
        if name == 'boards/boards.json' and path != root / name:
            registry = json.loads(data)
            boards = {key: registry['boards'][key] for key in ('aesl42_da', 'aesl29_da')}
            data = (json.dumps(dict(schemaVersion=registry['schemaVersion'], boards=boards), indent=2) + '\n').encode('utf-8')
        if name in projections: data=project_file(name,data)
        error = free_implementation_error(name, data)
        if error: raise ValueError(error + ': ' + name)
        if re.search(rb'-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----',data):
            raise ValueError('Private key material in source')
        result.append((name,data))
    return result

def export(root,output):
    root=root.absolute();output=output.absolute();plain(output)
    files=source_files(root) # Validate everything before creating the destination.
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Output must be a fresh empty directory')
    output.mkdir(parents=True,exist_ok=True)
    record={'schemaVersion':1,'scope':'DA14585 source snapshot',
        'files':[], 'projections':[]}
    for name,data in files:
        if name in PROJECTIONS:
            original=(root/name).read_bytes()
            blob=None
            if (root/'.git').exists():
                blob=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD:'+name],text=True).strip()
            record['projections'].append({'path':name,'rule':PROJECTIONS[name],
                'inputGitBlob':blob,'inputSha256':hashlib.sha256(original).hexdigest(),
                'outputSha256':hashlib.sha256(data).hexdigest()})
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('xb') as stream:stream.write(data)
        record['files'].append({'path':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
    record['allowlistSha256']=hashlib.sha256((root/MANIFEST).read_bytes()).hexdigest()
    (output/'SOURCE_MANIFEST.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    for entry in record['files']:
        if hashlib.sha256((output/entry['path']).read_bytes()).hexdigest()!=entry['sha256']:
            raise ValueError('Changing source export')
    return record

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=ROOT)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); record=export(args.repo,args.output)
    print(json.dumps({'files':len(record['files']),'output':str(args.output),'scope':record['scope']}))
if __name__=='__main__':main()
