"""Free stock input and real Git-index rejection of custom-input bodies."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess

import pytest

from da_host_helpers import ROOT, compile_run, function


def test_da_no_stock_mapping_ignores_unused_button_settings_in_ram(tmp_path):
    config = (ROOT/'compat/common/epd/inktag_epd_config.c').read_text(encoding='utf-8')
    default = function(config.replace('void epd_device_settings_default(',
                                     'static void epd_device_settings_default('),
                       'epd_device_settings_default(').removeprefix('static ')
    source = tmp_path/'actual-da-default.c'
    source.write_text(r'''
#include <assert.h>
#include <string.h>
#include "inktag_board.h"
#include "inktag_epd_config.h"
''' + default + r'''
int main(void){
    /* Exact maintained stock family, not a conclusion about every DA board. */
    assert(INKTAG_BOARD_DA_AESL29_STOCK_SELECTOR==2);
    assert(INKTAG_BOARD_DA_AESL42_STOCK_SELECTOR==3);
    assert(!INKTAG_BOARD_BUTTON_PRESENT && !INKTAG_BOARD_AUX_IO_BACKEND_AVAILABLE);
    epd_device_settings_t settings;
    memset(&settings,0xA5,sizeof settings);
    epd_device_settings_default(&settings);
    assert(settings.version==EPD_DEVICE_SETTINGS_VERSION);
    const unsigned char *p=(const unsigned char *)&settings.button;
    for(unsigned i=0;i<sizeof settings.button;i++)assert(!p[i]);
    return 0;
}
''', encoding='utf-8')
    source_path = runpy.run_path(str(ROOT/'build-support/build/source_scope.py'))['source_path']
    board_dir = source_path(ROOT, 'boards/da14585_aesl.h').parent
    compile_run(tmp_path, ['-DINKTAG_DIALOG', '-Itests/mocks/board-family',
                '-I'+Path(os.path.relpath(board_dir, ROOT)).as_posix(),
                '-Icompat/common/epd', '-Icompat/common/input'],
                [source, 'compat/common/input/inktag_input.c'], sanitize=True)


@pytest.mark.parametrize('symbol', [
    'inktag_input_action_allowed', 'inktag_button_bindings_migrate_default',
    'inktag_button_binding', 'inktag_button_simulate',
    'inktag_input_simulation_begin',
])
def test_real_index_rejects_custom_body_under_the_free_input_filename(tmp_path, symbol):
    def git(*args):
        return subprocess.check_output(['git', '-C', str(tmp_path), *args],
                                       stderr=subprocess.STDOUT)
    git('init', '-b', 'codex/input-boundary-fixture')
    git('config', 'core.autocrlf', 'false')
    name = 'compat/common/input/inktag_input.c'
    path = tmp_path/name
    path.parent.mkdir(parents=True)
    data = ('bool '+symbol+'(void) { return true; }\n').encode('ascii')
    path.write_bytes(data)
    (tmp_path/'SOURCE_MANIFEST.json').write_text(json.dumps({'files':[
        {'path':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}]}),
        encoding='utf-8')
    git('add', '.')
    guard = runpy.run_path(str(ROOT/'build-support/build/check_publish.py'))
    with pytest.raises(ValueError, match='Pro custom-input implementation'):
        guard['check'](tmp_path)


def test_stock_runtime_has_no_custom_definition():
    scope = runpy.run_path(str(ROOT/'build-support/build/source_scope.py'))
    name = 'compat/common/input/inktag_input.c'
    assert scope['free_implementation_error'](name, (ROOT/name).read_bytes()) is None
