"""Exercise pinned source preparation without network access or real dependency edits."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tarfile
import tempfile
from unittest.mock import patch

script = Path(__file__).resolve().parents[1] / 'scripts/fetch-pcsx2-components.py'
spec = importlib.util.spec_from_file_location('device_sources', script)
sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sources)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    reference = root / 'reference'
    reference.mkdir()
    (reference / 'source.cpp').write_bytes(b'original\n')
    archive = root / 'source.tar.gz'
    with tarfile.open(archive, 'w:gz') as output:
        output.add(reference / 'source.cpp', arcname='revision/source.cpp')
    entry = dict(name='pcsx2', directory='prepared', archive=archive.name,
                 url='https://invalid.example/not-requested', archive_bytes=archive.stat().st_size,
                 sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                 tree_sha256=sources.tree_hash(reference))
    (root / 'deps.lock.json').write_text(json.dumps({'pcsx2_devices': [entry]}))
    with patch.object(sys, 'argv', ['fetch', '--check']), patch.object(sources.urllib.request, 'urlopen') as network:
        try:
            sources.main(root)
            raise AssertionError('Missing source passed verification')
        except RuntimeError:
            pass
        network.assert_not_called()
    with patch.object(sys, 'argv', ['fetch']), patch.object(sources.urllib.request, 'urlopen') as network:
        sources.main(root)
        network.assert_not_called()
    prepared = root / 'prepared/source.cpp'
    assert prepared.read_bytes() == b'original\n'
    prepared.write_bytes(b'local change\n')
    with patch.object(sys, 'argv', ['fetch']):
        try:
            sources.main(root)
            raise AssertionError('Modified source was overwritten')
        except RuntimeError:
            pass
    assert prepared.read_bytes() == b'local change\n'
    prepared.unlink()
    prepared.parent.rmdir()
    archive.write_bytes(b'wrong archive')
    with patch.object(sys, 'argv', ['fetch']):
        try:
            sources.main(root)
            raise AssertionError('Wrong archive was accepted')
        except RuntimeError:
            pass
    assert not prepared.exists()
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    parent = root / 'prepared'
    child = parent / 'nested'
    child.mkdir(parents=True)
    (parent / 'source.cpp').write_bytes(b'parent\n')
    (child / 'source.cpp').write_bytes(b'child\n')
    entries = [
        dict(name='parent', directory='prepared', excluded_directories=['nested'],
             tree_sha256=sources.tree_hash(parent, (Path('nested'),))),
        dict(name='child', directory='prepared/nested', tree_sha256=sources.tree_hash(child)),
    ]
    def check_graphics():
        (root / 'deps.lock.json').write_text(json.dumps({'graphics_devices': entries}))
        with patch.object(sys, 'argv', ['fetch', '--group', 'graphics_devices', '--check']), \
                patch.object(sources.urllib.request, 'urlopen') as network:
            try:
                sources.main(root)
            finally:
                network.assert_not_called()
    check_graphics()
    (child / 'source.cpp').write_bytes(b'changed child\n')
    try:
        check_graphics()
        raise AssertionError('Nested component modification escaped verification')
    except RuntimeError as error:
        assert 'Modified or incomplete' in str(error)
    (child / 'source.cpp').write_bytes(b'child\n')
    for excluded in ['unlocked', '../prepared', str(root.resolve())]:
        entries[0]['excluded_directories'] = [excluded]
        try:
            check_graphics()
            raise AssertionError('Unowned or unsafe exclusion was accepted')
        except RuntimeError as error:
            assert 'own locked entry' in str(error)
print('Device source pin, nested dependency and preservation tests passed')
