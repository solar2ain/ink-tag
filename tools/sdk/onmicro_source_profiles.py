"""OM build-time providers. Preserve the existing SDK translation units."""
import hashlib
import json
from pathlib import Path

from inktag_source_profiles import source_path, profile_defines


class SourceSelection:
    def __init__(self, repo, profile):
        if profile != "base":
            raise ValueError("This Basic source tree supports only profile=base")
        self.repo = Path(repo).resolve()
        self.profile = profile
        self.defines = profile_defines(self.repo, profile=profile)
        self.port = self.repo / 'firmware/ports/onmicro/common/src'
        self.manifest = json.loads((self.repo / 'firmware/ports/onmicro/source-profiles.json').read_text())
        if self.manifest.get('schemaVersion') != 1:
            raise ValueError('Unsupported OM profile manifest')
        self.files = {}
        self.omitted = set()

    def selected(self, source):
        source = Path(source)
        relative = source.relative_to(self.repo) if source.is_relative_to(self.repo) else None
        if relative is not None and relative.parts[:2] == ('firmware', 'common'):
            return source_path(self.repo, relative.as_posix(), profile=self.profile)
        if source.parent == self.port:
            provider = self.manifest['providers'].get(source.name)
            if provider is not None:
                name = provider[self.profile]
                source = None if name is None else self.port / name
            elif self.profile == 'base' and source.name in self.manifest['privateAssets']:
                source = None
        if source is not None and not source.is_file():
            raise ValueError('Missing selected OM source: ' + str(source))
        return source

    def copy_file(self, source, destination):
        destination = Path(destination)
        selected = self.selected(Path(source))
        if selected is None:
            self.omitted.add(destination)
            if destination.exists():
                raise ValueError('Omitted private source present in base project: ' + str(destination))
            return
        content = selected.read_bytes()
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.is_file() or destination.read_bytes() != content:
            destination.write_bytes(content)
        if selected.is_relative_to(self.repo):
            self.files[str(destination)] = dict(logicalPath=Path(source).relative_to(self.repo).as_posix(),
                source=selected.relative_to(self.repo).as_posix(), sha256=hashlib.sha256(content).hexdigest())

    def include_assets(self, destination):
        # No private assets exist in this Basic source projection.
        pass

    def source_list(self, sdk, sources):
        if self.profile == 'base':
            sources = [name.replace('projects/ble_app_inktag/', 'projects/ble_app_inktag_base/')
                       .replace('projects/inktag_om_swap_boot/', 'projects/inktag_om_swap_boot_base/')
                       for name in sources]
        return [name for name in sources if Path(sdk) / name not in self.omitted]

    def receipt(self):
        return dict(schemaVersion=1, sourceProfile=self.profile, defines=self.defines,
                    files=[dict(destination=k, **v) for k, v in sorted(self.files.items())])
