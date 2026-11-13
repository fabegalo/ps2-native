"""Fetch or verify the immutable device sources recorded in deps.lock.json."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import urllib.request


def tree_hash(directory, excluded=()):
    digest = hashlib.sha256()
    for path in sorted(directory.rglob('*')):
        relative = path.relative_to(directory)
        if any(relative.is_relative_to(item) for item in excluded):
            continue
        if path.is_file():
            digest.update(relative.as_posix().encode() + b'\0')
            digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def main(root=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify without downloading or writing')
    parser.add_argument('--group', choices=('pcsx2_devices', 'graphics_devices'), default='pcsx2_devices')
    args = parser.parse_args()
    root = Path(root) if root is not None else Path(__file__).resolve().parents[1]
    entries = json.loads((root / 'deps.lock.json').read_text())[args.group]
    directories = {(root / entry['directory']).resolve() for entry in entries}
    for entry in entries:
        directory = root / entry['directory']
        excluded = tuple(Path(item) for item in entry.get('excluded_directories', []))
        for item in excluded:
            if item.is_absolute() or '..' in item.parts or (directory / item).resolve() not in directories:
                raise RuntimeError('Excluded device directory must have its own locked entry')
        if directory.exists():
            if tree_hash(directory, excluded) != entry['tree_sha256']:
                raise RuntimeError(f"Modified or incomplete component: {directory}; preserving existing files")
            print(f"Verified {entry['name']}")
            continue
        if args.check:
            raise RuntimeError(f"Missing {directory}; run scripts/fetch-pcsx2-components.py")
        archive = root / entry['archive']
        archive.parent.mkdir(parents=True, exist_ok=True)
        if not archive.exists():
            partial = archive.with_suffix(archive.suffix + '.part')
            try:
                with urllib.request.urlopen(entry['url'], timeout=90) as source, partial.open('wb') as output:
                    total = 0
                    while block := source.read(1024 * 1024):
                        total += len(block)
                        if total > entry['archive_bytes']:
                            raise RuntimeError('Archive exceeds locked size')
                        output.write(block)
                if hashlib.sha256(partial.read_bytes()).hexdigest() != entry['sha256']:
                    raise RuntimeError('Downloaded archive hash mismatch')
                partial.replace(archive)
            finally:
                partial.unlink(missing_ok=True)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError(f"Archive hash mismatch: {archive}")
        if archive.suffix == '.7z':
            names = subprocess.check_output(['tar', '-tf', str(archive)], text=True).splitlines()
            for name in names:
                path = PurePosixPath(name.replace('\\', '/'))
                if path.is_absolute() or '..' in path.parts or ':' in name:
                    raise RuntimeError('Unsafe archive path')
            directory.mkdir(parents=True)
            subprocess.run(['tar', '-xf', str(archive), '-C', str(directory)], check=True)
        else:
            directory.mkdir(parents=True)
            with tarfile.open(archive, 'r:gz') as source:
                for member in source:
                    parts = PurePosixPath(member.name).parts[1:]
                    if not parts or '..' in parts or not member.isfile():
                        continue
                    if entry['name'] == 'components':
                        if parts[0] == 'ext' and len(parts) > 1 and parts[1] in ('kzgs', 'kzspu2'):
                            parts = parts[1:]
                        elif len(parts) != 1 or not parts[0].startswith('LICENSE'):
                            continue
                    target = directory.joinpath(*parts)
                    if not target.resolve().is_relative_to(directory.resolve()):
                        raise RuntimeError('Unsafe archive path')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(source.extractfile(member).read())
        if tree_hash(directory, excluded) != entry['tree_sha256']:
            raise RuntimeError(f"Extracted component differs from locked source: {directory}")
        print(f"Prepared {entry['name']}")


if __name__ == '__main__':
    main()
