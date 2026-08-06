# Lesson 40: Linux 风格有界 execve/ELF 段校验与确定性用户栈布局 — 精讲文档

> **课号**：Lesson 40（可执行课）
> **主题**：Linux 风格有界 execve/ELF 段校验与确定性用户栈布局
> **课程主线位置**：第 5 阶段「Linux 风格内核抽象」第四课。前课完成「任务身份」
> （37）、「同步/调度抽象」（38）、「进程创建 fork/clone」（39）；本课完成
> 「换镜像」——模拟 `execve` 装载一个内嵌的微型 ELF，校验后得到确定性的
> argc/argv/envp 用户栈布局。fork + exec 合起来就是「创建并启动新程序」的
> 教学语义。
> **前置课程**：[`lesson-39-stable/README.md`](../lesson-39-stable/README.md)
> **后续课程**：[`lesson-41-stable/README.md`](../lesson-41-stable/README.md)
> **一句话目标**：能讲清楚 Linux `execve` 的「校验镜像 → 装载段 → 搭用户栈」三步，
> 并在 TinyOS 里用 `exec_model` + 内嵌 `embedded_exec_image` 复刻前三步的**元数据
> 版**——ELF magic/type/machine、程序头/段边界、入口范围全部校验，用户栈按固定
> `argc, argv[], NULL, envp[]` 布局，**绝不执行装载的字节**。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能回答——`execve` 在内核里到底做什么？答：读入新镜像并
逐字段校验（ELF 头、程序头表、段、入口），用校验结果替换当前进程的地址空间内容，
再按 SysV ABI 在用户栈顶搭 `argc/argv/envp`。TinyOS 用 `exec_validate()` 做同样的
字段校验，用 `exec_model` 记录入口/栈指针/argv/envp 指针，但**只产生元数据**。

- **在课程主线中的位置**：承接 lesson-39 的 fork（创建档案），本课 exec（换镜像）；
  lesson-41 在此基础上做 VMA 与缺页分类，lesson-42 做 user-pointer 校验——四课
  连成「进程内存管理」的 Linux 教学主线。
- **前置知识清单**：
  1. ELF 文件格式常识：`e_ident`（magic 7f 'E' 'L' 'F'）、`e_type`（ET_EXEC=2）、
     `e_machine`（EM_X86_64=62）、程序头表 `phoff/phentsize/phnum`、段
     `p_type/p_flags/p_vaddr/p_filesz/p_memsz`；
  2. SysV x86_64 进程启动约定：用户栈从高地址向下，栈顶依次是 `argc`、
     `argv[0..n]`、NULL、`envp[0..n]`、NULL；
  3. lesson-34/36 的 `USER_CODE_VA=0x400000`、`USER_STACK_VA=0x800000`、
     `USER_STACK_TOP` 常量；
  4. lesson-39 的 `exec64` 命令分发机制。
- **本课交付**：`execinfo`/`exectest`/`stacklayout` 三条命令；内嵌
  `embedded_exec_image`（微型 ELF64）；`exec_validate()`/`exec_stack_validate()`；
  确定性用户栈布局（argc=2、sp=0x800fc0、argv=sp+8、envp=argv+24）。

---

## 2. 核心概念精讲

### 2.1 概念一：ELF 是「自描述」的文件格式

ELF 头部有足够信息让装载器（loader）逐项验证与定位：

| ELF 字段 | 值（本课） | 含义 |
|---|---|---|
| `e_ident[0..3]` | `0x7f 'E' 'L' 'F'` | 魔数 |
| `e_type` | `2`（ET_EXEC） | 可执行文件 |
| `e_machine` | `62`（EM_X86_64） | 目标机器 |
| `e_entry` | `0x400000` | 入口点 |
| `e_phoff` | `0x20` | 程序头表偏移 |
| `e_phentsize` / `e_phnum` | `0x28` / `2` | 程序头大小/数量 |

TinyOS 用 `struct tiny_elf_header` 精确建模这些字段（packed），并在
`embedded_exec_image` 里手写一份合法镜像。

### 2.2 概念二：程序头表与段的「范围合法性」

每个程序头（`PT_LOAD=1`）描述一段：`p_vaddr`（虚拟地址）、`p_filesz`（文件里的
字节数）、`p_memsz`（内存里的字节数，≥ filesz，多余部分 BSS 清零）、`p_flags`
（R/W/X）。装载器必须验证：

- `filesz ≤ memsz`（内存 ≥ 文件数据）；
- `offset + filesz` 不越出文件；
- `vaddr` 落在用户区、`vaddr + memsz` 不越出用户区（本课：`USER_CODE_VA` 与
  `USER_STACK_VA` 之间）；
- 段必须有 R 或 X，且**不能同时 W 和 X**（W^X 纪律）。

### 2.3 概念三：确定性用户栈布局（SysV ABI）

Linux 为每次 exec 在用户栈顶搭建（`fs/exec.c` 的 `create_elf_tables`）：

```text
高地址  USER_STACK_TOP (0x801000)
  │  envp[1] = NULL
  │  envp[0]
  │  argv[1] = NULL
  │  argv[0]     ← argv_pointer (sp+8)
  │  argc        ← stack_pointer (sp)
低地址
```

TinyOS 只**计算指针、不写内存**：

```text
stack_pointer = USER_STACK_TOP - EXEC_STACK_WORDS*8 = 0x801000 - 0x40 = 0x800fc0
argv_pointer  = stack_pointer + 8                      = 0x800fc8
envp_pointer  = argv_pointer + (argc+1)*8 = 0x800fc8 + 24 = 0x800fe0
```

### 2.4 概念四：只校验、不执行 —— 本课的边界

`exec_model.executed` 永远是 0；`execinfo` 显示 `execution: metadata only`；
没有任何指令会从 `embedded_exec_image` 的入口 `0x400000` 开始执行。原因是课程
还没有把真实进程的地址空间切换到新镜像（那是更深一层的机制），本课只交付
「镜像是否可装载」的判定。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-39） |
|---|---|---|
| `boot.S` / `kernel.c` | 引导 | 未变化 |
| `kernel64.c` | 64 位内核主体 | **核心**：ELF 常量/结构 + exec_model + 校验函数 + 3 命令 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | 未变化 |
| `grub.cfg` | 装载 | 未变化 |

### 3.2 常量 / 结构 / 镜像精讲

```c
#define ELF_MAGIC0 0x7f
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELF_TYPE_EXEC 2U
#define ELF_MACHINE_X86_64 62U
#define ELF_SEG_R 1U
#define ELF_SEG_W 2U
#define ELF_SEG_X 4U
#define EXEC_MAX_SEGMENTS 2U
#define EXEC_MAX_IMAGE_BYTES 64U
#define EXEC_STACK_WORDS 8U
#define EXEC_STACK_ARGC 2U
struct tiny_elf_header { u8 ident[4]; u16 type, machine; u32 version;
    u64 entry, phoff; u16 phentsize, phnum; } __attribute__((packed));
struct tiny_elf_segment { u32 type, flags; u64 offset, vaddr, filesz, memsz; } __attribute__((packed));
struct exec_model { u64 entry, stack_pointer, argv_pointer, envp_pointer;
    u32 argc, segment_count, image_bytes; u8 validated, executed; };
static struct exec_model exec_model;
```

- ELF 常量全部对齐真实规范值（`ET_EXEC=2`、`EM_X86_64=62`、`PF_R=4/PF_W=2/PF_X=1`
  在 Linux 里是 4/2/1，本课用 1/2/4 的位顺序，语义是「R/W/X 位掩码」）；
- `tiny_elf_header`/`tiny_elf_segment`：packed，与规范字节布局一致
  （header 40 字节 = 0x28，segment 56 字节）；
- `exec_model`：记录校验结果与用户栈指针，`executed` 恒为 0；
- `EXEC_MAX_IMAGE_BYTES 64U`：镜像上限 64 字节，有界装载。

内嵌镜像（源码逐字）：

```c
static const u8 embedded_exec_image[sizeof(struct tiny_elf_header)+2*sizeof(struct tiny_elf_segment)+8] = {
 ELF_MAGIC0,ELF_MAGIC1,ELF_MAGIC2,ELF_MAGIC3, ELF_TYPE_EXEC,0, ELF_MACHINE_X86_64,0, 1,0,0,0,
 0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00, 0x20,0,0,0,0,0,0,0, 0x28,0,2,0,
 0x01,0,0,0,ELF_SEG_R|ELF_SEG_X,0,0,0,0x00,0x00,0x40,0x00,7,0,0,0, 7,0,0,0,0,0,0,0,
 0x01,0,0,0,ELF_SEG_R|ELF_SEG_W,0,0,0,0x00,0x00,0x80,0x00,15,0,0,0, 15,0,0,0,0,0,0,0
};
```

逐字节解读（小端）：
- 前 4 字节 `7f 45 4c 46`：ELF 魔数；
- 第 5~6 字节 `02 00`：`e_type=ET_EXEC(2)`；第 7~8 字节 `3e 00`：`e_machine=62`；
- 第 9~12 字节 `01 00 00 00`：`e_version=1`；
- 第 13~20 字节 `00 00 40 00 00 00 00 00`：`e_entry=0x400000`；
- 第 21~28 字节 `20 00 00 00 00 00 00 00`：`e_phoff=0x20`；
- 第 29~32 字节 `28 00 02 00`：`e_phentsize=0x28`、`e_phnum=2`；
- 段 1（偏移 0x20 起）：`type=1(PT_LOAD)`、`flags=R|X=5`、`offset=0`、
  `vaddr=0x400000`、`filesz=7`、`memsz=7`（可执行的代码段）；
- 段 2（偏移 0x40 起）：`type=1`、`flags=R|W=3`、`offset=0`、
  `vaddr=0x800000`、`filesz=15`、`memsz=15`（可读写的栈区段）。

### 3.3 函数精讲：exec_validate —— ELF 镜像校验

```c
static TEXT64 int exec_validate(void){const struct tiny_elf_header*h=
    (const struct tiny_elf_header*)embedded_exec_image;u32 i;u64 end;
    if(h->ident[0]!=ELF_MAGIC0||h->ident[1]!=ELF_MAGIC1||h->ident[2]!=ELF_MAGIC2||
       h->ident[3]!=ELF_MAGIC3||h->type!=ELF_TYPE_EXEC||h->machine!=ELF_MACHINE_X86_64||
       h->version!=1||h->phentsize!=sizeof(struct tiny_elf_segment)||
       h->phnum>EXEC_MAX_SEGMENTS)return 0;
    if(h->phoff>sizeof(embedded_exec_image)||h->phnum>(sizeof(embedded_exec_image)-h->phoff)/h->phentsize)return 0;
    for(i=0;i<h->phnum;i++){const struct tiny_elf_segment*s=
        (const struct tiny_elf_segment*)(embedded_exec_image+h->phoff+i*h->phentsize);
        if(s->type!=1||!s->filesz||s->memsz<s->filesz||s->offset>sizeof(embedded_exec_image)||
           s->filesz>sizeof(embedded_exec_image)-s->offset||s->vaddr<USER_CODE_VA||
           s->vaddr>USER_STACK_VA||s->memsz>USER_STACK_VA-s->vaddr)return 0;
        if(!(s->flags&(ELF_SEG_R|ELF_SEG_X))||((s->flags&ELF_SEG_W)&&(s->flags&ELF_SEG_X)))return 0;
        end=s->vaddr+s->memsz;if(end<s->vaddr||end>USER_STACK_VA)return 0;}
    if(h->entry<USER_CODE_VA||h->entry>=USER_CODE_VA+EXEC_MAX_IMAGE_BYTES)return 0;
    exec_model.entry=h->entry;exec_model.segment_count=h->phnum;
    exec_model.image_bytes=sizeof(embedded_exec_image);
    exec_model.argc=EXEC_STACK_ARGC;
    exec_model.stack_pointer=USER_STACK_TOP-EXEC_STACK_WORDS*8;
    exec_model.argv_pointer=exec_model.stack_pointer+8;
    exec_model.envp_pointer=exec_model.argv_pointer+(exec_model.argc+1)*8;
    exec_model.validated=1;exec_model.executed=0;return 1;}
```

算法步骤（每步 ≥3 行分析）：
1. **ELF 头校验**：魔数四字节、`type==ET_EXEC`、`machine==EM_X86_64`、
   `version==1`、`phentsize` 必须等于 `sizeof(tiny_elf_segment)`（56 字节——
   防偏移错位）、`phnum<=EXEC_MAX_SEGMENTS`（2 段上限）；
2. **程序头表范围校验**：`phoff` 不越出镜像、`phnum*phentsize` 不越出
   `sizeof(embedded_exec_image)-phoff`——防程序头表读到镜像外；
3. **逐段校验**：`type==PT_LOAD(1)`；`filesz>0`；`memsz>=filesz`；
   `offset` 与 `filesz` 都在镜像内；`vaddr` 落在
   `[USER_CODE_VA, USER_STACK_VA]`；`memsz <= USER_STACK_VA - vaddr`（段不能
   越过用户区上限）；
4. **段权限校验（W^X）**：必须含 R 或 X；**禁止 W 与 X 同时置位**——这是
   Linux 现代内核默认的 W^X 纪律，教学模型显式执行；
5. **溢出防护**：`end=vaddr+memsz` 先查 `end<vaddr`（无符号回绕）再查
   `end>USER_STACK_VA`；
6. **入口校验**：`entry` 必须在 `[USER_CODE_VA, USER_CODE_VA+EXEC_MAX_IMAGE_BYTES)`
   ——入口必须指向代码段所在范围；
7. **填 exec_model**：记录 entry/segment_count/image_bytes，并按
   `EXEC_STACK_WORDS=8`、`EXEC_STACK_ARGC=2` 计算三个栈指针，置
   `validated=1`、`executed=0`。

**exec_stack_validate**（栈布局自检）

```c
static TEXT64 int exec_stack_validate(void){return exec_model.validated&&
    exec_model.argc==2&&exec_model.stack_pointer==USER_STACK_TOP-EXEC_STACK_WORDS*8&&
    exec_model.argv_pointer==exec_model.stack_pointer+8&&
    exec_model.envp_pointer==exec_model.argv_pointer+24;}
```

`argv_pointer+24 = argv_pointer+(argc+1)*8`（argc=2 时 3 个 8 字节槽：argv[0]、
argv[1]、NULL），`envp` 紧随其后。全部恒等式成立才返回真。

### 3.4 函数精讲：execinfo / stacklayout / exectest 分支

```c
static TEXT64 void execinfo(u16*c){text64(c,"exec model: ELF-like bounded image\nvalidated: ");
    text64(c,exec_model.validated?"yes":"no");
    text64(c,"\nsegments/bytes: ");hex64(c,exec_model.segment_count);
    text64(c," ");hex64(c,exec_model.image_bytes);
    text64(c,"\nentry: ");hex64(c,exec_model.entry);
    text64(c,"\nexecution: ");text64(c,exec_model.executed?"forbidden/none":"metadata only");
    putc64(c,'\n');}
static TEXT64 void stacklayout(u16*c){text64(c,"user stack layout: argc, argv[], NULL, envp[]\nargc: ");
    hex64(c,exec_model.argc);text64(c,"\nsp: ");hex64(c,exec_model.stack_pointer);
    text64(c,"\nargv: ");hex64(c,exec_model.argv_pointer);
    text64(c,"\nenvp: ");hex64(c,exec_model.envp_pointer);
    text64(c,"\nlayout validation: ");text64(c,exec_stack_validate()?"passed":"BROKEN");
    text64(c,"\nallocation/execution: none\n");}
```

- `execinfo`：显示镜像是否为 ELF 风格、是否已校验、段数/字节数、入口、执行状态；
- `stacklayout`：按行显示 argc/sp/argv/envp 与布局校验结果，最后一行声明
  `allocation/execution: none`；
- `exec64` 分支：`exectest` 输出（逐字）
  `exectest: ELF header/segments/entry/stack passed; no execution`
  或 `exectest: BROKEN`。

### 3.5 kernel_main 与横幅

`kernel_main64_binary` 在打印横幅前先跑一次 `(void)exec_validate();`
（开机即校验，失败也不停机，留到 `exectest` 观察）。横幅（源码逐字）：

```text
TinyOS lesson 40: Linux-style bounded execve/ELF loader model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

`about`：`TinyOS lesson 40: bounded Linux-style execve/ELF loader and deterministic user stack`

### 3.6 构建管线（Makefile / linker）

无变化，同 lesson-39（双阶段构建、`grub-file` 校验、QEMU TCG 运行）。

### 3.7 主控制流

```text
kernel_main64_binary
  ├─ task_model_init / active_sched_class / pmm_init / vma... 
  ├─ ...GDT/TSS/IDT/PIT/PIC 初始化
  ├─ (void)exec_validate()            ← 本课新增：开机校验内嵌 ELF
  ├─ 横幅（lesson-40 字符串）
  └─ 键盘循环 → exec64：
        execinfo / exectest / stacklayout / forkinfo / tasklist / ...
```

---

## 4. 数据流与运行逻辑

```text
开机：kernel_main → (void)exec_validate()
  → ELF 头/程序头/段/入口全部通过 → exec_model.validated=1, executed=0
输入 "exectest"
  → exec_validate() && exec_stack_validate()
  → "exectest: ELF header/segments/entry/stack passed; no execution"
输入 "execinfo"
  → "exec model: ELF-like bounded image" / validated: yes / segments/bytes: 2 72 / entry: 0000000000400000
    / execution: metadata only
输入 "stacklayout"
  → user stack layout: argc, argv[], NULL, envp[] / argc: 2 / sp: 0000000000800fc0
    / argv: 0000000000800fc8 / envp: 0000000000800fe0 / layout validation: passed
    / allocation/execution: none
```

`embedded_exec_image` 总长 = `sizeof(tiny_elf_header)+2*sizeof(tiny_elf_segment)+8`
= 40+112+8 = 160 字节；`image_bytes` 显示 160（`00000000000000a0`）。
注意入口 `0x400000` 正是 `USER_CODE_VA`，与 lesson-34 起的用户代码虚拟地址一致。

---

## 5. 构建、运行与验证

### 5.1 依赖

同旧课：`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 header check passed.`。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：

```text
TinyOS lesson 40: Linux-style bounded execve/ELF loader model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

验证步骤（输出串从源码逐字）：

```bash
exectest
```

预期：`exectest: ELF header/segments/entry/stack passed; no execution`

```bash
execinfo
```

预期：

```text
exec model: ELF-like bounded image
validated: yes
segments/bytes: 0000000000000002 00000000000000a0
entry: 0000000000400000
execution: metadata only
```

```bash
stacklayout
```

预期：

```text
user stack layout: argc, argv[], NULL, envp[]
argc: 0000000000000002
sp: 0000000000800fc0
argv: 0000000000800fc8
envp: 0000000000800fe0
layout validation: passed
allocation/execution: none
```

继承回归：`forktest`/`forklifecycle`/`taskvalidate`（passed）、`processtest`、
`vmtest`、`preempttest`/`pctest`/`kbdwaittest` 行为与 lesson-39 一致。

### 5.4 课程实测记录（2026-08，稳定快照）

`exectest` 输出 `passed; no execution`；`execinfo` 显示 2 段 160 字节、
entry `0x400000`；`stacklayout` 显示 sp/argv/envp 为
`0x800fc0/0x800fc8/0x800fe0` 且 layout validation: passed；继承命令全部回归。
构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `exectest` 输出 `BROKEN` | `exec_validate` 或 `exec_stack_validate` 某条不成立 | 逐条核对 ELF 头/程序头/段/入口校验；对照 `embedded_exec_image` 字节 |
| `execinfo` 显示 `validated: no` | 开机 `(void)exec_validate()` 失败 | 用 `exectest` 复查；检查镜像字节是否有改动 |
| `stacklayout` 显示 `BROKEN` | 栈指针计算与 `EXEC_STACK_WORDS`/`EXEC_STACK_ARGC` 不一致 | 手算：sp=0x801000-0x40，argv=sp+8，envp=argv+24 |
| entry 不是 `0x400000` | `embedded_exec_image` 的 entry 字段写错 | 对照第 13~20 字节（`0x00 0x00 0x40 0x00 ...`） |
| 想「运行」镜像却黑屏 | 本课刻意不执行装载字节（`executed=0`） | `execinfo` 确认 `execution: metadata only`；这是设计边界 |
| `help` 列表顺序变化 | 本课在列表中加入 exec 命令 | 对照源码 `exec64` help 字符串 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `fs/exec.c` 与 `fs/binfmt_elf.c`**：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `exec_validate` 的 ELF 头校验 | `fs/binfmt_elf.c` 的 `elf_check_arch()`、`elf_check_fdpic()` 与 `load_elf_binary()` 的 `elf_ex` 检查 | 教学模型校验 magic/type/machine/version/phoff/phnum/phentsize |
| 程序头表范围校验 | `fs/binfmt_elf.c` 的 `load_elf_phdrs()`（`elf_phdata` 越界检查） | 教学模型用「phoff+phnum*phentsize 不超过镜像」等价 |
| 段范围与 `memsz>=filesz` | `fs/binfmt_elf.c` 装载 `PT_LOAD` 段时 `size = e_memsz`，BSS 清零 | 教学模型只校验，不做装载与清零 |
| W^X 段权限检查 | `arch/x86/include/asm/elf.h` 的 `SET_PERSONALITY`、`exec_mmap` 后页表按 `p_flags` 设权限 | 教学模型用 `(flags&W)&&(flags&X)` 显式拒绝 |
| 入口范围校验 | `fs/exec.c` 的 `bprm->entry`（由 `ELF_PLAT_INIT`/`start_thread` 使用） | 教学模型限定 `[USER_CODE_VA, USER_CODE_VA+EXEC_MAX_IMAGE_BYTES)` |
| 确定性用户栈（argc/argv/envp） | `fs/exec.c` 的 `create_elf_tables()`（`ARG_START`、`elf_stack`、`put_user` 逐项写栈） | **教学模型只算指针、不写内存**；`fs/exec.c` 还有 `bprm_stack_limits`/`setup_arg_pages` 做栈上限与随机化 |
| 镜像来源 | `fs/exec.c` 的 `do_open_execat`/`kernel_read` 从文件系统读 | 教学模型用**内嵌 const 数组**，无文件系统 |

**权威来源**：ELF 规范（`e_ident`/`e_type`/`e_machine`/程序头表字段）、SysV
x86_64 ABI（用户栈 argc/argv/envp 布局）。

**教学模型简化了什么**：
1. 无文件系统：镜像内嵌在 `kernel64.c` 里，跳过 `open/read`；
2. 无真实装载：校验通过后不复制段到目标内存、不设页表权限、不跳转 `entry`；
3. 无 `mm_struct` 替换：Linux 的 `execve` 会 `flush_old_exec` + `exec_mmap` 整体
   换地址空间，教学模型只记录元数据；
4. 无栈上真正写入：`create_elf_tables` 会 `put_user` 写满栈帧，教学模型只算
   三个指针；5. `EXEC_MAX_IMAGE_BYTES=64` 的镜像上限远小于真实 ELF。

---

## 8. 思考题与练习

1. **概念理解**：为什么段校验要求 `memsz>=filesz`？如果 `memsz<filesz` 意味着什么
   （提示：内存比文件还小，数据放不下）？
2. **源码定位**：在 `exec_validate` 中指出「防无符号回绕」与「W^X」分别对应哪两行。
3. **动手实验**：把 `embedded_exec_image` 段 1 的 `ELF_SEG_R|ELF_SEG_X` 改成
   `ELF_SEG_R|ELF_SEG_W|ELF_SEG_X`（W^X 违规），重建后 `exectest` 应输出
   `BROKEN`，然后改回（勿提交）。
4. **Linux 对照**：在 `fs/exec.c` 找 `create_elf_tables`，数出它写栈时用到的
   宏/布局顺序，与 `stacklayout` 打印的三指针对照。
5. **设计思考**：要让 exec 真正可执行，还需要实现哪些机制？（提示：把段复制到
   `vaddr`、按 `p_flags` 设页表权限、把 `rsp` 设为 `stack_pointer` 后 `iretq` 到
   `entry`。）本课为什么不做？

---

## 9. 本课小结与下一课预告

**小结**：本课用 `embedded_exec_image`（手写微型 ELF64）与 `exec_validate()`
复刻了 Linux execve 装载的前置校验：ELF 头、程序头表、段范围、W^X 权限、入口
范围；用 `exec_model` 记录入口与确定性用户栈的三指针（argc/argv/envp 布局）；
`exectest`/`execinfo`/`stacklayout` 三命令可观察全部结果。**校验通过 ≠ 装载执行**，
`executed` 恒为 0——这是把「校验」与「机制」分离的教学设计。

**下一课预告**：进入 [`lesson-41-stable/README.md`](../lesson-41-stable/README.md)，
在校验过的地址范围内引入 Linux 风格的 VMA 元数据：固定 4 槽 VMA 表
（code/data/stack 三段，带 R/W/X 权限）、`vma_lookup`/`vma_range_valid`、
按 `pf_classify` 分类缺页（not-present/protection/unmapped）与有界
`fault_insert`，对照 `mm/mmap.c`、`mm/memory.c` 与 `include/linux/mm.h`。
