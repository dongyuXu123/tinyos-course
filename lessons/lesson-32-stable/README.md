# Lesson 32: 校验后的内置用户程序镜像与最小加载器 — 精讲文档

> **课号**：Lesson 32
> **主题**：校验后的内置用户程序镜像（kernel-embedded user image）与最小加载器
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）的第一课，
> 承接第 2 阶段 CPL3 syscall 闭环（28–31）。
> **前置课程**：[Lesson 31（受控用户返回与 SYS_EXIT 终止路径）](../lesson-31-stable/README.md)
> **后续课程**：[Lesson 33（有界 address-space 对象）](../lesson-33-stable/README.md)
> **一句话目标**：学完本课，你能说清「用户程序不再由引导代码逐字节手工铺设，
> 而是作为 `const u8 user_image_code[]` 内嵌在 `kernel.c` 里，并在分配任何物理页、
> 建立任何映射、进入任何入口**之前**，先由一个可审计的加载校验器把关」。

---

## 1. 课程定位（Mission）

- **一句话目标**：把 Lesson 31 那段「在 `setup_long_mode_tables` 里用 `calls[]`
  循环现场拼装 37 字节 stub」的代码，替换为「**预置的内嵌镜像 + 描述符 + 校验器 +
  最小加载器**」：镜像字节由 `user_image_code[]` 常量数组携带；`user_image_descriptor`
  描述 magic/version/字节数/入口偏移/入口长度；`validate_user_image()` 在任何
  物理页分配与映射之前把关；校验失败直接写 VGA 报告 `user image validation/load failure`。
- **课程主线位置**：这是第 3 阶段「用户程序」的开端。阶段 2 结束时，用户代码是
  内核引导期**即兴生成**的字节；从本课起，用户程序变成**数据**（镜像），并第一次
  有了「可校验的格式」——这正是后续 ELF 段校验（Lesson 40）、VMA 与缺页
  （Lesson 41）的原型。
- **前置知识清单**：
  1. `setup_long_mode_tables()` 在 32 位引导期分配页表并铺设用户代码/栈映射
     （Lesson 8/28/31）；
  2. syscall ABI：0/1/2/99/3 五调用点与 all-GPR 帧（Lesson 29–31）；
  3. PTE 权限位（`PTE_PRESENT`/`PTE_WRITABLE`/`PTE_USER`）与低/高双别名映射
     （Lesson 27/28）；
  4. Multiboot2 内存图解析与 `long_mode_handoff` 交接块（Lesson 8/14/32 前各课）。
- **本课交付**：banner 与 `about` 文案标注 Lesson 32；`cpl3test` 仍跑完
  0、1、2、99、3 五调用并最终 EXIT 停机；`meminfo`/`lminfo` 等继承命令不受影响；
  唯一新增的「不可见但关键」交付是**镜像校验边界**——如果镜像被破坏，VGA 上会
  直接出现 `user image validation/load failure: <code>`，且永不进入 shell。

---

## 2. 核心概念精讲

### 2.1 内核内嵌用户镜像（kernel-embedded user image）

**定义**：用户程序的可执行字节不再在运行时拼装，而是作为**常量字节数组**放在
内核镜像里（32 位侧的 `kernel.c`）：

```c
static const u8 user_image_code[] = {
    0xb8,0x00,0x00,0x00,0x00,0xcd,0x80,   /* mov eax,0 ; int 0x80  (GETTICKS) */
    0xb8,0x01,0x00,0x00,0x00,0xcd,0x80,   /* mov eax,1 ; int 0x80  (GETPID) */
    0xb8,0x02,0x00,0x00,0x00,0xcd,0x80,   /* mov eax,2 ; int 0x80  (WRITE_CONSOLE) */
    0xb8,0x63,0x00,0x00,0x00,0xcd,0x80,   /* mov eax,99; int 0x80  (未知号 → -ENOSYS) */
    0xb8,0x03,0x00,0x00,0x00,0xcd,0x80,   /* mov eax,3 ; int 0x80  (SYS_EXIT) */
    0xeb,0xfe                             /* jmp $（保险丝） */
};
```

**为什么需要**：上一课把「程序是什么」编死在 `calls[]` 与 `for` 循环里——程序内容
与生成逻辑耦合，无法独立演进、无法审计。把程序变成**镜像数据**后：
1. 字节内容可静态检查（`xxd`/`objdump` 都能读）；
2. 加载器与镜像格式解耦：加载器只认「描述符 + 字节」，不关心指令语义；
3. 为「多程序共存」（Lesson 36 的第二个镜像）铺路。
**工作机制**：`const` + `static` 使数组落在只读段；37 字节（5×7+2）远小于一页。

### 2.2 镜像描述符（user_image_descriptor）

**定义**：描述镜像布局的元数据：

```c
struct user_image_descriptor {
    u32 magic, version, image_bytes, entry_offset, entry_length;
};
static const struct user_image_descriptor user_image = {
    USER_IMAGE_MAGIC, USER_IMAGE_VERSION, sizeof(user_image_code), 0, sizeof(user_image_code)
};
```

字段含义：`magic`（魔数，识别「这是 TinyOS 用户镜像」）、`version`（格式版本）、
`image_bytes`（镜像总字节数）、`entry_offset`（入口在镜像内的偏移）、
`entry_length`（入口段的字节长度）。

**为什么需要**：这是 Linux 可执行文件头的缩微版——ELF 有 `e_magic/e_version/
e_entry` 与 program header 的 `p_offset/p_filesz`；TinyOS 用 5 个 u32 表达同一
职责。入口被建模为「offset + length」，意味着将来入口可以不是 0（如 ELF 的
`e_entry` 指向入口符号）。

### 2.3 加载前校验（validate_user_image）

**定义**：加载器第一步（先于一切副作用）对描述符 + 字节做算术与范围检查：

```c
static int validate_user_image(void)
{
    u64 end;
    if(user_image.magic!=USER_IMAGE_MAGIC) return USER_IMAGE_BAD_MAGIC;
    if(user_image.version!=USER_IMAGE_VERSION) return USER_IMAGE_BAD_VERSION;
    if(!user_image.image_bytes || user_image.image_bytes>USER_IMAGE_MAX_BYTES ||
       user_image.image_bytes>sizeof(user_image_code)) return USER_IMAGE_BAD_SIZE;
    if(user_image.entry_length==0 || user_image.entry_length>USER_IMAGE_MAX_ENTRY_LENGTH) return USER_IMAGE_BAD_ENTRY;
    end=(u64)user_image.entry_offset+user_image.entry_length;
    if(end>user_image.image_bytes || end>USER_IMAGE_MAX_BYTES) return USER_IMAGE_BAD_ENTRY;
    return USER_IMAGE_OK;
}
```

**算法步骤**：① magic 必须等于 `USER_IMAGE_MAGIC`；② version 必须等于
`USER_IMAGE_VERSION`；③ `image_bytes` 非零、≤ `USER_IMAGE_MAX_BYTES`(256)、且
≤ 数组实际大小（防描述符声称读取越界）；④ `entry_length` 非零且 ≤
`USER_IMAGE_MAX_ENTRY_LENGTH`(128)；⑤ **加法溢出/越界防护**：`end =
entry_offset + entry_length`，若 `end` 超出 `image_bytes` 或超出 256 上限则拒绝。
**边界检查**：用 `u64` 做加法避免 u32 溢出回绕（若回绕，`end` 会小于 `entry_offset`，
随后的 `end>image_bytes` 就能抓住）；检查顺序「先魔数后几何」让错误码可定位。
**为什么这样设计**：对照 Linux `fs/binfmt_elf.c` 中 `elf_check_arch` /
`elf_check_fdpic` 的「先验格式、后读段」顺序——任何格式错误都必须在分配/映射
之前返回，避免半初始化状态。

### 2.4 最小加载器：先校验、后分配、后映射、后入口

**定义**：`setup_long_mode_tables()` 的入口处做校验，失败即回绝整个启动：

```c
u32 i,j; int image_status=validate_user_image();
long_mode_handoff.user_image_status=(u32)image_status;
long_mode_handoff.user_image_bytes=user_image.image_bytes;
long_mode_handoff.user_entry_offset=user_image.entry_offset;
long_mode_handoff.user_entry_length=user_image.entry_length;
if(image_status!=USER_IMAGE_OK){image_failure_report(image_status);return 0;}
/* The validated descriptor, not a synthesized syscall stub, defines the load. */
```

**为什么需要**：四件事有严格次序——**校验 → 分配物理页 → 映射 PTE → 交付入口**。
`image_status` 与镜像元数据同时写入 `long_mode_handoff`，让 64 位侧也能看到
「镜像边界在哪里、校验结论是什么」。校验失败返回 0 会让 `_start` 停在 `halt32`，
且 VGA 已被失败报告占据（见 2.5）。

加载本身（在分配两页之后）：

```c
{ volatile u8 *code=(volatile u8 *)(unsigned long)(u32)long_mode_handoff.user_code_phys;
  ...
  u32 k; for(k=0;k<user_image.image_bytes;k++)code[k]=user_image_code[k];
  pt[(USER_CODE_VA/PAGE_SIZE)%PAGE_ENTRIES]=...user_code_phys|PTE_PRESENT|PTE_USER;
  st[(USER_STACK_VA/PAGE_SIZE)%PAGE_ENTRIES]=...user_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER; }
```

`for(k=0;k<user_image.image_bytes;k++)` 严格按**描述符声明的字节数**复制，而不是
按数组的 `sizeof`——「描述符即真相」，与校验器保证的 `image_bytes ≤
sizeof(user_image_code)` 形成闭环。

### 2.5 校验失败报告（image_failure_report）

**定义**：校验失败时直接向 VGA 文本缓冲区（`0xb8000`）写入错误串与状态码：

```c
static void image_failure_report(int status)
{
    volatile u16 *v=(volatile u16 *)(unsigned long)0xb8000;
    const char *s="user image validation/load failure"; u32 i;
    for(i=0;s[i];i++) { v[i]=0x0f00U|(u8)s[i]; }
    v[i++]=0x0f00U|':'; v[i++]=0x0f00U|' '; v[i]=0x0f00U|('0'+(u8)status);
}
```

**为什么需要**：此时 64 位内核（`kernel_main64_binary`）还没运行，console/shell
基础不可用；而 VGA 缓冲区始终可写。状态码用 `'0'+status` 映射
`1=BAD_MAGIC, 2=BAD_VERSION, 3=BAD_SIZE, 4=BAD_ENTRY`，可直接读数定位。

---

## 3. 源码精讲（本课最长的章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 31） |
|---|---|---|
| `kernel.c` | 32 位引导 + 镜像/描述符/校验器/加载器 | **核心增量**：`user_image_code[]`、`user_image_descriptor`、`validate_user_image()`、`image_failure_report()`；handoff 新增 4 个 u32 字段；加载改为按描述符复制 |
| `kernel64.c` | 64 位主内核 | **文案增量**：banner / `syscall_report`（SYS_EXIT 与 dispatcher）/ `about` / `syscallinfo` 的课号与边界描述更新；逻辑不变 |
| `boot.S` | Multiboot2 头 + 进 long mode | 未变化 |
| `Makefile` | 构建 `kernel.iso` | 未变化 |
| `kernel64.ld` | 64 位裸机链接脚本 | 未变化 |
| `linker.ld` | 外层 ELF 布局 | 未变化 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel.c 精讲（本课核心）

#### 3.2.1 镜像相关宏

```c
#define USER_IMAGE_MAGIC 0x32555352U   /* "RSU2" 小端：课号 32 的用户镜像魔数 */
#define USER_IMAGE_VERSION 1U
#define USER_IMAGE_MAX_BYTES 256U      /* 镜像字节数硬上限 */
#define USER_IMAGE_MAX_ENTRY_LENGTH 128U /* 入口段长度硬上限 */
```

魔数 `0x32555352` 按小端字节序读为 `52 53 55 32`（ASCII `"RSU2"`）——一个可读的
「课号 32 用户镜像」签名，用于在启动早期人工识别。上限 256/128 让校验与加载都
发生在「一个页面之内」的已知边界。

#### 3.2.2 handoff 结构体增量

```c
struct long_mode_handoff {
    ...
    u32 mbi_address, mbi_size;
    u32 user_image_status, user_image_bytes, user_entry_offset, user_entry_length; /* 本课新增 */
};
```

四个新字段把「镜像校验结论与布局」从 32 位引导侧传递给 64 位内核，
`kernel_main64_binary` 之后可在诊断命令中复核（本课仅透传，未做展示命令）。

#### 3.2.3 setup_long_mode_tables —— 校验先行 + 按描述符加载

```c
static u32 setup_long_mode_tables(void)
{
    volatile u64 *pml4,*pdpt,*pd,*hpdpt,*hpd; u32 i,j; int image_status=validate_user_image();
    long_mode_handoff.user_image_status=(u32)image_status;
    long_mode_handoff.user_image_bytes=user_image.image_bytes;
    long_mode_handoff.user_entry_offset=user_image.entry_offset;
    long_mode_handoff.user_entry_length=user_image.entry_length;
    if(image_status!=USER_IMAGE_OK){image_failure_report(image_status);return 0;}
    /* The validated descriptor, not a synthesized syscall stub, defines the load. */
    long_mode_handoff.pml4=bootstrap_alloc_page(); ...
    ...
    { volatile u8 *code=...; volatile u64 *pt=...; volatile u64 *st=...; u32 k;
      for(k=0;k<user_image.image_bytes;k++)code[k]=user_image_code[k];
      pt[(USER_CODE_VA/PAGE_SIZE)%PAGE_ENTRIES]=long_mode_handoff.user_code_phys|PTE_PRESENT|PTE_USER;
      st[(USER_STACK_VA/PAGE_SIZE)%PAGE_ENTRIES]=long_mode_handoff.user_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER; }
    ...
}
```

- **签名与职责**：返回 PML4 物理地址；校验失败时返回 0 并写 VGA 报告。
- **算法步骤**：① `validate_user_image()` 先跑；② 四份镜像元数据写入 handoff；
  ③ 非 OK 则 `image_failure_report` + 返回 0（**不分配任何页**）；④ 分配 PML4/
  PDPT/PD/PT/高别名各页并清零；⑤ 分配用户代码页、栈页；⑥ 按 `image_bytes` 从
  `user_image_code[]` 复制到代码页；⑦ 建 `USER_CODE_VA`（只读/用户）与
  `USER_STACK_VA`（可写/用户）PTE。
- **边界与错误处理**：分配失败走既有 `table_page_ok` 检查；镜像复制循环上限是
  `image_bytes`（已被校验 ≤ 256），不会越界写用户代码页。
- **为什么这样设计**：校验被放在「第一个 `bootstrap_alloc_page()` 之前」——
  注释 `The validated descriptor, not a synthesized syscall stub, defines the load`
  点明了本课与 Lesson 31 的本质差别。

#### 3.2.4 kernel_main32 —— 默认状态初始化

```c
u32 kernel_main32(u32 magic,u32 mbi_address)
{
    ...
    long_mode_handoff.user_image_status=0xffffffffU;   /* 默认：未校验 */
    if(!prepare_memory_map()) return 0;
    long_mode_handoff.mbi_address=multiboot_address; long_mode_handoff.mbi_size=multiboot_total_size;
    return setup_long_mode_tables();
}
```

`0xffffffff` 作为「尚未调用校验器」的哨兵，区分「没校验」与「校验出 1–4 号错误」。
若 `prepare_memory_map()` 失败，镜像也不会被加载。

### 3.3 kernel64.c 精讲（文案增量）

- `kernel_main64_binary` banner：

  ```
  TinyOS lesson 32: validated embedded user image and SYS_EXIT
  GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR frame and IF=0
  ```

- `syscall_report` 两处课号串改为 `TinyOS lesson 32 SYS_EXIT` /
  `TinyOS lesson 32 syscall dispatcher`；EXIT 分支仍是三段报告 + `cli; hlt`。
- `about` 改为：`TinyOS lesson 32: validated embedded user image; bounded dual-alias mapping registry`。
- `syscallinfo` / `cpl3test` 文案与 Lesson 31 相同（`calls 0,1,2,99,3 (EXIT)`）。

逻辑零变化：IDT、PIC/PIT、调度器、VM 注册表、EXIT 停机路径全部继承。
说明：镜像内容（0,1,2,99,3 + `jmp .`）与 Lesson 31 的手工 stub 逐字节等价，
因此用户态行为可验证地保持不变——这正是「格式改变、语义不变」的回归测试。

### 3.4 构建管线（Makefile / linker）

与 Lesson 31 完全一致：`-m32` 编 `kernel.c`/`boot.S`，`-m64 -mno-red-zone` 编
`kernel64.c`，`objcopy -O binary` 产出裸 64 位内核，`grub-mkrescue` 打包 ISO，
`check` 用 `grub-file --is-x86-multiboot2` 校验。`user_image_code[]` 是 `kernel.c`
里的 `static const`，随 ELF 落在只读段并被 GRUB 加载到内存——**没有新增构建
步骤**，镜像内嵌是通过「普通数组 + 链接」自然实现的。

### 3.5 主控制流

```
_start (boot.S)
  └─ kernel_main32 (kernel.c)
        ├─ user_image_status = 0xffffffff（哨兵）
        ├─ prepare_memory_map()
        └─ setup_long_mode_tables()
              ├─ validate_user_image() ──失败──▶ image_failure_report + 返回 0 ──▶ halt32
              ├─ 分配页表页 / 用户代码页 / 用户栈页
              ├─ for(k<image_bytes) code[k]=user_image_code[k]   ← 最小加载器
              ├─ 映射 USER_CODE_VA（只读/USER）、USER_STACK_VA（可写/USER）
              └─ 返回 PML4 ──▶ enter_long_mode ──▶ kernel_main64_binary（Lesson 31 同款 shell）
```

---

## 4. 数据流与运行逻辑

1. **编译期**：`user_image_code[]`（37 字节）与 `user_image`（5×u32 描述符）随
   `kernel.c` 进入 ELF 只读段。
2. **启动**：GRUB 把 ELF 载入内存 → `kernel_main32` 记 handoff 基础字段 →
   `setup_long_mode_tables` 先调 `validate_user_image()`：
   - magic/version 不符 → 报告 `user image validation/load failure: 1|2`，停机；
   - 尺寸/入口越界 → 报告 `: 3|4`，停机；
   - 通过 → 分配页表与用户两页，`for(k<image_bytes)` 把镜像拷入代码页。
3. **映射**：代码页 PTE = `phys|PTE_PRESENT|PTE_USER`（只读），栈页 PTE =
   `phys|PTE_PRESENT_WRITABLE|PTE_USER`。
4. **进入用户态**：shell 输入 `cpl3test` → `enter_user_c` 的 `iretq` 跳到
   `USER_CODE_VA` → 依次执行镜像中 5 个 `int 0x80`（0、1、2、99、3）。
5. **返回/终止**：前 4 次经 `syscall_report` 打印 dispatcher 报告后 `iretq`；
   第 5 次（3=EXIT）打印 `TinyOS lesson 32 SYS_EXIT` 报告并 `cli; hlt`。

---

## 5. 构建、运行与验证

**依赖**：与 Lesson 31 相同（gcc、binutils、grub-mkrescue、grub-file、qemu）。

**构建与格式校验**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 通过时输出：

```
Multiboot2 header check passed.
```

**运行**（成功画面在 QEMU 图形窗口，勿加 `-display none`）：

```bash
make run
```

**验证步骤**（全新启动后）：

1. banner（逐字摘自 `kernel_main64_binary`）：

   ```
   TinyOS lesson 32: validated embedded user image and SYS_EXIT
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR frame and IF=0
   ```

2. 运行 `about`（逐字摘自 `exec64`）：

   ```
   TinyOS lesson 32: validated embedded user image; bounded dual-alias mapping registry
   ```

3. 运行 `idtinfo` 确认 `int 0x80` DPL3 门不变；运行 `cpl3test`，镜像执行
   0、1、2、99、3：前 4 份 dispatcher 报告后，末屏出现（逐字摘自 `syscall_report`）：

   ```
   TinyOS lesson 32 SYS_EXIT
   user requested controlled exit
   user return frame is valid; halting intentionally
   ```

   画面冻结 = 成功（EXIT 已受理）。

4. **镜像校验边界的观察**（改动后验证）：临时把 `USER_IMAGE_MAGIC` 改成其他值
   重新构建运行，屏幕应显示（逐字摘自 `image_failure_report`）：
   `user image validation/load failure: 1`，且不再进入 shell；改回后恢复。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 屏幕出现 `user image validation/load failure: 1` | 描述符 `magic` 与 `USER_IMAGE_MAGIC` 不一致 | 检查 `user_image` 初始化是否用 `USER_IMAGE_MAGIC` 宏 |
| `... failure: 2` | `version` 不等于 `USER_IMAGE_VERSION` | 核对版本常量与描述符 |
| `... failure: 3` | `image_bytes` 为 0、>256、或大于 `sizeof(user_image_code)` | 数数组长度是否与描述符一致 |
| `... failure: 4` | `entry_length==0` 或入口区间越界 | 复算 `entry_offset+entry_length ≤ image_bytes` |
| 无失败报告但用户代码乱跑（异常指令） | 复制循环用了 `sizeof(user_image_code)` 而非 `image_bytes`，或数组字节被改动 | 检查 `for(k=0;k<user_image.image_bytes;k++)` |
| 用户代码页只读但执行 #PF | 代码 PTE 少了 `PTE_PRESENT` | 检查 `pt[...]=...user_code_phys|PTE_PRESENT|PTE_USER` |
| 用户栈写失败（#PF） | 栈 PTE 少了 `PTE_WRITABLE` | 检查 `st[...]=...user_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER` |
| 一切正常但 shell 仍在，无 EXIT 冻结 | 镜像末尾缺少 `0xeb 0xfe` 或调用序列末位不是 3 | `xxd` 内核中的 `user_image_code[]`，核对 5×7+2 字节 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学模型简化了什么 |
|---|---|---|
| `user_image_code[]` 内嵌常量镜像 | `fs/binfmt_elf.c` 从文件加载 ELF 段 | Linux 镜像来自文件系统且有 phdr/segments；TinyOS 是编译期内嵌的裸字节 |
| `user_image_descriptor`（magic/version/size/entry） | ELF header `e_ident[EI_MAG0..3]`、`e_version`、`e_entry`、program header | ELF 用 64 字节头 + 多 program header；TinyOS 用 5 个 u32 表达最小元数据 |
| `validate_user_image()` 先于分配/映射 | `elf_check_arch()`、`load_elf_binary()` 里先校验再 `elf_map()` | Linux 还有 binfmt 探测、`mmap` 权限与 ASLR；TinyOS 只查范围与魔数 |
| `image_failure_report()` 直写 VGA | `printk`/`panic()` | 引导早期 Linux 用 `earlycon`/BSS 数据记录；TinyOS 直接写显存 |

权威来源：ELF 规范（`e_ident`/`e_entry`）、Linux `fs/binfmt_elf.c`、Intel SDM
Vol.3（页权限与 DPL3 门）。

---

## 8. 思考题与练习

1. **概念理解**：`validate_user_image()` 为什么必须发生在 `bootstrap_alloc_page()`
   之前？如果反过来，会引入什么可见的错误状态？
2. **源码定位**：在 `kernel.c` 中找出「image_bytes 同时被校验器与加载器使用」的两处
   引用，说明「描述符即真相」这个原则如何防止越界复制。
3. **动手实验**：把 `USER_IMAGE_MAX_BYTES` 改成 32（小于 `image_bytes` 37），
   重新构建运行，观察 `validate_user_image()` 返回哪个状态码、屏幕显示什么。
4. **动手实验**：把 `user_image_code[]` 里 `0xb8,0x63,...`（99 号调用）改成
   `0xb8,0x05,...`，运行 `cpl3test` 观察第 4 次调用返回什么，验证 dispatcher
   对未知号的行为与镜像字节的传递关系。
5. **Linux 对照**：阅读 `fs/binfmt_elf.c` 中 `load_elf_binary()` 的校验段顺序，
   对比它与 `validate_user_image()` 的「先格式后内容」顺序，列出 TinyOS 漏掉了
   哪几类 ELF 校验（如段重叠、文件截断、权限位）。

---

## 9. 本课小结与下一课预告

- 用户程序第一次成为**可校验的数据**：37 字节镜像内嵌于 `kernel.c`，
  描述符携带 magic/version/字节数/入口四元信息。
- `validate_user_image()` 在任何物理页分配、映射、入口跳转之前执行，五个检查
  点（魔数、版本、尺寸、入口长度、区间算术）把「无效镜像」挡在启动早期。
- `image_failure_report()` 让校验失败在 64 位内核未启动前就可读地显示在 VGA 上。
- 加载循环严格以 `image_bytes` 为界复制字节，PTE 权限仍为「代码只读 / 栈可写 /
  user 位」，用户态行为与 Lesson 31 逐字节等价。
- `long_mode_handoff` 新增四个 u32 字段，把镜像边界透传给 64 位侧。

**下一课（Lesson 33）**：把「映射注册表」升级为**有界 address-space 对象**——用
`struct address_space` 封装当前唯一的一对低/高页表，映射只接受显式用户映射
（`MAP_OWNER_USER`），内核专属/高别名地址、重复槽位、重复帧、重复释放全部被
对象边界拒绝。镜像校验边界本课已就位，下一课在此基础上做地址空间所有权隔离。
