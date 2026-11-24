"""Ordinary base ELF segment placement and read-only file validation.
No private measurement regions, digest patcher or activation/key retention.
"""
from pathlib import Path


def native_tool_path(path):
    value = Path(path).resolve().as_posix()
    if len(value) > 2 and value[1] == ':' and value[2] == '/':
        return '/mnt/' + value[0].lower() + value[2:]
    return value

def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Unknown OM native placement input: '+old[:80])
    return text.replace(old, new, 1)

def layout_choice(repo, role, requested='auto'):
    # Kept as a recipe ABI only; base placement does not permute private code.
    return 0

def guard_linker(text, layout, recovery=False, profile="base"):
    # GNU ld otherwise coalesces this output with legacy writable .text under
    # the pinned SDK's -N profile. Describe actual load permissions explicitly;
    # no FILEHDR/PHDRS bytes are inserted into the native payload.
    text = once(text, 'SECTIONS\n', '''PHDRS
{
    vectors PT_LOAD FLAGS(4);
    ram_code PT_LOAD FLAGS(5);
    legacy_text PT_LOAD FLAGS(7);
    data PT_LOAD FLAGS(6);
    bss PT_LOAD FLAGS(6);
}

SECTIONS
''')
    text = once(text, '        KEEP(*(.vectors))\n    } > FLASH',
                '        KEEP(*(.vectors))\n    } > FLASH :vectors')
    # Assign the launch/boot-entry output to the same vector segment.
    marker = '    } > FLASH\n\n    /* RAM */'
    text = once(text, marker, '    } > FLASH :vectors\n\n    /* RAM */')
    text = once(text, '    } > RAM_RO AT > FLASH', '    } > RAM_RO AT > FLASH :ram_code')
    text = once(text, '    } > RAM AT > FLASH', '    } > RAM AT > FLASH :data')
    text = once(text, '    } > RAM\n', '    } > RAM :NONE\n')
    text = once(text, '    } > INKTAG_RETAINED\n', '    } > INKTAG_RETAINED :NONE\n')
    text = text.replace('    } > RAM_HIGH\n', '    } > RAM_HIGH :bss\n')
    text = once(text, '        __code_end__ = .;\n    } > FLASH',
                '        __code_end__ = .;\n    } > FLASH :legacy_text')
    return text

def guard_makefile(text, repo, role, profile="base"):
    if profile != "base":
        raise ValueError("Private recipe must select its separate provider")
    tools = Path(repo)/'tools/sdk'
    command = 'python3 "'+native_tool_path(tools/'verify-onmicro-base-native.py')+'" --role '+role
    inputs = ' '.join(native_tool_path(tools/name).replace(' ', '\\ ') for name in
        ('verify-onmicro-base-native.py','onmicro_base_native.py','onmicro_base_elf.py','onmicro_base_hex.py'))
    return text + ('\n$(TARGET_ELF): .EXTRA_PREREQS += '+inputs+' Makefile\n'
        '.DELETE_ON_ERROR:\n.PHONY: inktag-base-native-verify\nall: inktag-base-native-verify\n'
        'inktag-base-native-verify: $(TARGET_ELF) $(TARGET_BIN) $(TARGET_HEX)\n'
        '\t'+command+' --elf $(TARGET_ELF) --bin $(TARGET_BIN) --hex $(TARGET_HEX)\n')

def install_link_hook(text):
    # base has no post-link rewrite. A vendor/other-profile optional hook may
    # remain in a shared SDK rule; this base recipe supplies no command for it.
    return text
