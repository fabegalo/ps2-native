"""Small synthetic regression for the locked SDK patch workflow; no real SDK edits."""
import difflib
import hashlib
import json
from pathlib import Path
import shutil
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
    # Upstream keeps some files CRLF in Git; LF patches must still apply to them.
    crlf = sdk / 'crlf.txt'
    crlf.write_bytes(b'one\r\ntwo\r\n')
    run('git', '-C', str(sdk), 'add', '.')
    run('git', '-C', str(sdk), '-c', 'user.name=Fixture', '-c',
        'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture')
    pin = run('git', '-C', str(sdk), 'rev-parse', 'HEAD')
    patch = root / 'change.patch'
    patch.write_bytes(''.join(difflib.unified_diff(
        base.decode().splitlines(True), final.decode().splitlines(True),
        fromfile='a/source.txt', tofile='b/source.txt')).encode() +
        b'--- /dev/null\n+++ b/added.txt\n@@ -0,0 +1 @@\n+new source\n')
    # Touches a file of the first patch: rejected by one batched git apply.
    second = root / 'second.patch'
    second.write_bytes(b'--- a/added.txt\n+++ b/added.txt\n@@ -1 +1,2 @@\n new source\n+more\n'
                       b'--- a/crlf.txt\n+++ b/crlf.txt\n@@ -1,2 +1,2 @@\n one\n-two\n+three\n')
    sha = lambda data: hashlib.sha256(data).hexdigest()
    lock = root / 'deps.lock.json'
    lock.write_text(json.dumps({'ps2recomp': {'commit': pin, 'patches': [{
        'file': patch.name, 'sha256': sha(patch.read_bytes()),
        'base_files': {'source.txt': sha(base), 'added.txt': ''},
        'files': {'source.txt': sha(final), 'added.txt': sha(b'new source\n')}
    }, {
        'file': second.name, 'sha256': sha(second.read_bytes()),
        'base_files': {'added.txt': sha(b'new source\n'), 'crlf.txt': sha(b'one\ntwo\n')},
        'files': {'added.txt': sha(b'new source\nmore\n'), 'crlf.txt': sha(b'one\nthree\n')}
    }]}}))
    command = (shutil.which('pwsh') or 'powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
               str(script), '-SourceDir', str(sdk), '-LockFile', str(lock))
    run(*command, '-Check', ok=False)
    run(*command)
    assert source.read_bytes() == final
    assert (sdk / 'added.txt').read_bytes() == b'new source\nmore\n'
    assert crlf.read_bytes().replace(b'\r', b'') == b'one\nthree\n'
    run(*command)  # idempotent
    run(*command, '-Check')
    source.write_bytes(b'unexpected\n')
    run(*command, '-Check', ok=False)
    run(*command, ok=False)
    assert source.read_bytes() == b'unexpected\n'
    source.write_bytes(final)
    run(*command, '-Revert')
    assert source.read_bytes() == base
    assert not (sdk / 'added.txt').exists()
    assert crlf.read_bytes().replace(b'\r', b'') == b'one\ntwo\n'
    # A later patch that does not apply rolls back the earlier ones.
    bad = b'--- a/added.txt\n+++ b/added.txt\n@@ -1 +1 @@\n-missing\n+x\n'
    lock.write_text(lock.read_text().replace(sha(second.read_bytes()), sha(bad)))
    second.write_bytes(bad)
    run(*command, ok=False)
    assert source.read_bytes() == base and not (sdk / 'added.txt').exists()
    patch.write_bytes(patch.read_bytes() + b'corrupt\n')
    run(*command, ok=False)
    assert source.read_bytes() == base
print('SDK patch regressions passed')
