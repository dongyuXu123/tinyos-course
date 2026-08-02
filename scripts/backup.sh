#!/bin/sh
# 每课冻结后的 GitHub 备份脚本。
# 用法: scripts/backup.sh "提交说明" [tag1 tag2 ...]
# 示例: scripts/backup.sh "Lesson 14 stable: bitmap PMM" lesson-14-stable
set -e
cd "$(dirname "$0")/.."

msg="$1"
[ -n "$msg" ] || { echo "usage: $0 <commit message> [tag...]" >&2; exit 1; }
shift

git add -A
git commit -m "$msg"
for t in "$@"; do
    git tag -f "$t" 2>/dev/null || true
    git push -f origin "refs/tags/$t" 2>/dev/null || git push origin "refs/tags/$t"
done
git push origin main
echo "backup OK: $(git rev-parse --short HEAD)"
