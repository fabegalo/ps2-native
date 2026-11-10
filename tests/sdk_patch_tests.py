"""Small synthetic regression for the locked SDK patch workflow; no real SDK edits."""
import difflib
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args, ok=True):
    result = subprocess.run(args, capture_output=True, text=True,
                            stdin=subprocess.DEVNULL, timeout=30)
    assert (result.returncode == 0) == ok, result.stdout + result.stderr
    return result.stdout.strip()


script = Path(__file__).resolve().parents[1] / 'scripts/apply-sdk-patches.ps1'
with tempfile.TemporaryDirectory(dir=sys.argv[1] if len(sys.argv) > 1 else None) as tmp:
    root = Path(tmp)
    sdk = root / 'sdk'
    sdk.mkdir()
    run('git', '-C', str(sdk), 'init', '-q')
    run('git', '-C', str(sdk), 'config', 'core.autocrlf', 'false')
    base, final = b'original\n', b'patched\n'
    source = sdk / 'source.txt'
    source.write_bytes(base)
    run('git', '-C', str(sdk), 'add', '.')
    run('git', '-C', str(sdk), '-c', 'user.name=Fixture', '-c',
        'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture')
    pin = run('git', '-C', str(sdk), 'rev-parse', 'HEAD')
    patch = root / 'change.patch'
    patch.write_bytes(''.join(difflib.unified_diff(
        base.decode().splitlines(True), final.decode().splitlines(True),
        fromfile='a/source.txt', tofile='b/source.txt')).encode())
    sha = lambda data: hashlib.sha256(data).hexdigest()
    lock = root / 'deps.lock.json'
    lock.write_text(json.dumps({'ps2recomp': {'commit': pin, 'patches': [{
        'file': patch.name, 'sha256': sha(patch.read_bytes()),
        'base_files': {'source.txt': sha(base)}, 'files': {'source.txt': sha(final)}
    }]}}))
    command = ('powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
               str(script), '-SourceDir', str(sdk), '-LockFile', str(lock))
    run(*command, '-Check', ok=False)
    run(*command)
    assert source.read_bytes() == final
    run(*command)  # idempotent
    run(*command, '-Check')
    source.write_bytes(b'unexpected\n')
    run(*command, '-Check', ok=False)
    run(*command, ok=False)
    assert source.read_bytes() == b'unexpected\n'
    source.write_bytes(final)
    run(*command, '-Revert')
    assert source.read_bytes() == base
    patch.write_bytes(patch.read_bytes() + b'corrupt\n')
    run(*command, ok=False)
    assert source.read_bytes() == base
print('SDK patch regressions passed')
