# Lesson 30: 有界 syscall dispatcher 与错误返回 — 精讲文档

> **课号**：30　**主题**：bounded syscall dispatcher and error returns
> **课程主线位置**：阶段五（用户态与 syscall）——在 Lesson 29（最小 `int 0x80` ABI）之后、
> Lesson 31（受控用户返回与 `SYS_EXIT`）之前，把单号 syscall 升级为**有界 dispatcher**：
> 三个服务号 + 未知号 `-ENOSYS`，用户 stub 依序调用 `0,1,2,99`。
> **前置课程**：[../lesson-29-stable/README.md](../lesson-29-stable/README.md)
> **后续课程**：[../lesson-31-stable/README.md](../lesson-31-stable/README.md)
> **一句话目标**：让 `int 0x80` 从「只会 GETTICKS」变成「按号码分派到 GETTICKS /
> GETPID / WRITE_CONSOLE，未知号返回 `-ENOSYS`」，并保持 all-GPR 帧、`iretq` 与
> 用户 IF=0 不变。

> **Course status: learning implementation; no stable snapshot.**
> 本目录为学习实现（继承 Lesson 29 稳定快照并扩展 dispatcher），README 为精讲文档。
> 保留并沿用 Lesson 29 的 all-GPR 帧、`iretq`、IF=0 用户帧、只读用户代码映射、
> PMM 固定用户页与维护性修正。

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能在 `int 0x80` 上叠加一个**有界**的系统调用分派层：
  用 `switch((u32)f->rax)` 实现三个服务号，未知号用 `(u64)(-(s64)ENOSYS)` 返回负错误码，
  并理解「WRITE_CONSOLE 不接触用户指针」为什么是安全边界。

- **在课程主线中的位置**：上一课证明了「进得去、回得来」（单号往返）；本课把单号改造成
  「号码 → 分派 → 返回值/错误码」，系统调用从此有了真正的接口形状。下一课
  （Lesson 31）将加入 `SYS_EXIT`，让用户 stub 能在打印完所有结果后显式终止而非死循环。

- **前置知识清单**：
  1. Lesson 29 的 `syscall_frame`（20×8）与 `syscall_entry` 的 push/pop 约定；
  2. IDT 向量 `0x80` 的 DPL3 门（`idt[0x80].type=0xee`）；
  3. 负数在 64 位下的补码表示与 `(u64)(-(s64)n)` 的类型转换；
  4. `f->rax` 槽作为返回值载体的机制（offset 112）；
  5. 用户 stub 的字节级生成（`kernel.c`）。

- **本课交付**（可见结果）：
  - `syscallinfo` 命令打印 `syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE; unknown=-ENOSYS`
    与 `WRITE_CONSOLE uses a fixed kernel-owned message and no user pointer`；
  - `cpl3test` 依序调用 `0,1,2,99`，屏幕出现
    `TinyOS lesson 30 syscall dispatcher`、`syscall number: <n>`、`return rax: <r>`；
    `0→ticks`、`1→0000000000000001`、`2→0`（且打印 `kernel-owned console message`）、
    `99→ffffffffffffffda`（`-38` 的补码）。

## 2. 核心概念精讲

### 2.1 系统调用号与分派表

- **定义**：用户把「要什么服务」编码成一个整数号放在 `RAX`（本课低 32 位）
  传给内核；内核按号查一个**固定的、有边界的**分派集合。
- **为什么需要（动机）**：单一 syscall 不需要号码；但多个服务共用同一个 `int 0x80`
  入口时，必须用号区分。有界 = 号码集合在编译期固定（本课 0/1/2），未知号有明确错误
  语义——这是「系统调用接口」的最小形态。
- **工作机制**：`syscall_dispatch` 用 `switch((u32)f->rax)` 分派；`default` 返回
  `(u64)(-(s64)ENOSYS)`。`ENOSYS=38`，`-(s64)38 = -38`，转 `u64` 后为 `0xffffffffffffffda`。
  这与 Linux「负错误码」约定一致（`sys_call_table` 的 `-ENOSYS`）。

### 2.2 错误码与返回值通道

- 返回值和错误码走**同一个通道**：`f->rax` 槽。成功返回正数（`ticks`、`FIXED_PID`、`0`），
  失败返回负数（补码）。调用方（用户）按「rax < 0」即错误的约定解读——本课用户 stub
  不解读，只打印 `return rax`。
- 为什么用负错误码而不是单独状态位：沿袭 POSIX 惯例，返回值单通道最简洁；
  `s64` 引入（`typedef long long s64`）正是为了做有符号补码运算。

### 2.3 不接触用户指针（WRITE_CONSOLE 的安全边界）

- 大多数 syscall 要读写用户内存（如 write 的缓冲区），这需要「copy_from_user」与
  地址校验。本课 `SYS_WRITE_CONSOLE` **不接收、不解引用任何用户指针**，只把一条
  内核自有的字符串打印到 VGA：`text64(c,"kernel-owned console message\n")`。
- 为什么这样设计：教学阶段先绕开「用户指针可信吗」问题；固定内核消息让分派逻辑
  可完整验证而无需页面边界/可访问性检查。`syscallinfo` 明确写出这一设计选择。

## 3. 源码精讲（本课最长章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|------|------|------------------------|
| `boot.S` | 32 位引导 | **未变化** |
| `kernel.c` | 32 位引导期页表与用户 stub | **主要增量**：用户 stub 生成 4 次 syscall（号码 0,1,2,99）循环 |
| `kernel64.c` | 64 位续体 | **主要增量**：`s64`、三个 syscall 宏、`syscall_dispatch`、`syscall_report` 文案、`syscallinfo` 命令 |
| `kernel64.ld` | 64 位链接脚本 | **未变化** |
| `linker.ld` | 32 位 ELF | **未变化** |
| `Makefile` | 构建 | **未变化** |
| `grub.cfg` | GRUB 菜单 | **未变化**（仍为 `TinyOS lesson 29: int 0x80 syscall ABI`） |

### 3.2 kernel.c：四次 syscall 的用户 stub

```c
{ volatile u8 *code=(volatile u8 *)(unsigned long)(u32)long_mode_handoff.user_code_phys;
  volatile u64 *pt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[USER_CODE_VA/(PAGE_ENTRIES*PAGE_SIZE)];
  volatile u64 *st=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[USER_STACK_VA/(PAGE_ENTRIES*PAGE_SIZE)];
  u32 i=0; u32 calls[4]={0,1,2,99};
  for(;i<4;i++){
      code[i*7]=0xb8;                          /* mov eax, imm32 */
      code[i*7+1]=(u8)calls[i];                /* 立即数低字节 */
      code[i*7+2]=(u8)(calls[i]>>8);
      code[i*7+3]=(u8)(calls[i]>>16);
      code[i*7+4]=(u8)(calls[i]>>24);
      code[i*7+5]=0xcd;code[i*7+6]=0x80;       /* int 0x80 */
  }
  code[28]=0xeb;code[29]=0xe2;                 /* jmp rel8=-30：回到第一条 mov */
  pt[(USER_CODE_VA/PAGE_SIZE)%PAGE_ENTRIES]=long_mode_handoff.user_code_phys|PTE_PRESENT|PTE_USER;
  st[(USER_STACK_VA/PAGE_SIZE)%PAGE_ENTRIES]=long_mode_handoff.user_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER; }
```

- 逐字节说明：4 组 `mov eax,imm32; int 0x80` 每组 7 字节，共 28 字节；第 28/29 字节是
  `jmp rel8=-30`（`0xeb 0xe2`），跳回第 0 字节——于是用户态无限循环执行
  `0,1,2,99` 四次 syscall。
- 用户代码页仍是只读 `PTE_PRESENT|PTE_USER`；栈页保留 Writable。
- `calls[4]` 用运行时数组生成指令立即数，号码 99 故意选为「未知号」以触发 `-ENOSYS` 路径。

### 3.3 kernel64.c：syscall 号常量与分派

```c
#define SYS_GETTICKS 0U
#define SYS_GETPID 1U
#define SYS_WRITE_CONSOLE 2U
#define ENOSYS 38
#define FIXED_PID 1ULL
typedef long long s64;
```

#### 函数：`syscall_dispatch`

```c
static TEXT64 u64 syscall_dispatch(struct syscall_frame*f,u16*c){
    switch((u32)f->rax){
        case SYS_GETTICKS:return ticks;                 /* 返回 PIT 时钟计数 */
        case SYS_GETPID:return FIXED_PID;               /* 固定进程号 1 */
        case SYS_WRITE_CONSOLE:text64(c,"kernel-owned console message\n");return 0;
        default:return (u64)(-(s64)ENOSYS);             /* 未知号 → -38 */
    }
}
```

- 签名与职责：接收 syscall 帧与输出光标，按 `f->rax` 低 32 位分派，返回要写入
  `f->rax` 的结果。
- 输入输出：输入号码（帧内 `rax` 槽）；输出返回值（u64），WRITE_CONSOLE 额外向 VGA
  打印一条内核自有消息。
- 算法步骤：`switch` 三分支 + `default`；成功分支直接 return 常量/ticks；错误分支做
  `(u64)(-(s64)ENOSYS)` 补码转换。
- 边界与错误处理：`(u32)f->rax` 只取低 32 位（用户可能只写 EAX）；`default` 覆盖所有
  非 0/1/2 号码，保证「有界」——任何号都有定义，不落空。
- 设计动机：与 Linux `arch/x86/entry/common.c` 的 `do_syscall_64` 用 `nr` 查
  `sys_call_table` 同构，只是用编译期 switch 代替表查找。

#### 函数：`syscall_report`

```c
TEXT64 void syscall_report(struct syscall_frame*f){
    u16 c=0;u64 number=f->rax,result;clear64(&c);
    result=syscall_dispatch(f,&c);                     /* 先分派，拿到结果 */
    text64(&c,"TinyOS lesson 30 syscall dispatcher\nsyscall number: ");
    hex64(&c,number);
    text64(&c,"\nreturn rax: ");f->rax=result;hex64(&c,f->rax);   /* 写回 rax 槽 */
    text64(&c,"\nuser rip: ");hex64(&c,f->rip);
    text64(&c,"\nuser cs: ");hex64(&c,f->cs);
    text64(&c,"\nuser rsp: ");hex64(&c,f->rsp);
    text64(&c,"\nuser ss: ");hex64(&c,f->ss);
    text64(&c,"\nall-GPR frame; returning with iretq; user IF remains disabled\n");
}
```

- 签名与职责：syscall 入口的 C 处理器；先分派得到 `result`，再打印报告并把结果写回
  `f->rax`。
- 算法步骤：(1) 记录原始号码 `number`；(2) 调 `syscall_dispatch`；(3) 打印头部与号码；
  (4) `f->rax=result` 并打印；(5) 打印用户现场；(6) 打印返回提示。
- 与上一课差异：先「分派」再「打印号码」，且文案改为 dispatcher 风格；返回值来源
  从 `ticks` 变为 `syscall_dispatch` 的任意结果（含负数）。
- 汇编侧 `syscall_entry` 完全复用上一课：`movq 112(%rdi),%rax` 重载 `f->rax`，
  `addq $8,%rsp` 跳槽，`iretq` 回用户。

### 3.4 shell 命令与 banner

```c
else if(eq64(word,"syscallinfo")){...text64(c,"syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE; unknown=-ENOSYS\nWRITE_CONSOLE uses a fixed kernel-owned message and no user pointer\n");}
else if(eq64(word,"cpl3test")){... text64(c,"entering CPL3 syscall stub with IF=0; calls 0,1,2,99\n"); enter_user(h); }
```

- `help` 字符串加入 `syscallinfo`；`about`/`idtinfo`/异常报告文案沿用上一课。
- banner（源码逐字）：

```
TinyOS lesson 30: bounded syscall dispatcher
GETTICKS, GETPID, WRITE_CONSOLE, unknown=-ENOSYS; all-GPR frame and IF=0
```

### 3.5 构建管线与主控制流

- `Makefile`/链接脚本/`grub.cfg` 均未变化（`grub.cfg` 菜单仍标 lesson 29，属上游遗留文案）。
- 主控制流：`cpl3test` → `enter_user` → `enter_user_c`（RFLAGS=0x002，IF=0）→ 用户
  `mov eax,0; int 0x80` → DPL3 门 → `rsp0` 换栈 → `syscall_entry` 压 15 GPR →
  `syscall_report`（分派+打印）→ `movq 112(%rdi),%rax` → pop → `iretq` → 用户
  `mov eax,1; int 0x80` … 直到 `99` 后再 `jmp -30` 循环。

## 4. 数据流与运行逻辑

- 命令 `cpl3test` → 打印 `entering CPL3 syscall stub with IF=0; calls 0,1,2,99` →
  进入用户态，四次 syscall 依次到达 `syscall_report`。
- 第 1 次（号码 0）：`syscall_dispatch` 命中 `SYS_GETTICKS` → `return rax: <ticks>`。
- 第 2 次（号码 1）：命中 `SYS_GETPID` → `return rax: 0000000000000001`（`FIXED_PID`）。
- 第 3 次（号码 2）：命中 `SYS_WRITE_CONSOLE` → 打印
  `kernel-owned console message`，`return rax: 0000000000000000`。
- 第 4 次（号码 99）：`default` → `return rax: fffffffffffffffda`（`-ENOSYS` 补码）。
- 每次都在 `syscall_report` 打印 `user rip/cs/rsp/ss` 后 `iretq` 回用户；
  `jmp -30` 使流程循环，屏幕持续刷新四段报告。

## 5. 构建、运行与验证

- **构建命令**（与 Makefile 一致）：

```bash
cd /home/dongyu/.zcode/workspace/default/lessons/lesson-30-stable
make clean && make -j"$(nproc)"
make check
```

- **运行命令**：`make run`（QEMU VGA 图形窗口，勿加 `-display none`）。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. 启动后输入 `idtinfo`：与上一课一致（DPL3 syscall 门）；再输入 `syscallinfo`：预期
     ```
     syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE; unknown=-ENOSYS
     WRITE_CONSOLE uses a fixed kernel-owned message and no user pointer
     ```
  2. 从**全新启动**运行 `cpl3test`：预期先打印
     `entering CPL3 syscall stub with IF=0; calls 0,1,2,99`，随后清屏出现四段
     `TinyOS lesson 30 syscall dispatcher` 报告：
     - `syscall number: 0000000000000000` → `return rax: <ticks>`；
     - `syscall number: 0000000000000001` → `return rax: 0000000000000001`；
     - `syscall number: 0000000000000002` → 屏幕含 `kernel-owned console message` 与
       `return rax: 0000000000000000`；
     - `syscall number: 0000000000000063` → `return rax: fffffffffffffffda`（= -38）。
  3. 每段都含 `user cs: 0000000000000033`、`user ss: 000000000000002b` 与
     `all-GPR frame; returning with iretq; user IF remains disabled`。
  4. 回归 `lminfo`、`hhinfo`、`meminfo`、`tssinfo`、`stackinfo`、`vminfo`/`vmtest`、
     `bptest`、`preempttest`、`idletest`。
- **判断成功**：`cpl3test` 的四段报告号码与 `return rax` 完全符合上表（含 99 的
  `-ENOSYS` 补码），且 `WRITE_CONSOLE` 段可见固定消息；其余回归一致。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| 99 号返回 0 而非 `...da` | `default` 分支丢失或 `ENOSYS` 宏未定义 | 检查 `syscall_dispatch` 的 `default:return (u64)(-(s64)ENOSYS);`；确认 `ENOSYS 38` |
| `-ENOSYS` 打印成 `ffffffffffffffda` | 预期行为（-38 补码） | `syscallinfo` 文案核对；计算 `-(s64)38` 的 64 位补码 |
| 只看到一段报告 | 用户 stub 循环字节错（`jmp` 偏移不对） | 检查 `code[28]=0xeb;code[29]=0xe2`（rel8=-30）；确认 4×7=28 字节布局 |
| 号码 2 不打印固定消息 | `syscall_dispatch` 未传 `c` 或顺序错 | 确认 `syscall_report` 先 `syscall_dispatch(f,&c)` 再用 `c` 打印 |
| 号码错位（1 显示 0） | `code[i*7+1..+4]` 立即数小端拆分错 | 对拍 `calls[i]` 的四字节拆分；`(u8)(calls[i]>>24)` 应为最高字节 |
| `return rax` 恒为上一号结果 | `movq 112(%rdi),%rax` 重载失败或 `f->rax=result` 缺失 | 确认 `syscall_report` 里 `f->rax=result`；核对 offset 112 |
| 用户态死机而非循环 | 用户代码页被改可写/不可执行 | 回归检查 `PTE_PRESENT|PTE_USER`（无 NX 位默认可执行，只读不变） |

## 7. 与 Linux 源码对照

- **TinyOS**：`syscall_dispatch` 用 `switch((u32)f->rax)` 分派 0/1/2，`default` 返回
  `-ENOSYS`；`WRITE_CONSOLE` 不接触用户指针。
- **Linux 对照**：
  - `arch/x86/entry/common.c` 的 `do_syscall_64` 用 `nr` 索引 `sys_call_table`
    （`arch/x86/entry/syscall_64.c`），越界返回 `-ENOSYS`（`__x64_sys_ni_syscall`）；
  - `fs/read_write.c` 的 `ksys_write`/`copy_from_user` 演示了「安全拷贝用户指针」，
    而 TinyOS 本课刻意不实现；
  - `include/uapi/asm-generic/errno.h` 定义 `ENOSYS=38`——TinyOS 的 `ENOSYS 38` 与其一致。
- **权威来源**：Intel SDM Vol.3 §6.7（`int n`）；POSIX 错误码惯例；Linux
  `arch/x86/entry/` 与 `include/uapi/asm-generic/errno.h`。
- **教学模型简化**：编译期 switch 代替 `sys_call_table`；无用户指针拷贝（固定消息）；
  无 `copy_from_user`/access_ok；用户态仍无中断、无调度。

## 8. 思考题与练习

1. **概念理解**：为什么未知号返回负数而不是正错误码？`(u64)(-(s64)38)` 与
   `0xffffffffffffffda` 的关系是什么？
2. **源码定位**：在 `kernel.c` 中找出生成 `calls[4]` 指令的循环，说明每条 syscall
   stub 为什么恰好占 7 字节，`jmp rel8=-30` 如何回到第一条。
3. **动手实验**：把 `calls[4]` 改成 `{0,2,2,3}`，重建运行 `cpl3test`，观察第二/三次
   报告与 WRITE_CONSOLE 消息次数变化，验证分派与 stub 生成解耦。
4. **动手实验**：新增 `SYS_DUMMY 3U` 并让 `default` 返回 `0`（去掉 ENOSYS），运行
   `cpl3test`，说明有界性与错误返回之间的工程权衡。
5. **Linux 对照**：阅读 `arch/x86/entry/syscall_64.c` 的 `sys_call_table` 与
   `do_syscall_64` 的越界处理，对比 TinyOS `syscall_dispatch` 的 `default`，
   总结「表查找 vs switch」在教学场景的取舍。

## 9. 本课小结与下一课预告

- 本课在 Lesson 29 的 `int 0x80` ABI 上叠加了**有界 dispatcher**：
  `syscall_dispatch` 用 switch 分派 `SYS_GETTICKS(0)`/`SYS_GETPID(1)`/
  `SYS_WRITE_CONSOLE(2)`，未知号（如 99）返回 `(u64)(-(s64)ENOSYS)`。
- 返回值仍经 all-GPR 帧的 `rax` 槽传递，`syscall_entry` 汇编与上一课完全复用；
  用户 IF=0、只读用户代码映射、PMM 固定用户页保持不变。
- `WRITE_CONSOLE` 只打印内核自有字符串、不解引用用户指针，是本课明确的安全边界。
- 用户 stub 用字节级循环生成 4 次 syscall（`0,1,2,99`）并以 `jmp -30` 循环；
  `syscallinfo` 命令把号码表与设计选择写进 shell。
- `cpl3test` 的四段报告（号码 0/1/2/63）与对应 `return rax`
  （ticks / 1 / 0 / `ffffffffffffffda`）构成完整验证。
- 已知边界（延续旧 README 记录）：调度器与用户 IRQ 保持关闭；无用户指针安全拷贝；
  无进程/地址空间对象；`grub.cfg` 菜单文案仍为 lesson 29（上游遗留，不影响运行）。
- **下一课**（[../lesson-31-stable/README.md](../lesson-31-stable/README.md)）：在继承
  本课 bounded dispatcher 的基础上引入 **`SYS_EXIT`（3）** 与受控用户返回：用户 stub
  调完 `0,1,2,99` 后调用 `SYS_EXIT`，内核报告有效用户帧并有意停机，不再回到用户态，
  形成用户程序「正常结束」的路径。
