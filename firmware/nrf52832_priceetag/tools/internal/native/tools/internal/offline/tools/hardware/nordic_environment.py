"""Read-only portable dependency inspection. No transports or host configuration."""
import importlib.metadata
import json
import sys
import cryptography
import cffi
import yaml
import qrcode

if __name__ == '__main__':
    print(json.dumps(dict(python='.'.join(map(str, sys.version_info[:3])),
        packages={name: importlib.metadata.version(name) for name in
                  ('cryptography','cffi','pycparser','PyYAML','qrcode','colorama')})))
