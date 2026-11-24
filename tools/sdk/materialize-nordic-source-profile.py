#!/usr/bin/env python3
"""Select Nordic common inputs at build time, preserving logical object/TU names.

This is a thin Nordic Make adapter to the maintained shared selector. It never
signs, stages, accesses hardware or discovers an optional private directory.
"""
import argparse
import hashlib
import json
import shlex
from pathlib import Path

from inktag_source_profiles import profile_defines, source_path, source_selection

GUI_SOURCE = 'firmware/common/gui/inktag_gui.c'
GUI_PROVIDERS = {
    'INKTAG_GUI_DEVICE_BADGE_PROVIDER': 'firmware/common/gui/inktag_gui_edition_badge.inc',
    'INKTAG_GUI_DEVICE_NAME_PROVIDER': 'firmware/common/gui/inktag_gui_edition_name.inc',
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def update(path, data):
    path = Path(path)
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.new')
    temporary.write_bytes(data)
    temporary.replace(path)


def materialize(repo, output, profile, logicals):
    repo, output = Path(repo).resolve(), Path(output)
    # The caller retains source/link order. Never flatten basenames into one
    # object namespace or copy private bodies into the public source directory.
    gui = GUI_SOURCE in logicals
    selection = source_selection(repo, list(logicals) +
        (list(GUI_PROVIDERS.values()) if gui else []), profile=profile)
    selection['selectorSha256'] = digest(repo / 'tools/sdk/inktag_source_profiles.py')
    selection['manifestSha256'] = digest(repo / 'firmware/source-profiles.json')
    selection['adapterSha256'] = digest(__file__)
    receipt = output.with_suffix('.json')
    selected, omitted = [], []
    for logical in logicals:
        source = source_path(repo, logical, profile=profile)
        if source is None:
            omitted.append(logical)
        else:
            selected.append((logical, source.relative_to(repo).as_posix()))
    definitions = profile_defines(repo, profile=profile)
    # A separate non-ALLOC assembler section binds the final ELF to its build
    # profile without adding a firmware byte, C state or LTO-visible asm block.
    note = output.parent / 'inktag_source_profile.S'
    update(note, ('.section .inktag_source_profile,"",%progbits\n'
                  f'.byte {definitions["INKTAG_BUILD_PRO"]}\n').encode('ascii'))
    lines = ['# Generated explicit source profile; no firmware runtime dispatch.',
             'NRF_PROFILE_OMITTED := ' + ' '.join(omitted),
             'NRF_PROFILE_DEFINES := ' + ' '.join(f'-D{k}={v}' for k, v in definitions.items()),
             'NRF_PROFILE_NOTE_OBJECT := $(BUILD_DIR)/generated/inktag_source_profile.o',
             '', '$(NRF_PROFILE_NOTE_OBJECT): ' + note.as_posix(),
             '\t@mkdir -p $(dir $@)',
             '\t$(AS) $(ASFLAGS) -MMD -MP -c $< -o $@']
    if gui:
        providers = {key: source_path(repo, name, profile=profile)
                     for key, name in GUI_PROVIDERS.items()}
        target = '$(BUILD_DIR)/firmware/common/gui/inktag_gui.o'
        lines += ['', target + ': ' + ' '.join(path.relative_to(repo).as_posix()
                                               for path in providers.values())]
        if profile == 'pro':
            # Quoted absolute headers work with the original in-place TU.
            # Shell quote the complete -D argument, including its C quotes.
            flags = [shlex.quote('-D' + key + '="' + path.as_posix() + '"')
                     for key, path in providers.items()]
            lines += [target + ': override CFLAGS += ' + ' '.join(flags)]
    for logical, source in selected:
        if source == logical:
            continue
        target = '$(BUILD_DIR)/' + str(Path(logical).with_suffix('.o')).replace('\\', '/')
        lines += ['', f'{target}: {source} {receipt.as_posix()}',
                  '\t@mkdir -p $(dir $@)',
                  '\t$(CC) $(CFLAGS) -MMD -MP -c $< -o $@']
    update(receipt, (json.dumps(selection, indent=2) + '\n').encode('utf-8'))
    update(output, ('\n'.join(lines) + '\n').encode('utf-8'))
    return selection


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', choices=('base', 'pro'), required=True)
    parser.add_argument('logical', nargs='+')
    args = parser.parse_args()
    materialize(args.repo, args.output, args.profile, args.logical)
