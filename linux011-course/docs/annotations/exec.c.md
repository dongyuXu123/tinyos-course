# 注释：`fs/exec.c`

- 源码：`linux011-course/source/fs/exec.c`
- 版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`
- 角色：检查用户程序头、建立地址空间和用户栈，并切换到新程序入口。

```bash
grep -nE 'do_execve|copy_strings|change_ldt|executable' linux011-course/source/fs/exec.c
```

把 pathname、inode、程序头、参数字符串和新用户 RIP/EIP 的关系画成数据流。
