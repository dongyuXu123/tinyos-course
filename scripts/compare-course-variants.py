#!/usr/bin/env python3
"""Compare paired TinyOS lesson trees without modifying course directories."""
from __future__ import annotations
import argparse, difflib, hashlib, re
from pathlib import Path

IGNORED_PARTS={"build"}; IGNORED_NAMES={".tinyos-screen-", ".tinyos-vga-"}
STATUS_RE=re.compile(r"(?m)^(> \*\*Course status: ).*(\*\*\.?\s*)$")
ID_RE=re.compile(r"^lesson-(.+)-(stable|learning)$")

def ignored(path):
    return any(part in IGNORED_PARTS for part in path.parts) or any(path.name.startswith(p) for p in IGNORED_NAMES)
def files(root):
    return {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and not ignored(p.relative_to(root))}
def normalized(path, rel):
    data=path.joinpath(rel).read_bytes()
    if rel=='README.md': data=STATUS_RE.sub(r'\1<variant status>\2',data.decode('utf-8')).encode()
    return data
def classify(learning, stable):
    lf,sf=files(learning),files(stable); all_files=sorted(lf|sf)
    missing=[f for f in all_files if f not in lf or f not in sf]
    changed=[f for f in all_files if f in lf and f in sf and normalized(learning,f)!=normalized(stable,f)]
    substantive=[f for f in changed if f!='README.md']+missing
    return ('identical' if not changed else 'documentation-only') if not substantive else 'source-difference',changed,missing
def short_diff(learning,stable,rel):
    if rel not in files(learning) or rel not in files(stable): return 'file added/removed'
    a=normalized(learning,rel).decode(errors='replace').splitlines(); b=normalized(stable,rel).decode(errors='replace').splitlines()
    diff=list(difflib.unified_diff(b,a,n=1)); meaningful=[x for x in diff if x.startswith(('+','-')) and not x.startswith(('+++','---'))]
    return ' '.join(x[1:].strip() for x in meaningful[:2])[:180] or 'binary or formatting difference'
def natural_id(value):
    return tuple(int(x) if x.isdigit() else x for x in re.split(r'(\d+)',value))
def main():
    p=argparse.ArgumentParser(); p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]); p.add_argument('--output',type=Path); args=p.parse_args()
    lessons=args.root/'lessons'; ids={}
    for d in lessons.iterdir():
        if not d.is_dir(): continue
        m=ID_RE.match(d.name)
        if m: ids.setdefault(m.group(1),{})[m.group(2)]=d
    rows=[]
    for ident, pair in sorted(ids.items(),key=lambda x:natural_id(x[0])):
        if 'stable' not in pair: raise SystemExit(f'missing stable lesson: {ident}')
        if 'learning' not in pair: continue
        category,changed,missing=classify(pair['learning'],pair['stable']); rows.append((ident,category,changed,missing,pair['learning'],pair['stable']))
    output=args.output or args.root/'docs/learning-stable-diff-report.md'; output.parent.mkdir(parents=True,exist_ok=True)
    counts={}
    for _,c,*_ in rows: counts[c]=counts.get(c,0)+1
    with output.open('w',encoding='utf-8') as r:
        r.write('# Learning / stable 历史差异报告\n\n> 当前仓库只发布 stable；本报告只记录仍存在配对目录的历史审计结果。\n\n')
        r.write(f'- 自动发现 stable 课程：{len(ids)} 对象；仍有 learning/stable 配对：{len(rows)}。\n- 排除 `build/`、`.tinyos-screen-*`、`.tinyos-vga-*`；README 状态行归一化。\n\n')
        r.write('## 汇总\n\n| 分类 | 数量 |\n|---|---:|\n')
        labels={'identical':'完全相同','documentation-only':'仅文档差异','source-difference':'源码/结构差异'}
        for k in labels: r.write(f'| {labels[k]} (`{k}`) | {counts.get(k,0)} |\n')
        r.write('\n## 仍存在配对的逐课结果\n\n| Lesson | 分类 | 差异文件 |\n|---|---|---|\n')
        for ident,c,changed,missing,l,s in rows:
            details=[f'`{x}` — {short_diff(l,s,x)}' for x in changed]+[f'`{x}` missing' for x in missing]
            r.write(f'| {ident} | `{c}` | {"<br>".join(details) or "—"} |\n')
    print(f'wrote {output}; stable objects={len(ids)} pairs={len(rows)}')
if __name__=='__main__': main()
