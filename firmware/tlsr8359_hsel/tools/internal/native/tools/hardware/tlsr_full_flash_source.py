"""Bounded source-family inspection; never guesses an old partition version."""
import argparse
import json
from pathlib import Path
from tlsr_full_flash_config import PCB_BACKENDS
from tlsr_full_flash_v1 import read_exact,source_family
from tlsr_full_flash_address import preservation_selector, complete_source


def inspect_factory(path: Path, board: str) -> dict:
    entry=next((row for row in PCB_BACKENDS.values() if row[0]==board),None)
    if entry is None:raise ValueError('Unknown immutable backend')
    factory=read_exact(path,4096)
    family=source_family(factory,entry[4])
    result=dict(source_layout=family,factory_bytes=4096,whole_flash_read=False,
                warnings=complete_source(factory)[1],
                prior_partition_version_observed=False)
    if family=='stock':result['stock_not_yet_verified']=True
    else:
        try:
            backend,flags=preservation_selector(factory[0x100:0x10c])
        except ValueError:
            result.update(board_selector=None, target_board_selector=entry[4], flags=None,
                          explicit_config_selector_correction=True)
        else:
            result.update(board_selector=backend,target_board_selector=entry[4],flags=flags,
                          explicit_config_selector_correction=backend!=entry[4])
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--factory',type=Path,required=True);p.add_argument('--board',required=True)
    a=p.parse_args()
    try:print(json.dumps(inspect_factory(a.factory,a.board)))
    except (ValueError,OSError,KeyError) as error:p.error(str(error))
