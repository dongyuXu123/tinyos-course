# 注释：`mm/memory.c`

- 源码：`linux011-course/source/mm/memory.c`
- 版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`
- 角色：页表操作、物理页分配和共享/复制语义。

```bash
grep -nE 'get_free_page|put_page|copy_page_tables|do_no_page|do_wp_page' linux011-course/source/mm/memory.c
```

为每个函数记录物理页、线性地址、页表项和引用计数的变化。
