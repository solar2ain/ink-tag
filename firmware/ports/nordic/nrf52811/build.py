#!/usr/bin/env python3
"""One-command Full build. Windows uses the documented WSL toolchain."""
from pathlib import Path
import runpy
import sys

sys.dont_write_bytecode = True
runpy.run_path(str(Path(__file__).resolve().parent/'build-support/build/build-full.py'), run_name='__main__')
