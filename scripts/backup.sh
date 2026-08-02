#!/bin/sh
# 每课冻结后的 GitHub 备份脚本。
# 用法: 先显式 git add 需要备份的文件，再运行：
#       scripts/backup.sh "提交说明" [tag1 tag2 ...]
# 示例: git add README.md lessons/lesson-14-learning lessons/lesson-14-stable
#       scripts/backup.sh "Lesson 14 stable: bitmap PMM" lesson-14-stable
set -e
cd "$(dirname "$0")/.."

msg="$1"
[ -n "$msg" ] || { echo "usage: $0 <commit message> [tag...]" >&2; exit 1; }
shift

git diff --cached --quiet && { echo "no staged changes; stage the intended checkpoint first" >&2; exit 1; }
git commit -m "$msg"
for t in "$@"; do
    if git rev-parse -q --verify "refs/tags/$t" >/dev/null; then
        echo "refusing to overwrite existing local tag: $t" >&2
        exit 1
    fi
    if git ls-remote --exit-code --tags origin "refs/tags/$t" >/dev/null 2>&1; then
        echo "refusing to overwrite existing remote tag: $t" >&2
        exit 1
    fi
    git tag "$t"
    git push origin "refs/tags/$t"
done
git push origin main
echo "backup OK: $(git rev-parse --short HEAD)"
