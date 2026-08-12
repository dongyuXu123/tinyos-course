# 模块：内存管理

## 职责

管理页表、物理页分配、用户地址空间和写时复制所需的页引用语义。

## 主要源码

- `source/mm/memory.c`
- `source/mm/page.s`
- `source/include/linux/mm.h`
- `source/include/asm/segment.h`

## 关注点

阅读页目录/页表建立、页面共享、写保护和缺页处理；把每个物理地址、线性地址和页表项权限写成状态表。

## 地址空间分层

阅读时分开记录五种概念：

1. 物理页分配范围（`LOW_MEM`、`mem_map`）；
2. task 的逻辑地址空间和 segment base；
3. page directory/page table 的线性地址映射；
4. fork 后共享页的写保护状态；
5. `mem_map` 引用计数和 COW 缺页路径。

Boot 的初始分页只提供早期映射；用户地址空间和 COW 在 `fork.c`/`memory.c` 后续建立。

## 依赖

Boot 阶段必须先建立可用的地址转换；进程模块依赖地址空间复制；文件和 exec 依赖用户内存可写入。

## 只读练习

```bash
grep -nE 'get_free_page|put_page|copy_page_tables|do_no_page' source/mm/memory.c
```
