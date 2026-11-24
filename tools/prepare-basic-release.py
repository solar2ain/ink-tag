#!/usr/bin/env python3
"""Prepare a fixed BASIC release candidate locally. No GitHub writes."""
from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import zipfile

from basic_release import INVENTORY, digest, inspect_basic_pair, native_inventory
from client_content import CREATION_FONT_NOTICES, GUIDES, render_documents, scriptless_document, user_web_files
from publication_scope import artifacts, verify_firmware_bytes


ROOT = Path(__file__).resolve().parents[1]


def file_inventory(*paths):
    listing = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', *paths], cwd=ROOT)
    return {name: (ROOT / name).read_bytes()
            for raw in set(listing.split(b'\0')) if raw
            for name in (raw.decode('utf-8'),) if (ROOT / name).is_file()}


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


def write_zip(path, prefix, contents):
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(contents.items()):
            info = zipfile.ZipInfo(prefix + '/' + name, (2026, 11, 24, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)


def refresh_manual():
    manual = render_documents(ROOT)
    for lang, title in (('zh-CN', 'InkTag 使用手册'), ('en', 'InkTag User Manual')):
        links = ''.join(f'<li><a href="{name}.html">{html.escape((ROOT / "docs" / lang / (name + ".md")).read_text("utf-8").splitlines()[0].lstrip("# "))}</a></li>'
                        for name in GUIDES)
        manual[f'manual/{lang}/index.html'] = scriptless_document(title, '<h1>' + title + '</h1><ul>' + links + '</ul>', lang).encode()
    for name, body in manual.items():
        for root in ('webui', 'webui-standalone'):
            target = ROOT / root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            home = '../' * (len(Path(name).parts) - 1)
            target.write_bytes(body if root == 'webui' else body.replace(
                f'href="{home}index.html"'.encode(), f'href="{home}InkTag-WebUI.html"'.encode()))


def reference(path, tag, kind):
    return {'type': kind, 'name': path.name, 'bytes': path.stat().st_size, 'sha256': digest(path.read_bytes()),
            'downloadUrl': f'https://github.com/solar2ain/ink-tag/releases/download/{tag}/{path.name}'}


def prepare(tag, *, replace=False):
    version = json.loads((ROOT / 'version.json').read_bytes())['version']
    if not re.fullmatch(rf'v{re.escape(version)}-\d{{8}}\.[1-9]\d*', tag):
        raise ValueError('Use the next unused dated stable release tag')
    record_path = ROOT / 'releases/record.json'
    record = json.loads(record_path.read_bytes())
    previous = next((entry for entry in record['releases'] if entry['releaseTag'] == tag), None)
    if previous and (previous['published'] or previous['releaseReady'] or not previous['candidateOnly']):
        raise ValueError('Published or approved release tags cannot be rebuilt')
    assets = ROOT / '.release-assets' / tag
    if assets.exists() and not replace:
        raise ValueError('Candidate attachments already exist; compare or explicitly --replace local candidate files')
    assets.mkdir(parents=True, exist_ok=True)
    inventory = native_inventory(ROOT)
    native = json.loads((ROOT / 'release/firmware/manifest.json').read_bytes())
    if native['appVersion'] != version or native['edition'] != 'BASIC':
        raise ValueError('Native delivery is for a different version or edition')

    refresh_manual()
    for name in CREATION_FONT_NOTICES:
        for folder in ('webui', 'webui-standalone'):
            shutil.copyfile(ROOT / 'webui/src/assets/fonts' / name, ROOT / folder / 'licenses' / name)
    static = user_web_files(ROOT, include_update_feeds=False)
    index = (ROOT/'webui/licenses/index.html')
    index.write_bytes(static['licenses/index.html'])
    (ROOT/'webui-standalone/licenses/index.html').write_bytes(static['licenses/index.html'])
    standalone = file_inventory('webui/dist/standalone')
    standalone = {name.removeprefix('webui/dist/standalone/'): body for name, body in standalone.items()}
    for name, body in static.items():
        if name.startswith(('manual/', 'licenses/')):
            standalone[name] = (ROOT / 'webui-standalone' / name).read_bytes() if (ROOT / 'webui-standalone' / name).is_file() else body
    standalone['LICENSE.txt'] = static['LICENSING.txt']
    standalone['THIRD_PARTY_NOTICES.txt'] = static['THIRD_PARTY_NOTICES.txt']
    standalone['README.txt'] = (ROOT / 'webui-standalone/README.txt').read_bytes()
    dist = ROOT / 'webui/dist/standalone/InkTag-WebUI.html'
    shutil.copyfile(dist, ROOT / 'webui-standalone/InkTag-WebUI.html')

    web_file = assets / f'InkTag-WebUI-static-{tag}.zip'
    offline_file = assets / f'InkTag-WebUI-standalone-{tag}.zip'
    cli_file = assets / f'InkTag-cli-{tag}.zip'
    source_file = assets / f'InkTag-client-source-{tag}.zip'
    web_build = re.search(rb'name="inktag-client-build" content="([^"]+)"', static['index.html'])[1].decode()
    write_zip(web_file, 'webui', static)
    write_zip(offline_file, 'InkTag-WebUI', standalone)
    cli = file_inventory('cli', 'tools/findmy', 'LICENSE', 'LICENSING.md', 'THIRD_PARTY_NOTICES.md',
                         'README.md', 'README.en.md', 'install.py', 'install_inktag.cmd', 'install_inktag.ps1', 'install_inktag.sh')
    nfc_paths = (
        'tools/hardware/inktag_nfc.py', 'tools/hardware/inktag_nfc_ndef.py',
        'tools/hardware/inktag_nfc_reader.py', 'tools/hardware/inktag_gt_stream.py',
        'tools/nfc/inktag-nfc.cmd', 'tools/nfc/requirements.txt', 'tools/nfc/README.md',
        'tools/nfc/README.en.md', 'tools/nfc/provider/LICENSE', 'tools/nfc/provider/NOTICE.md',
        'tools/nfc/provider/ink_nfc_fmsc/__init__.py', 'tools/nfc/provider/ink_nfc_fmsc/isodep.py',
        'tools/nfc/provider/ink_nfc_fmsc/pn532.py',
    )
    nfc = file_inventory(*nfc_paths)
    if nfc.keys() != set(nfc_paths):
        raise ValueError('PN532 tool files are missing from the release checkout')
    cli.update(nfc)
    write_zip(cli_file, 'InkTag-client', {**cli, **{'webui/' + name: body for name, body in static.items()}})
    with TemporaryDirectory(prefix='inktag-cli-') as temporary:
        project = Path(temporary) / 'cli'
        for name, body in cli.items():
            if name.startswith('cli/'):
                file = project / name.removeprefix('cli/')
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(body)
        subprocess.run([sys.executable, '-m', 'build', '--no-isolation', '--outdir', str(assets), str(project)],
                       env=dict(os.environ, SOURCE_DATE_EPOCH='1795478400'), check=True, stdout=subprocess.DEVNULL)
    cli_distributions = [reference(assets / name, tag, 'cli') for name in
                         (f'inktag_cli-{version}-py3-none-any.whl', f'inktag_cli-{version}.tar.gz')]
    source = file_inventory('webui/src', 'webui/tools', 'webui/public', 'webui/vite.config.ts',
                            'webui/index.html', 'webui/leafer-lab.html', 'webui/package.json', 'webui/package-lock.json',
                            'webui/tsconfig.app.json', 'webui/tsconfig.node.json', 'webui/tsconfig.json',
                            'packages', 'assets/fonts', 'protocol', 'tests/fixtures',
                            'firmware/common', 'firmware/boards', 'tools/emulator', 'tools/version', 'version.json',
                            'docs/build-webui.md', 'docs/build-webui.en.md')
    notices = {name: body for name, body in file_inventory('webui/licenses').items()
               if name not in ('webui/licenses/index.html', 'webui/licenses/release-materials.txt')}
    write_zip(source_file, 'InkTag-client-source', {**cli, **source, **notices})

    pending = dict(candidateOnly=True, releaseReady=False, published=False)
    root = ROOT / 'release/firmware'
    tools = []
    firmware = []
    boards = []
    checksum = ROOT / INVENTORY
    for target, entry in sorted(native['targets'].items()):
        ota_ref, = entry['artifacts']['ota']
        fulls = [a for a in entry['artifacts']['full'] if not a['path'].endswith('.hex')]
        if not fulls and target == 'nrf52811_aesl':
            # The maintained programmer consumes the ZIP; the primary HEX is
            # also supplied for users of a graphical programmer.
            fulls = [{'path': p.relative_to(root).as_posix()} for p in (root / target / 'full').glob('*.zip')]
        full_ref, = fulls
        pair = [root / ota_ref['path'], root / full_ref['path']]
        if any(not p.is_relative_to(root / target) or p.relative_to(ROOT).as_posix() not in inventory for p in pair):
            raise ValueError(f'Invalid native artifact paths for {target}')
        qualified = inspect_basic_pair(ROOT, target, *pair)
        if (qualified['version'] != version + '.0' or
                qualified['target']['layout'] != entry.get('otaLayout', entry['layout']) or
                qualified['target']['compatibilityEpoch'] != entry['epoch']):
            raise ValueError(f'Native artifact metadata differs from the distribution inventory: {target}')
        rows = []
        public = []
        for kind, source_path in zip(('ota', 'full'), pair):
            sha = digest(source_path.read_bytes())
            name = f'InkTag-{target}-{kind}-v{qualified["version"]}-{sha[:12]}{source_path.suffix}'
            destination = assets / name
            shutil.copyfile(source_path, destination)
            item = reference(destination, tag, kind)
            if kind == 'ota':
                item['repositoryPath'] = source_path.relative_to(ROOT).as_posix()
            rows.append(item)
            public.append(dict(item, repositoryPath=source_path.relative_to(ROOT).as_posix(),
                               embeddedOtaSha256=qualified['pairing']['otaSha256'],
                               candidateOnly=True, qualification='local-review-only'))
        tool = dict(toolsVersion=entry['toolsVersion'], inventory=INVENTORY, bytes=checksum.stat().st_size,
                    sha256=digest(checksum.read_bytes()))
        tools.append(dict(buildTarget=target, **tool))
        firmware.append(dict(qualified, buildTarget=target, edition='basic',
                             buildId='sha256:' + rows[0]['sha256'], buildNumber=0, programmerInventory=tool,
                             artifacts=rows, **pending))
        boards.append(dict(boardId=target, buildTarget=target, firmwareVersion=qualified['version'],
                           releaseScope='candidate', artifacts=public, publication=qualified,
                           recoveryVersion=entry['recoveryVersion'], boardProfiles=entry['boardProfiles']))

    web, offline, client, sources = (reference(p, tag, k) for p, k in
        ((web_file, 'web'), (offline_file, 'web-standalone'), (cli_file, 'cli'), (source_file, 'source-material')))
    client.update(requiresPython='>=3.11', includesLocalServer=True)
    release = dict(releaseTag=tag, channel='stable', **pending, sourceMaterials=[sources], firmware=firmware,
                   components={'webui': dict(version=version, buildId=web_build, buildNumber=0, artifacts=[web, offline]),
                               'cli': dict(version=version, buildId='sha256:' + client['sha256'], buildNumber=0,
                                           artifacts=[client, *cli_distributions])})
    verify_firmware_bytes(ROOT, release, assets)
    record['releases'] = [r for r in record['releases'] if r['releaseTag'] != tag] + [release]
    record['firmwareEnabled'] = True
    write_json(record_path, record)
    manifest = dict(schemaVersion=2, product='InkTag', repository='solar2ain/ink-tag', releaseTag=tag,
                    version=version, webuiVersion=version, cliVersion=version, edition='basic', channel='stable',
                    components=['cli', 'firmware', 'webui'], candidateOnly=True, releaseReady=False,
                    firmwareSelection=dict(included=True, published=False, boards=[f['buildTarget'] for f in firmware],
                        kinds=['ota', 'full'], publicationRecord='releases/record.json', state='prepared-release-snapshot'),
                    boards=boards, programmerTools=tools, web=web, webStandalone=offline, cli=client,
                    cliDistributions=cli_distributions, sourceMaterials=[sources])
    manifest_path = ROOT / 'firmware/manifest.json'
    write_json(manifest_path, manifest)
    shutil.copyfile(manifest_path, assets / f'InkTag-v{version}-manifest.json')
    shutil.copyfile(ROOT / 'RELEASE_NOTES.md', assets / 'RELEASE_NOTES.md')
    runtime = {p.relative_to(ROOT).as_posix(): digest(p.read_bytes())
               for folder in ('cli/src', 'webui/dist/static/assets')
               for p in (ROOT / folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts}
    renderer = 'cli/src/inktag_cli/_assets/nativePreviewWasm.js'
    source_binding = dict(id='client-source', bytes=sources['bytes'], sha256=sources['sha256'],
                          forComponents=[{'path': renderer, 'sha256': runtime[renderer]}],
                          delivery=dict(kind='release-asset', name=source_file.name, url=sources['downloadUrl']))
    write_json(ROOT / 'releases/readiness.json', dict(schemaVersion=2, edition='basic', reviewedReleaseTag=tag,
        publicReleaseApproved=False, cliCorrespondingSourceVerified=False, webuiBinaryTermsApproved=False,
        thirdPartyInventoryVerified=False, requiredSourceComponents=[renderer], newIndependentApprovalRequired=True,
        sourceMaterials=[source_binding], runtimeHashes=runtime))
    checks = {item['name']: item['sha256'] for item in artifacts(release)}
    checks.update({path.name: digest(path.read_bytes()) for path in
                   (assets / f'InkTag-v{version}-manifest.json', assets / 'RELEASE_NOTES.md')})
    if replace and previous:
        for item in artifacts(previous):
            name = item['name']
            if name in checks or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.+-]*', name):
                continue
            file = assets / name
            if not file.is_symlink() and file.is_file() and digest(file.read_bytes()) == item['sha256']:
                file.unlink()
    (assets / 'SHA256SUMS.txt').write_text(''.join(f'{v}  {k}\n' for k, v in sorted(checks.items())),
                                         encoding='ascii', newline='\n')
    print(f'Prepared {len(checks)} checksummed attachments; {len(firmware)} BASIC targets. Awaiting release review.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--replace', action='store_true', help='Rebuild an existing local candidate')
    args = parser.parse_args()
    prepare(args.tag, replace=args.replace)
