# Lesson 94: 文件权限与访问检查 — 精讲文档

> **课号**：Lesson 94 ｜ **主题**：文件权限与访问检查（file permissions and access checks）
> **课程主线位置**：VFS 阶段（Lesson 88–97），本课为 Lesson 87 原型的检查点
> **前置课程**：[`../lesson-93-stable/README.md`](../lesson-93-stable/README.md)（mount namespace 元数据）
> **后续课程**：[`../lesson-95-stable/README.md`](../lesson-95-stable/README.md)（文件打开与 file_operations）
> **一句话目标**：精讲 Unix 文件权限位（rwx 三元组）与 Linux 访问检查链（`permission`→`generic_permission`→`may_open`），并在教学内核中对照 `inode_model.mode`（存储但不强制）与 VMA `prot`/uaccess `permission`（真正执行的检查）两个层面。

本课是稳定快照（stable snapshot）检查点，也是 VFS 阶段第七课。`kernel64.c` 相对上一课仅做一处增量——把 `l93test` 恢复为 `l86test`，新增 `lesson_87_model` 状态与 `l94test` 检查点测试，并更新 `about`/开机横幅为本课主题。**如实说明**：本课是「检查点课：机制继承 + 本课检查点增量」——文件侧权限是「**存储但不强制**」：`vfs_init` 把 `inode_table[].mode` 置为 `0100644`，`fd_open_model` 只记录 `flags` 而**不做** mode 校验；真正执行「权限/访问检查」逻辑的是内存侧的 `uaccess_validate`（`permission` 位）与 `pf_classify`（VMA `prot` 检查→`PF_PROTECTION`）。本课以「权限位 → 检查链」为线索，把这两侧对照起来精讲。继承的进程、GUI、子系统回归保持有效。

**勘误说明**：旧 README 标注的检查点命令为 `l87test`，但实际源码中本课检查点命令是 `l94test`（`l87test` 到 Lesson 95 才出现）。本文以源码为准：本课检查点命令为 `l94test`，历史回归命令 `l86test`（恢复自上一课 `l93test`）。

---

## 1. 课程定位（Mission）

**学完本课你能**：读出 `0100644` 的每一位含义（文件类型 + 属主/属组/其他 rwx）；说出 Linux 中 `open(O_RDONLY)` 的权限检查发生在哪几个函数（`inode_permission`→`generic_permission`，`may_open`）；解释教学内核文件侧为何「只存不查」、内存侧为何真的检查（`uaccess_validate`/`pf_classify`）；运行 `l94test`/`ptrtest`/`copytest`/`pfmodel` 验证。

**在课程主线中的位置**：Lesson 88–93 完成了 VFS 的「结构（四层表）→ 注册（superblock/fs_type）→ 路径（dentry/解析）→ 命名空间」讲解。本课进入「**打开之前**」的守卫环节——权限检查。Linux 中 `open()` 在找到 dentry 之后、创建 file 之前必须过 `may_open()`/`permission()`；教学内核文件侧把这个守卫简化为「不检查」，但内存侧保留了完整的 `prot` 位检查。下一课（Lesson 95）讲「打开之后」——文件打开与 file_operations。

**前置知识清单**（学本课前必须掌握）：
1. inode 模型：`struct inode_model { ino,size,mode,refs }` 与 `vfs_init` 的初始化（Lesson 88/90）。
2. VMA 模型：`struct vma_model { start,end,prot,kind }`，`VMA_R/VMA_W/VMA_X` 位（Lesson 42/66）。
3. uaccess 模型：`uaccess_validate`/`uaccess_copy` 的四项校验（canonical/range/VMA/permission，Lesson 42）。
4. 打开路径：`fd_open_model` 的 inode→file→fd 分配（Lesson 88）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 94: 文件权限与访问检查`；
- 新命令 `l94test` 输出 `l94test: bounded VFS, devices, epoll, and service management checkpoint passed`（或其 fallback）；
- `ptrtest`/`copytest`/`pfmodel` 展示真实的权限/访问检查：VMA `prot` 位判定 `PF_PROTECTION`、uaccess `permission` 位判定读写是否被允许。

---

## 2. 核心概念精讲

### 2.1 Unix 文件权限位：mode 三元组

**直觉**：每个文件除了内容还有「谁能碰它」的元数据。Unix 用 12 个比特（`mode_t`）表达：3 比特文件类型 + 9 比特权限（属主/属组/其他各 3 位 rwx）。

**`0100644` 逐位拆解**（八进制）：
```
0   1   0   0   6   4   4
│   └─┬─┘   └─┬─┘ └─┬─┘
类型  │      属主  属组  其他
      │      rw-   r--   r--
      │      6=110 4=100 4=100
      └─ 特殊位（setuid/setgid/sticky，此处 0）
```
1. 前缀 `0100000`（八进制）→ `S_IFREG` 普通文件；
2. `6`（`rw-`）：属主可读可写；
3. `4`（`r--`）：属组可读；
4. `4`（`r--`）：其他可读。

对照 Linux `include/linux/fs.h`：`struct inode` 的 `i_mode`（`umode_t`），宏 `S_IRUSR=00400`、`S_IWUSR=00200`、`S_IXUSR=00100`……以及 `S_IFMT`/`S_IFREG`。

### 2.2 访问检查链：permission → generic_permission → may_open

**Linux 打开流程的守卫环节**（对照 `fs/open.c`/`fs/namei.c`）：
1. `open()` → `filp_open()` → `path_openat()` → `may_open()`；
2. `may_open()` 先用 `acc_mode` 检查：`inode_permission()`（`fs/namei.c`）→ `generic_permission()` → `acl_permission_check()`——把 inode 的 `i_mode` 与当前进程的 `fsuid`/`fsgid`/groups 比对；
3. 目录查找途中每走一级还要 `may_lookup`/`inode_permission` 检查执行（`x`）位——路径遍历权限；
4. 返回 `-EACCES`/`-EPERM` 则打开失败。

**关键语义**：权限检查需要「请求者身份」——内核必须知道当前进程的 uid/gid 与 umask；教学内核没有用户身份模型，这正是文件侧无法真正检查的根因。

### 2.3 教学模型的两侧对照

| 层面 | 载体 | 检查行为 | 结论 |
|---|---|---|---|
| 文件侧 | `inode_model.mode`（`0100644`）、`fd_open_model` 的 `flags` | **只存储，不强制**：`fd_open_model` 不比对 flags 与 mode | 权限位存在但无执行者 |
| 内存侧 | `vma_model.prot`（`VMA_R/W/X`）、`uaccess_result.permission` | **真的检查**：`pf_classify` 按 `prot` 判 `PF_PROTECTION`；`uaccess_validate` 按 `prot` 设 `permission` 位 | 教学内核里「能读/能写」的守卫 |

这个对照是本课的核心教学点：**权限模型（mode 位）与权限执行（检查函数）是两回事**。Linux 两者都有；教学模型只有「位」没有「文件检查」，但保留了「检查」在内存侧的一个完整实例。

### 2.4 检查点模型：lesson_87_model

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，`l94test` 成功串沿用 VFS/设备阶段主题（Origin 编号 Lesson 87）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | 恢复 `l86test`；新增 `lesson_87_model`/`lesson_87_state`/`l94test`；`about` 与横幅更新。权限/访问检查机制由累积代码承载（mode 位 + VMA prot + uaccess permission），本课以「权限与检查」视角精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`文件权限与访问检查`/`Lesson 94`/`l94test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（文件权限/访问检查视角 + 本课增量）

#### 3.2.1 权限位的存储：inode_model.mode 与 vfs_init

```c
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
static TEXT64 void vfs_init(void){u32 i;for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}
```
1. **权限位确实存在**：`inode_table[i]` 的第三个字段 `mode=0100644`——普通文件、属主 `rw-`、属组 `r--`、其他 `r--`，对应 Linux `i_mode` 的一个具体值。
2. **但它只被存储**：全内核搜索 `mode` 的读取点会发现没有任何函数拿它和请求的访问方式比较——这就是「只存不查」。Linux 中该字段被 `generic_permission()` 消费。
3. **为什么统一 0100644**：教学内核没有 chmod/chown、没有多用户，所有 inode 用同一个宽松权限位，保证任何路径都能打开——权限位在此是「模型完整性」而非「安全机制」。

#### 3.2.2 打开时不检查：fd_open_model

```c
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){file_table[f]=(struct file_model){inode,0,flags,1,1};fd_table[i]=(struct fd_model){f,1};inode_table[inode].refs++;fd_opens++;return (int)i;}}return -1;}
```
1. `flags` 参数就是调用者的「访问方式请求」（对应 `open()` 的 `O_RDONLY/O_WRONLY/O_RDWR`）；它被原样存进 `file_table[f].flags`。
2. **缺失的检查**：调用者传什么 flags 都放行——模型没有 `may_open()` 那样的 `acc_mode` vs `i_mode` 比对，也没有 `-EACCES`。
3. 与 Linux 对照：`fs/open.c` 的 `may_open()` 会检查 `acc_mode` 与 `i_mode` 的读写位（`FMODE_READER/FMODE_WRITER` 建立于 `inode_permission` 之后）；教学模型把这一步整个省略。

#### 3.2.3 真正的访问检查（内存侧）：vma_lookup / vma_range_valid / pf_classify

```c
static TEXT64 const struct vma_model *vma_lookup(u64 va){u32 i;for(i=0;i<vma_count;i++)if(vma_table[i].valid&&va>=vma_table[i].start&&va<vma_table[i].end)return &vma_table[i];return 0;}
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
```
1. `vma_lookup` 在 `vma_table`（3 个 VMA：code `r-x`、data `rw-`、stack `rw-`）中按地址找归属区间——对应 Linux `find_vma()`。
2. **权限检查在这里真实发生**：`pf_classify` 对写访问要求 `prot&VMA_W`、对读访问要求 `prot&VMA_R`，不满足即归为 `PF_PROTECTION`（计数 `fault_protection++`）。
3. 教学测试 `pfmodel` 断言：对 `VMA_DATA_START`（`rw-`）写访问是 `PF_NOT_PRESENT`（页不在），对 `VMA_CODE_START`（`r-x`）写访问是 `PF_PROTECTION`（**权限拒绝**），对 `0x00100000` 是 `PF_UNMAPPED`——一页代码同时演示「权限检查」三分类。
4. 对照 Linux `mm/memory.c` 的 `do_page_fault`→`handle_mm_fault` 路径：先查 VMA（`find_vma`，无则 `SEGV_MAPERR`），再查 `vma->vm_flags` 的 `VM_READ/VM_WRITE`（违反则 `SEGV_ACCERR`）——`pf_classify` 正是这两步的教学压缩。

#### 3.2.4 带权限位的复制校验：uaccess_validate

```c
static TEXT64 int uaccess_validate(u64 address,u64 length,u8 access,struct uaccess_result*r){u64 end;const struct vma_model*v;r->address=address;r->length=length;r->access=access;r->canonical=address<=USER_CANONICAL_MAX&&(length==0||address<=USER_CANONICAL_MAX-length+1);r->range=length<=USER_COPY_MAX&&address<USER_RANGE_MAX&&length<=USER_RANGE_MAX-address&&address+length>=address;r->vma=0;r->permission=0;r->copied=0;if(r->canonical&&r->range&&length){end=address+length;v=vma_lookup(address);if(v&&end<=v->end){r->vma=1;r->permission=(access==UACCESS_READ?(v->prot&VMA_R):(v->prot&VMA_W))!=0;}}else if(r->canonical&&r->range&&!length){r->vma=1;r->permission=1;}return r->canonical&&r->range&&r->vma&&r->permission;}
```
1. **四项校验**：`canonical`（地址规范）/`range`（长度界内）/`vma`（落在某个 VMA）/`permission`（读写位匹配），全部通过才返回 1。
2. **permission 位就是访问检查**：`access==UACCESS_READ` 时要求 `v->prot&VMA_R`，`UACCESS_WRITE` 时要求 `v->prot&VMA_W`——与 Linux `access_ok` 之外的 `__access_remote_vm`/`copy_to_user` 前置检查同构，但模型**绝不解引用指针**。
3. `ptrtest` 断言四组：读 data（通过）、非规范地址（拒）、超长（拒）、**写 code（`r-x`，拒）**——最后一条正是「权限拒绝」用例：
   `ptrtest: canonical/range/VMA/permission checks passed`。
4. `copytest` 在 `uaccess_validate` 之上计数成功/失败（写 data 成功、写 code 失败、读非规范失败、超长失败），输出 `copytest: copy_to_user/from_user bounded success/failure accounting passed`。

#### 3.2.5 本课新增检查点函数

```c
struct lesson_87_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_87_model lesson_87_state;
static TEXT64 void l94test(u16*c){lesson_87_state=(struct lesson_87_model){87U,88U,89U,90U,1,1,1,1};int ok=lesson_87_state.valid&&lesson_87_state.active&&lesson_87_state.ready&&lesson_87_state.accounted&&lesson_87_state.b==lesson_87_state.a+1U;text64(c,"l94test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 87 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 87→90（Origin 编号 Lesson 87），四布尔位全置 1。
2. **成功串**：`l94test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback `Lesson 87 fallback reported`。
3. **恢复的 `l86test`**：本课同时恢复 `l86test`（`lesson_86_state`，86→89，由上一课 `l93test` 更名而来），历史检查点可独立运行。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 94: 文件权限与访问检查\n`；检查点分支：
```c
else if(eq64(word,"l86test")){if(!noargs64(arg))usage64(c,"l86test");else l86test(c);}else if(eq64(word,"l94test")){if(!noargs64(arg))usage64(c,"l94test");else l94test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 94: 文件权限与访问检查\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 依次断言 `grub-file --is-x86-multiboot2` 通过、README 含 `文件权限与访问检查` 与 `Lesson 94`、kernel64.c 含 `l94test`，最后打印 `Multiboot2 and Lesson 94 checks passed.`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ module_init_model() → pmm/vma（3 个带 prot 的 VMA）→ vfs_init()（mode=0100644）
 ├─ 横幅 "Lesson 94: 文件权限与访问检查"
 └─ 主循环：命令 → exec64
     ├─ l94test → 阶段检查点
     ├─ pfmodel → pf_classify 权限三分类（NOT_PRESENT/PROTECTION/UNMAPPED）
     ├─ ptrtest/copytest → uaccess_validate 的 permission 位检查
     └─ fdtest/fdinfo → 文件侧「只存不查」的打开路径
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`vma_init()` 建 3 个带 `prot` 的 VMA（code `r-x`、data `rw-`、stack `rw-`），`vfs_init()` 把 inode 的 `mode` 置 `0100644`，打印横幅 `Lesson 94: 文件权限与访问检查`。
2. **`l94test`** → `l94test(c)` → 断言 `lesson_87_state` → `l94test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`pfmodel`** → `pf_classify(VMA_DATA_START,1)`（data 写：`PF_NOT_PRESENT`）、`pf_classify(VMA_CODE_START,1)`（code 写：`PF_PROTECTION`）、`pf_classify(0x00100000,0)`（未映射：`PF_UNMAPPED`）+ `fault_insert` → `pfmodel: not-present/protection/unmapped classified; bounded page inserted`。
4. **`ptrtest`** → 4 次 `uaccess_validate`（含「写 code 拒绝」）→ `ptrtest: canonical/range/VMA/permission checks passed`。
5. **`copytest`** → 4 次 `uaccess_copy`（写 data 成、写 code 败、读非规范败、超长败）→ `copytest: copy_to_user/from_user bounded success/failure accounting passed`。
6. **`fdtest`** → 文件侧打开不查权限 → `fdtest: fd/file/inode/dentry refs and offsets passed`。
7. **`about`** → `Lesson 94: 文件权限与访问检查`。

---

## 5. 构建、运行与验证

**依赖**：同前几课（`build-essential gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86`）。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 预期最终输出：
```
Multiboot2 and Lesson 94 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 94: 文件权限与访问检查` 横幅 |
| `l94test` | `l94test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l86test` | `l86test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `pfmodel` | `pfmodel: not-present/protection/unmapped classified; bounded page inserted` |
| `ptrtest` | `ptrtest: canonical/range/VMA/permission checks passed` |
| `copytest` | `copytest: copy_to_user/from_user bounded success/failure accounting passed` |
| `fdtest` | `fdtest: fd/file/inode/dentry refs and offsets passed` |
| `about` | `Lesson 94: 文件权限与访问检查` |

判定成功：`l94test`/`pfmodel`/`ptrtest`/`copytest` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l94test` 输出 `Lesson 87 fallback reported` | `lesson_87_state` 初始化/断言失败（stale 镜像） | `grep -n "l94test" kernel64.c`；确认初始化串 `{87U,88U,89U,90U,1,1,1,1}` |
| `pfmodel` 输出 `BROKEN` | `pf_classify` 的 VMA 匹配或 prot 判断错误 | 对照 `vma_init` 的 3 个 VMA（code `r-x`/data `rw-`/stack `rw-`）与 `pf_classify` 的 `VMA_W`/`VMA_R` 位判断 |
| `ptrtest` 输出 `BROKEN` | `uaccess_validate` 的 canonical/range/vma/permission 任一项判错 | 重点检查「写 `VMA_CODE_START`」用例：`access==UACCESS_WRITE` 时要求 `prot&VMA_W`，code 无写位应拒 |
| `copytest` 输出 `BROKEN` | `uaccess_copy` 成功/失败计数不符 | 对照 `uaccess_validate` 返回值与 `uaccess_attempts/successes/failures` 计数位置 |
| 误以为 `fd_open_model` 会拦权限 | 文件侧「只存不查」是设计 | `grep -n "mode" kernel64.c` 确认没有比对 flags 与 mode 的代码；这是与 Linux `may_open` 的差距 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 94' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `文件权限与访问检查` 与 `Lesson 94` |
| 误敲 `l87test` 无响应 | `l87test` 在本课源码中不存在（Lesson 95 才引入） | 本课检查点命令是 `l94test`；旧 README 的 `l87test` 标注已勘误 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `inode_model.mode=0100644`（存储） | `include/linux/fs.h` 的 `struct inode.i_mode`（`umode_t`）、`S_IRUSR`/`S_IWUSR`/`S_IRGRP` 宏 | 模型只存不查，无 chmod/chown/umask |
| `fd_open_model` 不比对 flags 与 mode | `fs/open.c` 的 `may_open()`（`acc_mode` 检查）；`fs/namei.c` 的 `inode_permission()`→`generic_permission()`→`acl_permission_check()` | 模型无 uid/gid/groups，无 `-EACCES` 返回 |
| `vma_lookup` 按地址查区间 | `mm/mmap.c` 的 `find_vma()` | 模型 3 个静态 VMA，无红黑树 |
| `pf_classify` 的 `prot&VMA_W/R` → `PF_PROTECTION` | `mm/memory.c` 的 `do_page_fault`→`handle_mm_fault`：`find_vma`（无则 `SEGV_MAPERR`）→`vm_flags` 读写位（违反则 `SEGV_ACCERR`） | 模型不真正触发 #PF，只做元数据分类 |
| `uaccess_validate.permission` 位 | `arch/x86/lib/usercopy_64.c` 的 `copy_to_user`/`copy_from_user`；`linux/uaccess.h` 的 `access_ok` | 模型绝不解引用指针，只验证元数据 |
| `ptrtest`/`copytest` 的 permission 用例 | LTP/`tools/testing/selftests` 的 access 类测试 | 模型把权限检查断言固化进内核 |
| `l94test` 断言 | 无直接对应 | 模型把权限主题检查点固化 |

**权威来源**：Linux `include/linux/fs.h`、`fs/open.c`、`fs/namei.c`、`mm/memory.c`、`mm/mmap.c` 为对照；Multiboot2 规范与 Intel SDM（页级 U/S、R/W 位）仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么「权限位存在」不等于「权限被检查」？结合 `fd_open_model` 只存 `flags` 不比对 `mode`，说明教学模型在哪个环节缺失了 Linux 的 `may_open`。
2. **源码定位**：在 `kernel64.c` 中找出全部带 `prot` 的 VMA 及它们的值，列出「读/写 code 区」分别被 `pf_classify` 与 `uaccess_validate` 判定为什么结果。
3. **动手实验**：把 `vma_init` 中 code 区改为 `VMA_R|VMA_W|VMA_X`，重新构建运行 `pfmodel`/`ptrtest`，观察对 code 的写访问从 `PF_PROTECTION`/拒绝变为允许。
4. **动手实验**：在 `fd_open_model` 中加一行 flags 检查（如 `flags==1` 要求 `inode_table[inode].mode&0400`），观察现有 `fdtest`/`shelltest` 是否仍通过——体会「补上文件权限检查」需要哪些额外身份信息。
5. **Linux 对照**：阅读 `fs/namei.c` 的 `generic_permission()`，列出它除了 rwx 位还检查了什么（如 `i_op->permission` 钩子、ACL、所有权），并说明教学模型为何无法表达这些。

---

## 9. 本课小结与下一课预告

1. 本课讲清 Unix 权限位（`0100644` = 普通文件 + 属主 rw + 属组 r + 其他 r）与 Linux 检查链（`may_open`→`inode_permission`→`generic_permission`）。
2. 教学模型是「权限位存储，文件侧不检查」：`mode` 字段存在但无 `may_open` 对应物——缺的是 uid/gid/身份模型。
3. 教学模型在内存侧提供了**真实的访问检查**：`pf_classify` 的 `PF_PROTECTION` 与 `uaccess_validate` 的 `permission` 位，是 Linux `find_vma`+`vm_flags` 检查的教学压缩。
4. 「权限模型」与「权限执行」分离是本课的核心洞见：位定义≠守卫，守卫需要执行者与身份上下文。
5. `l94test` 沿用 VFS/设备阶段检查点家族，`l86test` 历史检查点保留。
6. 下一课（[`../lesson-95-stable/README.md`](../lesson-95-stable/README.md)，Lesson 95）讲 **文件打开与 file_operations**（对照 `fs/open.c` 与 `include/linux/fs.h` 的 `struct file_operations`）——打开动作本身的对象模型。
