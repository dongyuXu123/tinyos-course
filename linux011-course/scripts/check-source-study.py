#!/usr/bin/env python3
"""Read-only integrity and documentation checks for the vendored Linux 0.11 study."""
from pathlib import Path
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'source'

required = [
    'README.md', 'Makefile', 'boot/bootsect.s', 'boot/setup.s', 'boot/head.s',
    'init/main.c', 'kernel/sched.c', 'kernel/fork.c', 'kernel/exit.c',
    'mm/memory.c', 'fs/exec.c', 'fs/buffer.c', 'fs/inode.c',
    'kernel/blk_drv', 'kernel/chr_drv', 'include', 'lib', 'tools/build.c',
]
for rel in required:
    if not (SRC / rel).exists():
        raise SystemExit(f'missing source path: {rel}')

for metadata in ('source-revision.txt', 'SOURCE-PROVENANCE.md'):
    if not (ROOT / metadata).is_file():
        raise SystemExit(f'missing provenance metadata: {metadata}')

revision = (ROOT / 'source-revision.txt').read_text(encoding='utf-8')
provenance = (ROOT / 'SOURCE-PROVENANCE.md').read_text(encoding='utf-8')
commit_match = re.search(r'^commit:\s*([0-9a-f]{40})\s*$', revision, re.MULTILINE)
provenance_match = re.search(r'commit:\s*([0-9a-f]{40})', provenance)
if not commit_match or not provenance_match:
    raise SystemExit('missing commit in source metadata')
if commit_match.group(1) != provenance_match.group(1):
    raise SystemExit('source-revision.txt and SOURCE-PROVENANCE.md disagree on commit')

sha = ROOT / 'source.sha256'
if not sha.is_file():
    raise SystemExit('missing source.sha256')
checksummed = []
seen = set()
for number, line in enumerate(sha.read_text(encoding='utf-8').splitlines(), 1):
    match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
    if not match:
        raise SystemExit(f'invalid checksum line {number}')
    digest, rel = match.groups()
    path = ROOT / rel
    try:
        path.relative_to(SRC)
    except ValueError:
        raise SystemExit(f'checksum path outside source/: {rel}')
    if rel in seen:
        raise SystemExit(f'duplicate checksum path: {rel}')
    seen.add(rel)
    checksummed.append(rel)
    if not path.is_file():
        raise SystemExit(f'missing checksummed file: {rel}')
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != digest:
        raise SystemExit(f'checksum mismatch: {rel}')
actual_files = sorted(str(path.relative_to(ROOT)) for path in SRC.rglob('*') if path.is_file())
if sorted(checksummed) != actual_files:
    missing = sorted(set(actual_files) - set(checksummed))
    extra = sorted(set(checksummed) - set(actual_files))
    raise SystemExit(f'checksum coverage mismatch: missing={missing}; extra={extra}')

md = list((ROOT / 'docs').rglob('*.md')) + [ROOT / 'README.md']
for p in md:
    text = p.read_text(encoding='utf-8')
    for link in re.findall(r'\]\(([^)#]+)', text):
        if not link.startswith(('http://', 'https://')) and not (p.parent / link).exists():
            raise SystemExit(f'{p.relative_to(ROOT)}: missing link {link}')

required_docs = [
    'source-map.md', 'startup-load-order.md', 'startup-source-tour.md',
    'module-index.md', 'call-graphs.md', 'image-layout.md', 'syscall-abi.md',
]
for rel in required_docs:
    if not (ROOT / 'docs' / rel).is_file():
        raise SystemExit(f'missing study doc: {rel}')

modules = ['boot', 'init', 'process-scheduler', 'memory', 'filesystem', 'devices-tty', 'traps-syscalls', 'user-space']
for name in modules:
    p = ROOT / 'docs/modules' / f'{name}.md'
    if not p.is_file():
        raise SystemExit(f'missing module summary: {name}')

annotations = {
    'bootsect.s.md': 'boot/bootsect.s',
    'setup.s.md': 'boot/setup.s',
    'head.s.md': 'boot/head.s',
    'main.c.md': 'init/main.c',
    'sched.c.md': 'kernel/sched.c',
    'memory.c.md': 'mm/memory.c',
    'exec.c.md': 'fs/exec.c',
    'build-image.md': 'tools/build.c',
}
for doc, source in annotations.items():
    p = ROOT / 'docs/annotations' / doc
    if not p.is_file():
        raise SystemExit(f'missing annotation: {doc}')
    if source not in p.read_text(encoding='utf-8'):
        raise SystemExit(f'annotation {doc} missing source anchor: {source}')

for anchor in ['bootsect.s', 'setup.s', 'head.s', 'main.c', 'startup_32', 'init(']:
    if anchor not in (ROOT / 'docs/startup-load-order.md').read_text(encoding='utf-8'):
        raise SystemExit(f'startup doc missing: {anchor}')
for anchor in ['system_call', 'sys_call_table', 'sys_open', 'sys_execve', 'sys_fork', '72']:
    if anchor not in (ROOT / 'docs/syscall-abi.md').read_text(encoding='utf-8'):
        raise SystemExit(f'syscall doc missing: {anchor}')
for anchor in ['bootsect.s', 'setup.s', 'startup_32', 'mem_init', 'mount_root', 'system_call', 'sys_call_table', 'do_execve']:
    if anchor not in (ROOT / 'docs/startup-source-tour.md').read_text(encoding='utf-8'):
        raise SystemExit(f'tour doc missing: {anchor}')

print(f'Linux 0.11 source files={len(actual_files)}; module summaries={len(modules)}; annotations={len(annotations)}: PASS')
