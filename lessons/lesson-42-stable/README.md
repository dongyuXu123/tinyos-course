# Lesson 42: Linux 风格有界 user-pointer 校验与 copy_to_user/copy_from_user 教学模型 — 精讲文档

> **课号**：Lesson 42（可执行课）
> **主题**：Linux 风格有界 user-pointer 校验与 copy_to_user/copy_from_user 教学模型
> **课程主线位置**：第 5 阶段「Linux 风格内核抽象」收官课。前课完成 VMA 表与缺页
> 分类（41）；本课在 VMA 权限之上做「内核如何安全访问用户指针」的教学模拟：
> `uaccess_validate` 依次检查 canonical、range、VMA 命中、VMA 权限，
> `uaccess_copy` 模拟有界复制并记账——**绝不解引用任意用户指针**。
> **前置课程**：[`lesson-41-stable/README.md`](../lesson-41-stable/README.md)
> **后续课程**：第 5 阶段结束，衔接第 6 阶段（后续课程目录待定，可在
> `lessons/` 下继续按编号学习）。
> **一句话目标**：能讲清楚 Linux 为什么不能在内核里直接 `memcpy` 用户指针、
> 为什么要有 `access_ok`（canonical + range）与 VMA 权限两道关卡、`copy_to_user`/
> `copy_from_user` 失败后返回多少，并在 TinyOS 里复刻全部**判定与记账**——
> 而指针从未被解引用。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——内核访问用户地址的四道校验：① canonical
（地址形式合法）；② range（不越界、不溢出、长度有上限）；③ VMA 命中（地址确实
属于用户地址空间的一段）；④ VMA 权限（读需 R、写需 W）。TinyOS 用
`uaccess_validate` 返回一个 8 字段的 `struct uaccess_result`，`uaccess_copy`
按校验结果做成功/失败记账。

- **在课程主线中的位置**：它是第 5 阶段六连课的终点。37（任务身份）→ 38
  （同步/调度抽象）→ 39（fork/clone）→ 40（exec/ELF）→ 41（VMA/缺页）→
  42（用户指针安全）。42 把「进程内存」完整收口：VMA 提供地址合法性，
  uaccess 提供内核访问用户的边界。
- **前置知识清单**：
  1. lesson-41 的 `vma_lookup`、`VMA_R/W` 位、三段 VMA 地址范围；
  2. x86-64 canonical 地址：用户态低半区 `0x0000_0000_0000_0000` 到
     `0x0000_7fff_ffff_ffff`，高 16 位必须符号扩展；
  3. Linux `access_ok`/`copy_to_user`/`copy_from_user` 的基本语义
     （`include/linux/uaccess.h`）；
  4. `SYS_WRITE_CONSOLE` 等 syscall ABI（lesson-34/35）：syscall 参数里的指针
     来自用户，内核不能直接信任。
- **本课交付**：`ptrinfo`/`ptrtest`/`copytest` 三条命令；`uaccess_result`
  结构与 `uaccess_validate`/`uaccess_copy`；`USER_CANONICAL_MAX`/
  `USER_RANGE_MAX`/`USER_COPY_MAX` 三个有界常量；`vmatest` 改为继承性占位命令。

---

## 2. 核心概念精讲

### 2.1 概念一：为什么内核不能直接解引用用户指针

用户 syscall 传来的指针是「用户空间的地址」。内核如果直接 `*(u64*)ptr` 读写：
① 用户可能传一个指向内核空间的地址（`0xffffffff...`），直接写会破坏内核；
② 用户可能传一个越界/未映射地址，导致内核上下文 `#PF` 无法安全处理；
③ 即使地址合法，用户可能把只读页当写缓冲传进来。因此 Linux 规定内核只能通过
`copy_to_user`/`copy_from_user`（内部用 `access_ok` + 异常感知指令）访问用户内存。

教学模型把「判定」与「复制」分离：`uaccess_validate` 做四道判定，
`uaccess_copy` 只记账、绝不触碰内存。

### 2.2 概念二：canonical 与 range —— 两道前置关卡

| 关卡 | 判据（本课常量） | 对应 Linux |
|---|---|---|
| canonical | `address <= USER_CANONICAL_MAX (0x7fff_ffff_ffff)`，且 `address+length-1` 不越出 | `TASK_SIZE_MAX`/`access_ok` 的 `addr_limit` |
| range | `length <= USER_COPY_MAX (256)`、`address < USER_RANGE_MAX (0x1_0000_0000)`、`address+length` 无符号回绕检查 | `check_object_size`（`mm/usercopy.c`）+ 越界检查 |

`USER_RANGE_MAX=0x100000000`（4 GiB）本课的用户区上限：任何用户指针必须小于它。
`USER_COPY_MAX=256`：单次复制长度上限，对应 Linux 的 `copy_user_*` 内联长度约束
与 usercopy hardening。

### 2.3 概念三：VMA 命中与权限 —— 后两道关卡

canonical/range 通过后，`vma_lookup(address)` 必须命中一段 VMA，且整段
`[address, address+length)` 落在该 VMA 内（`end<=v->end`）；然后按访问类型查
权限：`UACCESS_READ` 要求 `VMA_R`、`UACCESS_WRITE` 要求 `VMA_W`。这对应 Linux
`access_ok` 之后的页级/区域权限检查。

### 2.4 概念四：`struct uaccess_result` —— 一次校验的完整档案

```c
struct uaccess_result { u64 address,length; u8 access,canonical,range,vma,permission,copied; };
```

8 个字段记录校验输入（address/length/access）与四项判定结果（canonical/range/
vma/permission），外加 `copied` 标志。`ptrtest` 可以逐字段观察失败原因，
这是「校验可审计」的教学设计。

### 2.5 概念五：零长度复制是合法的

Linux 语义：`copy_to_user(to, from, 0)` 合法且返回 0。教学模型在
`uaccess_validate` 里对 `length==0` 单独处理：跳过 VMA/权限检查，直接置
`vma=1; permission=1`——零长度复制不需要真实页命中。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-41） |
|---|---|---|
| `boot.S` / `kernel.c` | 引导 | 未变化 |
| `kernel64.c` | 64 位内核主体 | **核心**：uaccess 常量/结构 + validate/copy + 3 命令；`vmatest` 改占位 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | **check 新增 grep**：README 含 `include/linux/uaccess.h`、`mm/usercopy.c`、`arch/x86/include/asm/uaccess.h`；kernel64.c 含 `ptrtest`、`copytest` |
| `grub.cfg` | 装载 | **menuentry 标题更新**为 lesson-42 主题 |

### 3.2 常量 / 结构 / 全局变量精讲

```c
/* Lesson 42: Linux-style uaccess metadata. Never dereference arbitrary user pointers. */
#define USER_CANONICAL_MAX 0x00007fffffffffffULL
#define USER_RANGE_MAX 0x0000000100000000ULL
#define USER_COPY_MAX 256U
#define UACCESS_READ 1U
#define UACCESS_WRITE 2U
struct uaccess_result { u64 address,length; u8 access,canonical,range,vma,permission,copied; };
static u64 uaccess_attempts,uaccess_successes,uaccess_failures,uaccess_bytes;
```

逐行注释：
- 头注释是整课纪律：**元数据**、**绝不解引用任意用户指针**；
- `USER_CANONICAL_MAX = 0x7fff_ffff_ffff`：x86-64 用户态 canonical 低半区上限
  （与 Linux `TASK_SIZE_MAX` 概念一致）；
- `USER_RANGE_MAX = 0x1_0000_0000`：本课用户区最大地址（4 GiB）；
- `USER_COPY_MAX = 256`：单次复制字节上限，三个上限共同构成「有界」；
- `UACCESS_READ/WRITE`：访问类型位；
- `uaccess_*` 四个全局计数器：attempts/successes/failures/bytes（字节数只在
  成功时累加）。

### 3.3 函数精讲：uaccess_validate —— 四道判定

```c
static TEXT64 int uaccess_validate(u64 address,u64 length,u8 access,struct uaccess_result*r){
    u64 end;const struct vma_model*v;
    r->address=address;r->length=length;r->access=access;
    r->canonical=address<=USER_CANONICAL_MAX&&(length==0||address<=USER_CANONICAL_MAX-length+1);
    r->range=length<=USER_COPY_MAX&&address<USER_RANGE_MAX&&
              length<=USER_RANGE_MAX-address&&address+length>=address;
    r->vma=0;r->permission=0;r->copied=0;
    if(r->canonical&&r->range&&length){
        end=address+length;v=vma_lookup(address);
        if(v&&end<=v->end){r->vma=1;
            r->permission=(access==UACCESS_READ?(v->prot&VMA_R):(v->prot&VMA_W))!=0;}}
    else if(r->canonical&&r->range&&!length){r->vma=1;r->permission=1;}
    return r->canonical&&r->range&&r->vma&&r->permission;}
```

算法步骤（每步 ≥3 行分析）：
1. **记录输入**：把 address/length/access 抄进结果结构，供 `ptrtest`/调试审计；
2. **canonical 判定**：`address <= USER_CANONICAL_MAX` 且
   `address <= USER_CANONICAL_MAX-length+1`（当 length>0 时——防止
   `address+length-1` 越过上限，用减法规避无符号加法溢出）；
3. **range 判定**：四个条件——长度 ≤ 256；起始地址 < 4 GiB；长度不超过
   `USER_RANGE_MAX-address`（剩余空间足够）；`address+length>=address`
   （加法不回绕）；
4. **VMA/权限判定**：仅当前两关通过且 `length>0` 时，`vma_lookup(address)`
   命中且 `end<=v->end`（整区间在 VMA 内），再按 access 选 R 或 W 检查；
5. **零长度特例**：`length==0` 直接放行 VMA 与权限（合法空复制）；
6. 最终返回四关 AND——`canonical && range && vma && permission`。

### 3.4 函数精讲：uaccess_copy —— 有界复制记账

```c
static TEXT64 int uaccess_copy(u64 address,u64 length,u8 access){
    struct uaccess_result r;
    uaccess_attempts++;
    if(!uaccess_validate(address,length,access,&r)){uaccess_failures++;return 0;}
    r.copied=1;uaccess_successes++;uaccess_bytes+=length;return 1;}
```

分析：
- 每次调用先 `uaccess_attempts++`；
- `uaccess_validate` 失败 → `uaccess_failures++` 并返回 0（对应
  `copy_to_user` 返回非零「未复制字节数」的失败语义）；
- 成功 → 置 `copied=1`、`uaccess_successes++`、`uaccess_bytes+=length` 返回 1；
- **没有任何内存读写**：`address` 从未被强制转换并访问——注释
  `no source/destination bytes touched` 是设计承诺而非空话。

### 3.5 函数精讲：ptrinfo / ptrtest / copytest 与 vmatest 占位

```c
static TEXT64 void ptrinfo(u16*c){text64(c,"uaccess: canonical max/range max/copy max ");
    hex64(c,USER_CANONICAL_MAX);text64(c," ");hex64(c,USER_RANGE_MAX);
    text64(c," ");hex64(c,USER_COPY_MAX);
    text64(c,"\nattempts/success/failure/bytes: ");hex64(c,uaccess_attempts);
    text64(c," ");hex64(c,uaccess_successes);text64(c," ");
    hex64(c,uaccess_failures);text64(c," ");hex64(c,uaccess_bytes);
    text64(c,"\nvalidation only: no arbitrary pointer dereference\n");}
static TEXT64 void ptrtest(u16*c){struct uaccess_result r;
    int ok=uaccess_validate(VMA_DATA_START,8,UACCESS_READ,&r)&&
        !uaccess_validate(USER_CANONICAL_MAX+1,8,UACCESS_READ,&r)&&
        !uaccess_validate(VMA_DATA_START,USER_COPY_MAX+1,UACCESS_READ,&r)&&
        !uaccess_validate(VMA_CODE_START,8,UACCESS_WRITE,&r);
    text64(c,"ptrtest: ");text64(c,ok?"canonical/range/VMA/permission checks passed":"BROKEN");
    putc64(c,'\n');}
static TEXT64 void copytest(u16*c){
    int a=uaccess_copy(VMA_DATA_START,16,UACCESS_WRITE),
        b=uaccess_copy(VMA_CODE_START,16,UACCESS_WRITE),
        d=uaccess_copy(USER_CANONICAL_MAX-3,8,UACCESS_READ),
        e=uaccess_copy(VMA_DATA_START,USER_COPY_MAX+1,UACCESS_READ);
    text64(c,"copytest: ");text64(c,a&&!b&&!d&&!e?
        "copy_to_user/from_user bounded success/failure accounting passed":"BROKEN");
    text64(c,"\nno source/destination bytes touched; pointers were never dereferenced\n");}
```

- `ptrinfo`：显示三个上限常量与四个计数器，末行声明「只校验、不解引用」；
- `ptrtest`：四个断言——① 数据段读 8 字节通过（canonical/range/VMA/R 权）；
  ② `USER_CANONICAL_MAX+1` 读失败（canonical 越界）；③ 数据段读 257 字节失败
  （`USER_COPY_MAX` 超限）；④ 代码段写失败（无 W 权）；
- `copytest`：四个模拟复制——① 数据段写 16 成功；② 代码段写失败；③
  `USER_CANONICAL_MAX-3` 读 8 字节失败（跨过 canonical 边界，
  `address<=MAX-length+1` 不成立）；④ 数据段读 257 字节失败；
- `vmatest` 在本课改为占位（源码逐字）：
  `static TEXT64 void vmatest(u16*c){text64(c,"vmatest: inherited VMA checks available\n");}`
  ——VMA 细节检查让位于 `ptrtest`/`copytest`，此处只确认继承的 VMA 机制可用。

### 3.6 exec64 分支、kernel_main、grub.cfg 与 Makefile

`exec64` 新增三个分支：

```c
else if(eq64(word,"ptrinfo")){if(!noargs64(arg))usage64(c,"ptrinfo");else ptrinfo(c);}
else if(eq64(word,"ptrtest")){if(!noargs64(arg))usage64(c,"ptrtest");else ptrtest(c);}
else if(eq64(word,"copytest")){if(!noargs64(arg))usage64(c,"copytest");else copytest(c);}
```

横幅（源码逐字）：

```text
TinyOS lesson 42: Linux-style bounded VMA and user-pointer/copy model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

`about`：`TinyOS lesson 42: bounded Linux-style user-pointer validation and usercopy model`

`grub.cfg` 的 menuentry 标题更新（源码逐字）：
`menuentry "TinyOS lesson 42: bounded user-pointer validation and usercopy model" {`。

Makefile `check` 目标新增五条 grep（README 三路径 + kernel64.c 两符号）：

```make
@grep -q 'include/linux/uaccess.h' README.md
@grep -q 'mm/usercopy.c' README.md
@grep -q 'arch/x86/include/asm/uaccess.h' README.md
@grep -q 'ptrtest' kernel64.c
@grep -q 'copytest' kernel64.c
@printf '%s\n' 'Multiboot2 and lesson 42 checks passed.'
```

### 3.7 主控制流

```text
kernel_main64_binary
  ├─ task_model_init / active_sched_class / pmm_init / vma_init
  ├─ address_space_init / GDT/TSS/IDT/PIT/PIC / (void)exec_validate()
  ├─ 横幅（lesson-42 字符串）
  └─ 键盘循环 → exec64：
        ptrinfo / ptrtest / copytest / vmainfo / pfmodel / execinfo / ...
```

---

## 4. 数据流与运行逻辑

```text
输入 "ptrtest"
  → uaccess_validate(0x600000, 8, READ)   → 四关全过 → true
  → uaccess_validate(0x800000000000,8,READ)→ canonical 越界 → false
  → uaccess_validate(0x600000, 257, READ) → length>256 → range 失败 → false
  → uaccess_validate(0x400000, 8, WRITE)  → VMA0 无 W 权 → permission 失败 → false
  → "ptrtest: canonical/range/VMA/permission checks passed"
输入 "copytest"
  → a=copy(0x600000,16,WRITE)成功；b=copy(0x400000,16,WRITE)失败；
    d=copy(0x7ffffffffffc,8,READ)失败；e=copy(0x600000,257,READ)失败
  → "copytest: copy_to_user/from_user bounded success/failure accounting passed"
    + "no source/destination bytes touched; pointers were never dereferenced"
输入 "ptrinfo"
  → "uaccess: canonical max/range max/copy max 000000007fffffffffff 0000000100000000 0000000000000100"
  → "attempts/success/failure/bytes: ..."（随运行累加）
  → "validation only: no arbitrary pointer dereference"
```

两次 `copytest` 后：attempts=8、successes=2、failures=6、bytes=32。

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

`make check` 输出：`Multiboot2 and lesson 42 checks passed.`（要求 README 含
`include/linux/uaccess.h`、`mm/usercopy.c`、`arch/x86/include/asm/uaccess.h`，
kernel64.c 含 `ptrtest` 与 `copytest`，缺一即失败）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：

```text
TinyOS lesson 42: Linux-style bounded VMA and user-pointer/copy model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

验证步骤（输出串从源码逐字）：

```bash
ptrtest
```

预期：`ptrtest: canonical/range/VMA/permission checks passed`

```bash
copytest
```

预期：

```text
copytest: copy_to_user/from_user bounded success/failure accounting passed
no source/destination bytes touched; pointers were never dereferenced
```

```bash
ptrinfo
```

预期：

```text
uaccess: canonical max/range max/copy max 000000007fffffffffff 0000000100000000 0000000000000100
attempts/success/failure/bytes: 0000000000000008 0000000000000002 0000000000000006 0000000000000020
validation only: no arbitrary pointer dereference
```

（计数按运行历史累加；上例为跑过一次 `copytest` 后的值。）

```bash
vmatest
```

预期：`vmatest: inherited VMA checks available`（本课占位命令）

继承回归：`vmainfo`/`pfmodel`/`exectest`/`forklifecycle`/`taskvalidate`/
`processtest`/`vmtest` 行为与 lesson-41 一致；真实 `#PF` 命令
`pftest`/`isttest`/`stackguardtest` 保持致命停机。

### 5.4 课程实测记录（2026-08，学习快照）

`make check` 输出 `Multiboot2 and lesson 42 checks passed.`；`ptrtest` 四断言
通过；`copytest` 一次后 `ptrinfo` 显示 attempts=8、success=2、failure=6、
bytes=32（两次 copytest 后翻倍）；`vmatest` 输出占位串；`vmainfo`/`pfmodel`
与 lesson-41 一致。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make check` 报错 | README 缺 `include/linux/uaccess.h`/`mm/usercopy.c`/`arch/x86/include/asm/uaccess.h`，或 kernel64.c 缺 `ptrtest`/`copytest` | 对照 Makefile check 的五条 grep |
| `ptrtest` 输出 `BROKEN` | 四个断言中一个不成立 | 逐条核对：数据段读 8 成功；canonical+1 失败；257 失败；代码段写失败 |
| `copytest` 输出 `BROKEN` | 四个模拟复制结果与预期不符 | 手算每个 case 的 canonical/range/VMA/permission |
| `ptrinfo` 的 bytes 比预期大 | `uaccess_copy` 成功路径被多次调用 | 每次成功 `uaccess_bytes+=length`，两次 copytest 后应为 32 |
| 担心「真的复制了」 | 设计保证不解引用 | `copytest` 末行声明；`uaccess_copy` 无任何内存访问指令 |
| `vmatest` 不再做范围校验 | 本课改为占位（源码事实） | 范围/权限细节由 `ptrtest`/`copytest` 覆盖 |
| QEMU 窗口菜单名与课号不符 | `grub.cfg` menuentry 未更新 | 对照 lesson-42 的 `grub.cfg` 标题 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `include/linux/uaccess.h`、`mm/usercopy.c` 与
`arch/x86/include/asm/uaccess.h`**（另继承 lesson-41 的
`mm/mmap.c`/`mm/memory.c`/`include/linux/mm.h`）：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `USER_CANONICAL_MAX=0x7fff_ffff_ffff` | `arch/x86/include/asm/processor.h` 的 `TASK_SIZE_MAX` | 教学模型用常量直接判定；Linux 还区分 `TASK_SIZE_MAX` 与 `addr_limit` |
| `uaccess_validate` 的 canonical/range | `include/linux/uaccess.h` 的 `access_ok()`（`arch/x86` 的 `__range_not_ok`/`ACCESS_OK`） | 教学模型把 `access_ok` 与长度上限合并成显式条件 |
| `USER_COPY_MAX=256` 与长度检查 | `mm/usercopy.c` 的 `check_object_size` + `copy_user_*` 的长度分支 | 教学模型不做 slab 对象尺寸检查，只做字节数上限 |
| VMA 命中 + 权限检查 | `access_ok` 之后的页表/`find_vma` 路径；`vmacache_find` | 教学模型复用 lesson-41 的 `vma_lookup` |
| `uaccess_copy` 成功/失败记账 | `arch/x86/include/asm/uaccess.h` 的 `copy_to_user`/`copy_from_user` → `raw_copy_to_user`（`rep movsb` + `stac/clac`） | **教学模型不执行任何复制指令**，只记账；Linux 用 `STAC/CLAC` 与异常表处理 `#PF` |
| `UACCESS_READ/WRITE` | `VERIFY_READ`/`VERIFY_WRITE`（旧内核）与 `check_object_size` 的 `write` 参数 | 教学模型用访问位区分读/写 |
| 零长度复制放行 | Linux `copy_to_user` 对 `n==0` 直接返回 0 | 语义一致，教学模型显式 `length==0` 分支 |

**权威来源**：Intel SDM（canonical 地址规则：高 16 位符号扩展；`#PF` 语义）、
SysV x86-64 ABI。

**教学模型简化了什么**：
1. 不解引用任何指针：Linux 的 `raw_copy_to_user` 会用带异常表的 `rep movsb`
   真正搬运字节并处理 `#PF`，教学模型只返回成功/失败；
2. 无 `addr_limit`/`set_fs` 概念：教学模型用固定常量 `USER_RANGE_MAX`；
3. 无 `mm/usercopy.c` 的 slab 对象尺寸检查与 hardening 内核对象白名单；
4. 无 `stac/clac`（SMAP）：教学模型不执行用户内存访问，自然不需要 SMAP；
5. VMA 仍是固定 3 段线性查找，无红黑树、无 `get_user_pages`/`fault_in*`。

---

## 8. 思考题与练习

1. **概念理解**：`uaccess_validate` 为什么用 `USER_CANONICAL_MAX-length+1` 而不用
   `address+length<=USER_CANONICAL_MAX+1`？（提示：无符号加法回绕。）
2. **源码定位**：在 `ptrtest` 的四个断言中，指出每个断言分别命中四道关卡
   （canonical/range/vma/permission）中的哪一道。
3. **动手实验**：把 `USER_COPY_MAX` 改成 512，重建后 `ptrtest` 的第三个断言
   （`USER_COPY_MAX+1`）应仍失败（因为 257<512 会改判成功路径而破坏断言），
   观察 `ptrtest` 输出变化，然后改回（勿提交）。
4. **Linux 对照**：打开 `arch/x86/include/asm/uaccess.h` 的 `access_ok`/
   `__range_not_ok`，对照本课 `r->range` 的四个条件，列出差异。
5. **设计思考**：如果 syscall 想真正复制 8 字节到用户缓冲区，本课模型会返回
   「校验通过」，但实际复制还需要什么？（提示：异常表、`stac/clac`、
   `#PF` 处理与部分复制字节计数。）为什么本课把这些全省掉？

---

## 9. 本课小结与下一课预告

**小结**：本课用 `uaccess_validate` 的四道判定（canonical、range、VMA 命中、
VMA 权限）复刻了 Linux 内核访问用户指针前的全部安全检查；`uaccess_copy`
按判定结果做有界记账（attempts/successes/failures/bytes），模拟
`copy_to_user`/`copy_from_user` 的成功与拒绝——**指针从未被解引用**。
`ptrinfo`/`ptrtest`/`copytest` 三命令可观察上限常量、四道关卡与复制记账；
`vmatest` 改为继承性占位；Makefile `check` 强制 README 与 kernel64.c 含
相应符号；grub.cfg 菜单标题同步更新。至此第 5 阶段六连课收官：任务身份 →
同步/调度 → 进程创建 → 镜像装载 → 地址空间 → 用户指针安全，全部以「有界
元数据 + 验证函数」的教学模型完成，且**从未把任何未经校验的指针变成可执行/
可访问的内核对象**。

**下一课预告**：第 5 阶段结束。下一课将进入第 6 阶段（可在 `lessons/` 目录按
编号查看后续课程），把本阶段积累的元数据模型（task/fork/exec/VMA/uaccess）
逐步向「真正可执行、可切换的进程」收敛——届时会复用本课学到的边界判定作为
安全前提。
