"""Package the matching built NRO/payload without deleting other build outputs."""
from pathlib import Path
import hashlib
import re
import zipfile

root = Path(__file__).resolve().parents[1]
version = re.search(r'set\(HATS_TOOLS_VERSION "([^"]+)"', (root/'sphaira/CMakeLists.txt').read_text()).group(1)
files = {
    'switch/nexus-hats-installer/nexus-hats-installer.nro': root/'build/Release/nexus-hats-installer.nro',
    'switch/nexus-hats-installer/hats-installer.bin': root/'payload/output/hats-installer.bin',
    'config/nexus-hats-installer/hekate_ipl_mod.ini': root/'assets/romfs/hekate_ipl_mod.ini',
    'config/nexus-hats-installer/hats-ca-bundle.pem': root/'assets/romfs/hats-ca-bundle.pem',
    'config/nexus-hats-installer/config.ini': root/'config.ini',
    'config/nexus-hats-installer/background.rgba': root/'assets/external-background/background.rgba',
}
for name in ('fetch-hats', 'fetch-firmware', 'cheats', 'advanced-options', 'file-browser', 'uninstall-components'):
    files[f'config/nexus-hats-installer/icons/{name}.rgba'] = root/f'assets/external-icons/{name}.rgba'
for name, file in files.items():
    if not file.is_file() or not file.stat().st_size:
        raise SystemExit(f'Missing or empty release file: {file}')
    if name.endswith('.rgba'):
        expected = 1280*720*4 if 'background' in name else 256*256*4
        if file.stat().st_size != expected:
            raise SystemExit(f'Wrong artwork dimensions: {file}')
embedded = root/'build/Release/sphaira/romfs/nexus-package'
for name, file in files.items():
    # User configuration is created from compiled defaults on NSP first launch;
    # it is intentionally not a replaceable bundled support asset.
    if name.endswith('.nro') or name.endswith('/config.ini'):
        continue
    if not (embedded/name).is_file() or (embedded/name).read_bytes() != file.read_bytes():
        raise SystemExit(f'Bundled support file differs from release file; rebuild first: {name}')
out = root/'out'
out.mkdir(exist_ok=True)
target = out/f'Nexus-HATS-Installer-{version}.zip'
partial = target.with_suffix('.zip.part')
with zipfile.ZipFile(partial, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name, file in files.items():
        z.write(file, name)
with zipfile.ZipFile(partial) as z:
    if z.testzip() is not None:
        raise SystemExit('Release ZIP failed its CRC check')
partial.replace(target)
print(f'Created {target}\nSHA256 {hashlib.sha256(target.read_bytes()).hexdigest()}')
