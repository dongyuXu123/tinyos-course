# Lesson 158: capability 权限检查 — 精讲文档

> **课号**：Lesson 158（可执行课，checkpoint 快照）
> **主题**：capability 权限检查——把 Linux POSIX capability 的「权限位图 + 检查点」
> 作为概念模型精讲，并回顾 TinyOS 内核里既有的「检查通过才执行」类机制
> （VMA 权限位检查、page fault 保护分类、内核/用户 task 种类、fork 资源拷贝/共享
> 策略、模块导出符号查找），追加确定性校验的 checkpoint 模型 `lesson_151_model`。
> **课程主线位置**：资源/安全主题的「检查点课」序列（Lesson 157–162），位于
> Lesson 157（资源限制与回收）之后、Lesson 159（syscall 安全边界）之前。
> **前置课程**：[`lesson-157-stable/README.md`](../lesson-157-stable/README.md)
> **后续课程**：[`lesson-159-stable/README.md`](../lesson-159-stable/README.md)
> **一句话目标**：学完本课你能说清「capability 权限检查」在 Linux 里的样子（三套
> capability 集合、`capable()` 检查、CAP_* 常量），以及 TinyOS 教学模型用什么既有
> 机制近似它、`l158test` 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：读懂「主题宣告 + checkpoint 增量」模式下本课新增的确定性模型
`lesson_151_model` 及其 `l158test` 断言，会用 `l150test`、`l158test`、`ptrtest`、
`copytest`、`pfmodel`、`forklifecycle`、`moduletest` 等命令复现与权限相关的既有
检查，并理解 capability 概念与这些检查的对应关系。

- **在课程主线中的位置**：与 Lesson 157、159–162 同属「资源/安全主题的检查点课」，
  相邻课 `kernel64.c` 的 diff 通常只有几行（本课相对 Lesson 157 仅 4 处改动：
  `l157test`→`l150test` 改名、新增 `struct lesson_151_model` 与 `l158test`、
  exec64/about/banner 文案）。**注意：源码中没有任何 capability 位图结构或
  `capable()` 函数**——本课主题是「宣告 + 概念模型」，机制载体是继承自早期课程的
  权限检查类函数。
- **前置知识清单**：
  1. VMA 权限位模型（`VMA_R/VMA_W/VMA_X`、`vma_lookup`、`vma_range_valid`）——
     Lesson 133 的 `pf_classify` 按它分类 `PF_PROTECTION`，`uaccess_validate` 按它
     判读写权限，这两处是 TinyOS「权限检查」最直接的实现；
  2. task 种类模型（`enum task_kind { TASK_KIND_KERNEL, TASK_KIND_USER }`）——
     Linux capability 区分「特权内核操作 vs 普通用户操作」的类比；
  3. fork 资源策略（`enum resource_policy { RESOURCE_COPIED, RESOURCE_SHARED }`）——
     capability 跨 fork/exec 继承语义的类比；
  4. checkpoint 课固定模式（`struct lesson_K_model` + `lXXtest`，Lesson 133–157）。
- **本课交付**：capability 概念模型（有效/许可/可继承三集合、`capable()` 检查流、
  CAP_* 常量）；既有权限检查机制的逐函数精讲；命令 `l150test`（改名）与 `l158test`
  （新增）；`about`/banner 文案。

---

## 2. 核心概念精讲

### 2.1 概念一：POSIX capability——「权限位图」而非「全能 root」

**直觉**：传统 Unix 把一切特权都交给 uid 0（root）：「要么是 root 什么都能干，
要么不是 root 什么特权都没有」。Linux 的 POSIX capabilities 把这个粗糙的二值特权
拆成 40 多个细粒度权限位，每个权限位可以单独授予、检查、剥夺。

**准确定义**：capability = 进程/线程凭证（credential）中的一个权限位集合。Linux
`include/linux/capability.h` 定义了 `CAP_*` 常量（如 `CAP_NET_ADMIN`、
`CAP_SYS_ADMIN`、`CAP_SYS_RAWIO`），每个特权操作入口在动手前调用
`capable(cap)`（`kernel/capability.c` 的 `cap_capable`）检查调用者凭证里的
`cap_effective` 集合是否包含该位，不满足返回 `-EPERM`。

**为什么需要**：最小权限原则——守护进程只需要绑定低端口（`CAP_NET_BIND_SERVICE`），
没必要拥有 root 的全部能力。能力可以按程序运行期「有效能力」与「许可能力」动态收窄。

**工作机制**（Linux 侧）：每个线程凭证携带三套集合 + 系统级两套：
1. `cap_permitted`：进程**可能**拥有的能力上限（运行期从这份集合里扣）；
2. `cap_effective`：当前**实际生效**的能力，`capable()` 只查这一套；
3. `cap_inheritable`：fork/exec 时**可被继承**的能力；
4. 系统级 `cap_bset`（bounding set）与 `cap_ambient` 进一步收窄可获能力。
`capable(CAP_X)` = `cap_effective & (1 << CAP_X)` 非零才放行。

### 2.2 概念二：TinyOS 的「检查通过才执行」机制——capability 的本地近似

**直觉**：TinyOS 没有能力位图，但它处处在做同一件事——「先检查权限/归属，再执行
动作」。这些继承机制就是 capability 语义的教学投影：

| TinyOS 机制 | 检查什么 | 对应 Linux capability 语义 |
|------------|---------|---------------------------|
| `uaccess_validate` | 目标地址是否落在 `VMA_R/VMA_W` 允许的区间 | 内核为 `copy_to_user` 前的能力/访问权限校验 |
| `pf_classify` 的 `PF_PROTECTION` | 对只读 VMA 写 → 拒绝 | `capable()` 之外的内存保护权限 |
| `task_kind` 的 `TASK_KIND_KERNEL/USER` | 区分特权上下文与普通上下文 | 内核态 vs 用户态凭证差异 |
| `fork_model` 的 `RESOURCE_COPIED/SHARED` | fork 后资源是拷贝还是共享 | capability 的继承（inheritable）语义 |
| `module_lookup` 导出符号查找 | 只允许查 `exported && valid` 的符号 | 模块符号导出的权限边界 |
| `pmm_free_page` 的归属检查 | 只能释放 `allocated` 且未被映射的页 | 资源所有权（ownership）校验 |

**为什么这样设计**：教学内核刻意把「权限」抽象成元数据检查而非 CPU 强制——注释
反复强调 `no arbitrary pointer dereference`、`metadata only`。capability 概念在
本课的作用是给这些分散的检查提供一个统一的「权限位图」心智模型。

### 2.3 概念三：检查点模型（checkpoint model）

**直觉**：与 Lesson 157 完全相同的模式——本课新增：

```c
struct lesson_151_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

**工作机制**：`l158test` 把 `lesson_151_state` 整体赋为 `{151U,152U,153U,154U,1,1,1,1}`
（a=151, b=152, c=153, d=154，四个状态位全 1），断言 `valid && active && ready &&
accounted && b==a+1`。字面量赋值使断言恒真，输出恒为 `bounded networking,
namespaces, cgroups, and security checkpoint passed`。模型名 `lesson_151_model`
的 151 = 158−7，是「回锚」到 Lesson 151 检查点模型的记号。**教学模型：不执行任何
能力位运算，只校验元数据自洽。**

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 157） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验并加载用户镜像、构造 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核本体（1031 行）：PMM/异常/中断/调度/进程/VFS/GUI/权限检查/checkpoint 模型 | `l157test`→`l150test` 改名；新增 `struct lesson_151_model`、`l158test`；exec64 增加 `l150test`/`l158test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局（`.multiboot`/`.text64`/`.data`/`.bss`） | 未变化 |
| `Makefile` | `make all/check/run/clean`；`check` 目标 grep `capability 权限检查`、`l158test`、`Lesson 158` | 仅 grep 文案（Lesson 157→158） |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：权限检查机制精讲（继承代码）

> 说明：本课**没有**新增 capability 位图代码；以下函数都是早期课程继承的既有机制，
> 但因为它们正是本课主题「权限检查」的载体，逐函数精讲其「检查」语义。

#### 3.2.1 uaccess 权限校验（uaccess_validate / ptrtest / copytest）

```c
static TEXT64 int uaccess_validate(u64 address,u64 length,u8 access,struct uaccess_result*r){u64 end;const struct vma_model*v;r->address=address;r->length=length;r->access=access;r->canonical=address<=USER_CANONICAL_MAX&&(length==0||address<=USER_CANONICAL_MAX-length+1);r->range=length<=USER_COPY_MAX&&address<USER_RANGE_MAX&&length<=USER_RANGE_MAX-address&&address+length>=address;r->vma=0;r->permission=0;r->copied=0;if(r->canonical&&r->range&&length){end=address+length;v=vma_lookup(address);if(v&&end<=v->end){r->vma=1;r->permission=(access==UACCESS_READ?(v->prot&VMA_R):(v->prot&VMA_W))!=0;}}else if(r->canonical&&r->range&&!length){r->vma=1;r->permission=1;}return r->canonical&&r->range&&r->vma&&r->permission;}
```

逐行注释：
- 签名与职责：对一次「用户侧访问」做四层校验——canonical（是否合法规范地址）、
  range（长度与上界）、vma（是否落在 VMA 内）、permission（读写权限位是否具备），
  结果写进 `struct uaccess_result`。
- 关键行 `r->permission=(access==UACCESS_READ?(v->prot&VMA_R):(v->prot&VMA_W))!=0;`：
  这是 TinyOS 最接近「能力位检查」的一行——像 `capable(CAP_X)` 一样，把「要做的
  操作（读或写）」与「持有者 VMA 的权限位」做与运算，非零才放行。
- 边界：`length==0` 时特判为「零长度访问直接合法」；`r->copied` 留到 `uaccess_copy`
  真正「拷贝」时才置 1（且本模型不碰任何用户指针）。
- 与 capability 的对应：`uaccess_validate` 的 `permission` 位 ≈ `cap_effective`，
  `ptrtest`/`copytest` 就是一组 `capable()` 风格的「放行/拒绝」用例。

```c
static TEXT64 void ptrtest(u16*c){struct uaccess_result r;int ok=uaccess_validate(VMA_DATA_START,8,UACCESS_READ,&r)&&!uaccess_validate(USER_CANONICAL_MAX+1,8,UACCESS_READ,&r)&&!uaccess_validate(VMA_DATA_START,USER_COPY_MAX+1,UACCESS_READ,&r)&&!uaccess_validate(VMA_CODE_START,8,UACCESS_WRITE,&r);text64(c,"ptrtest: ");text64(c,ok?"canonical/range/VMA/permission checks passed":"BROKEN");putc64(c,'\n');}
```

- 算法：四个用例——①data 段读（应放行）；②非规范地址（应拒绝，canonical 失败）；
  ③长度超限（应拒绝，range 失败）；④对 r-x 的 code 段写（应拒绝，permission 失败）。
- 这组用例正是 capability 检查的教学标本：同一资源（VMA）对不同操作（读写）给出
  不同「权限裁决」。

#### 3.2.2 页故障保护分类（pf_classify 的 PF_PROTECTION 分支）

```c
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
```

- 权限检查点就在第二行：`(write && !(v->prot&VMA_W)) || (!write && !(v->prot&VMA_R))`
  ——「想写但没有写权限 / 想读但没有读权限」即判 `PF_PROTECTION`。这是 CPU 视角的
  能力检查：硬件用 PTE 的 R/W 位强制，教学模型用 VMA 权限位模拟。
- 与 capability 的对应：保护违规在 Linux 里可能是 `-EPERM`（`access_ok`/
  `perf_event_paranoid` 等），TinyOS 把它归为页故障分类的一支。

#### 3.2.3 task 种类与 fork 资源策略（特权上下文建模）

```c
enum task_kind { TASK_KIND_KERNEL=1, TASK_KIND_USER=2 };
enum resource_policy { RESOURCE_COPIED=1, RESOURCE_SHARED=2 };
```

- `task_kind` 把任务分成内核/用户两类，是「特权上下文 vs 非特权上下文」的最小模型；
  后续 `task_table_validate` 会校验每项 `kind` 非零，保证「每任务必有归属种类」。
- `resource_policy` 把 fork 时的资源处置抽象成「拷贝」与「共享」两种位——对应
  Linux capability 继承语义：`RESOURCE_COPIED` ≈ 子进程复制父进程凭证的
  `cap_permitted/cap_effective`，`RESOURCE_SHARED` ≈ 共享内核对象（如打开的文件表）。
- `fork_model_validate` 断言 `copied_metadata==RESOURCE_COPIED &&
  shared_resources==RESOURCE_SHARED`，把「拷贝/共享边界」固化为可验证不变量。

#### 3.2.4 模块导出符号查找（module_lookup）

```c
static TEXT64 int module_lookup(u64 name){u32 i;module_lookups++;for(i=0;i<SYMBOL_MAX;i++)if(exported_symbols[i].valid&&exported_symbols[i].exported&&exported_symbols[i].name_hash==name)return 1;return 0;}
```

- 检查语义：只有 `valid && exported && name_hash==name` 三项全满足的符号才对外可见
  ——「未导出符号查不到」与 capability「未授予的能力不可用」同构。
- `moduletest` 验证 `module_lookup(0x706d6d)`（pmm）命中而 `module_lookup(0x6d697373)`
  （miss）落空，输出 `moduletest: module init order and exported-symbol lookup passed`。

#### 3.2.5 本课新增 checkpoint：lesson_151_model 与 l158test

```c
struct lesson_151_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_151_model lesson_151_state;
static TEXT64 void l158test(u16*c){lesson_151_state=(struct lesson_151_model){151U,152U,153U,154U,1,1,1,1};int ok=lesson_151_state.valid&&lesson_151_state.active&&lesson_151_state.ready&&lesson_151_state.accounted&&lesson_151_state.b==lesson_151_state.a+1U;text64(c,"l158test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 151 fallback reported");putc64(c,'\n');}
```

- `struct lesson_151_model`：4 个 u32 + 4 个状态位，`a` 以 `151U` 起头，151 = 158−7，
  与 Lesson 157 的 `lesson_150_model`（150 = 157−7）同惯例，是连续两课的「回锚」链。
- `l158test` 算法：①字面量赋值；②五连断言（valid/active/ready/accounted/b==a+1）；
  ③成功串 `bounded networking, namespaces, cgroups, and security checkpoint passed`
  或失败串 `Lesson 151 fallback reported`。
- 为什么：回归探针。它不执行任何 capability 位运算；消息里的 "networking,
  namespaces, cgroups, and security" 描述继承机制的覆盖面。

#### 3.2.6 exec64 命令接线（本课增量）

```c
}else if(eq64(word,"l150test")){if(!noargs64(arg))usage64(c,"l150test");else l150test(c);}else if(eq64(word,"l158test")){if(!noargs64(arg))usage64(c,"l158test");else l158test(c);}
```

- 本课把上一课的 `l157test` 分支改名 `l150test`（其模型 `lesson_150_state` 不动，
  仍是 `{150,151,152,153}`），并新增 `l158test` 分支。
- **勘误**：旧 README 写的 `Commands: l151test` 与源码不符——源码中**不存在**
  `l151test` 命令（`grep -c l151test` 为 0），可用的 checkpoint 命令是 `l150test`
  与 `l158test`。
- about 文案 `else text64(c,"Lesson 158: capability 权限检查\n");` 与开机横幅
  `text64(&c,"Lesson 158: capability 权限检查\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT;
  unknown=-ENOSYS; bounded reclaim metadata\n");` 一起构成主题标识。

### 3.3 构建管线（Makefile / linker）

- `kernel64.o`：`gcc $(CFLAGS64) -c`。`CFLAGS64` 含 `-m64 -ffreestanding -fpie
  -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror`——`-fpie` 允许 RIP 相对
  寻址（`leaq` 取 stub 地址依赖它），`-mno-red-zone` 防止中断路径踩红区。
- `kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld -nostdlib` 再 `objcopy -O binary`；
  `kernel64.ld` 从 0 开始布局，`.data` 内用 `. = ALIGN(0x1000)` 依次划出
  idle/rsp0/ist1 三块 guard+stack，末尾三条 `ASSERT(...==0x1000)` 锁死每块栈尺寸。
- `boot.o`：`gcc $(CFLAGS)`（32 位），依赖 `build/kernel64.bin`——外层 `.text64`
  段 `kernel_main64` 以 `.incbin` 嵌入二进制。
- `kernel.iso`：`ld -m elf_i386 -T linker.ld` 链接外层 ELF32，`grub-mkrescue` 出 ISO；
  `linker.ld` 保证 `.multiboot` 在 1 MiB 起、8 字节对齐、`.text64` 紧随其后。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`capability 权限检查`、`l158test`、`Lesson 158`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**。Makefile 仅 `check` 目标的 grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) 解析 MBI/建页表
  → enter_long_mode (boot.S) CR4.PAE → EFER.LME → CR0.PG → far jump
  → kernel_main64_binary (kernel64.c)
       module_init_model() → init_model_start() → wait_model_start()
       → adoption_start() → resource_start()
       → pmm_init() → vma_init() → reclaim_init() → vfs_init()
       → 进程/线程元数据装配 → framebuffer_init
       → stack_guards_init / runtime_gdt_tss_init / idle_init / install_idt
       → pit_init()+pic_init() → 横幅 "Lesson 158: capability 权限检查\n..."
       → 键盘 shell 循环：kbd_dequeue → exec64(word)
  exec64 分支 → ptrtest:uaccess_validate×4（放行/拒绝各半）
             → copytest:uaccess_copy×4（成功/失败记账）
             → pfmodel:pf_classify 三类分类
             → forklifecycle:fork_model_validate 拷贝/共享边界
             → moduletest:导出符号查找
             → l150test / l158test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以本课主题相关的三条命令为例：

1. **`about`** → exec64 命中 `about` 分支 → `text64(c,"Lesson 158: capability 权限检查\n")` → 屏幕打印 `Lesson 158: capability 权限检查`。
2. **`ptrtest`** → `ptrtest(c)` 对四个地址/权限组合依次 `uaccess_validate`：
   data 段读（放行）、非规范地址（拒绝）、超长拷贝（拒绝）、code 段写（拒绝）→
   四结果与运算 → 输出 `ptrtest: canonical/range/VMA/permission checks passed`。
3. **`l158test`** → `l158test(c)` 对 `lesson_151_state` 赋值并五连断言 → 输出
   `l158test: bounded networking, namespaces, cgroups, and security checkpoint passed`。

VGA 输出管线全由 `putc64`/`text64`/`hex64` 驱动（0xB8000 文本模式，属性 0x0f 白字
黑底，80×25），`clear64` 负责清屏。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-158-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `capability 权限检查`、`l158test`、`Lesson 158` 与 kernel64.c 中的 `l158test`，
  全部命中输出 `Multiboot2 and Lesson 158 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。
  启动横幅第一行为 `Lesson 158: capability 权限检查`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 158: capability 权限检查`
  2. `l158test` → `l158test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l150test` → `l150test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `ptrtest` → `ptrtest: canonical/range/VMA/permission checks passed`
  5. `copytest` → `copytest: copy_to_user/from_user bounded success/failure accounting
     passed`，下一行 `no source/destination bytes touched; pointers were never
     dereferenced`
  6. `pfmodel` → `pfmodel: not-present/protection/unmapped classified; bounded page
     inserted`
  7. `forklifecycle` → `fork lifecycle: passed (identity, parent, copy/share
     boundaries, no execution)`
  8. `moduletest` → `moduletest: module init order and exported-symbol lookup passed`
- **如何判断成功**：上述命令逐一打印预期串即成功；`make check` 三条 grep 全命中即
  通过。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l158test` 输出 `Lesson 151 fallback reported` | checkpoint 模型字段被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l158test` 的赋值 `{151U,152U,153U,154U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| 输入 `l151test` 报 `unknown command` | 旧 README 命令名是笔误，源码无此命令 | 源码中可用命令是 `l150test` 与 `l158test` |
| `ptrtest` 输出 `BROKEN` | `uaccess_validate` 某层检查与断言不符（如 `USER_COPY_MAX` 改动、VMA 未初始化） | `vmainfo` 确认三块 VMA 存在；检查 `USER_CANONICAL_MAX/USER_RANGE_MAX/USER_COPY_MAX` 宏值 |
| `copytest` 的 `uaccess_failures` 计数异常 | `uaccess_validate` 在 `uaccess_copy` 前未成功返回 | `ptrinfo` 看 attempts/success/failure 三段计数；确认 code 段 `r-x` 无 `VMA_W` |
| `pfmodel` 输出 `BROKEN` | `pf_classify` 的 `PF_PROTECTION` 分支未按预期命中（如 VMA prot 被改） | `vmainfo` 确认 code 段为 `r-x`；检查 `vma_init` 是否在 `pfmodel` 前调用 |
| `forklifecycle` 输出 `BROKEN` | `fork_model` 的 copied/shared 标志被改 | 检查 `RESOURCE_COPIED/RESOURCE_SHARED` 宏与 `fork_model_validate` 断言 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 158: capability 权限检查`；`make check` 的 grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **capability 数据结构**：Linux 线程凭证里的能力三集合在
   `include/linux/cred.h` 的 `struct cred`（`kernel_cap_t cap_effective,
   cap_inheritable, cap_permitted`）与 `include/linux/capability.h`（`CAP_*` 常量，
   `kernel_cap_t` 是 `struct { __u32 cap[2]; }`，共 64 位）中定义；TinyOS 没有对应
   结构体，本课把 capability 作为**概念模型**讲解，机制上以 VMA 权限位
   （`VMA_R/VMA_W/VMA_X`）做教学近似。
2. **检查入口 `capable()`**：Linux `kernel/capability.c` 的 `cap_capable` 查
   `cred->cap_effective`，返回 0 或 `-EPERM`；TinyOS 的 `uaccess_validate` 查
   `v->prot` 位并返回布尔，`pf_classify` 的 `PF_PROTECTION` 分支查读写权限——
   同为「检查权限位再放行」，只是 TinyOS 用 VMA 位图而非全局能力位图。
3. **权限授予/继承**：Linux 在 fork（`copy_creds`）与 exec（`cap_bprm_apply_creds`，
   `security/commoncap.c`）时按 inheritable/bset 计算新凭证；TinyOS `fork_model` 用
   `RESOURCE_COPIED/RESOURCE_SHARED` 两个标志表示「拷贝 vs 共享」边界，
   `fork_model_validate` 固化之。
4. **特权上下文区分**：Linux 用 `capable()` 前先看 `current_cred()`，用户态进程可能
   因为 `cap_effective` 为空而 `-EPERM`；TinyOS `enum task_kind` 的
   `TASK_KIND_KERNEL/TASK_KIND_USER` 是最粗糙的「特权/非特权」二分，且
   `task_table_validate` 保证每任务必有 kind。
5. **模块符号导出**：TinyOS `module_lookup` 只放行 `valid && exported` 的符号；
   Linux 的模块导出表 `__ksymtab`（`kernel/module.c` + `EXPORT_SYMBOL`）只允许
   `EXPORT_SYMBOL` 的符号被 `symbol_put`/`find_symbol` 查到，未导出符号查不到——
   机制一一对应，TinyOS 用 4 槽小表模拟。
6. **内存保护权限**：Linux 的 `access_ok`（`include/linux/uaccess.h`）在
   `copy_to_user` 前检查地址范围，VMA 权限由 `get_user_pages` 的
   `FOLL_WRITE` 等按 `vm_flags` 校验；TinyOS `uaccess_validate` 同时做 canonical/
   range/vma/permission 四层检查，是这两者的合并极简版。

**权威来源**：Linux `include/uapi/linux/capability.h`（CAP_* 常量）、
`kernel/capability.c`（cap_capable）、`include/linux/cred.h`（struct cred）、
`security/commoncap.c`（cap_bprm_apply_creds）、Intel SDM Vol.3A。
**教学模型简化了什么**：真实 capability 有完整的三集合 + bounding set + ambient
机制、随 uid/gid 变化的 `cap_task_fix_setuid` 规则，以及 `capability` LSM 钩子；
TinyOS 只把它们抽象成「VMA 权限位 + 资源拷贝/共享标志 + 导出符号过滤」三类检查，
没有能力位图结构，也没有 `-EPERM` 的返回值语义。

---

## 8. 思考题与练习

1. **概念理解**：Linux 的 `capable(CAP_SYS_ADMIN)` 检查的是三集合中的哪一套？为什么
   不是 `cap_permitted`？TinyOS 的 `uaccess_validate` 中哪个字段相当于
   `cap_effective`？
2. **源码定位**：在 `kernel64.c` 中找出 `pf_classify` 的 `PF_PROTECTION` 判定条件，
   说明它与 `uaccess_validate` 的 `permission` 判定有什么异同（提示：一个判 VMA
   权限，另一个判什么）。
3. **动手实验**：修改 `l158test` 的赋值，把 `c` 从 `153U` 改成 `153U+2`，重新构建
   运行，观察输出是否仍为 passed；再把 `accounted` 改成 `0`，观察输出翻转。
4. **动手实验**：在 `ptrtest` 里增加一个用例 `uaccess_validate(VMA_STACK_START,4,
   UACCESS_WRITE,&r)`，预测结果并解释 stack 段（`rw-`）对写访问是否放行。
5. **Linux 对照**：阅读 `security/commoncap.c` 的 `cap_bprm_apply_creds` 与
   `include/linux/capability.h` 的 `CAP_NET_ADMIN` 等常量，对比 capability 的
   「exec 时计算新集合」与 TinyOS `fork_model` 的「copied/shared 二值标志」，
   指出两者粒度上的差距。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是资源/安全主题的检查点课，`kernel64.c` 相对上一课只有 4 处小增量，主题由
   banner/about 文案标识，**源码中无 capability 位图结构**——本课是「主题宣告 +
   概念模型」型检查点课。
2. POSIX capability 把 root 二值特权拆成 `CAP_*` 位图，三集合
   （permitted/effective/inheritable）+ bounding set 决定「能有什么、现在能用什么、
   能传给谁」，`capable()` 只查 `cap_effective`。
3. TinyOS 的本地近似：`uaccess_validate` 的 VMA 权限位检查、`pf_classify` 的
   `PF_PROTECTION`、`task_kind` 内核/用户二分、`fork_model` 的 copied/shared 策略、
   `module_lookup` 的导出符号过滤。
4. 新 checkpoint `l158test` 用字面量赋值 + 五连断言固化回归探针；模型名
   `lesson_151_model` 的 151 = 158−7 延续「回锚」惯例。
5. 旧 README 的 `Commands: l151test` 已勘误为源码实际的 `l150test` 与 `l158test`。

**下一课**：[`lesson-159-stable/README.md`](../lesson-159-stable/README.md) 主题为
「syscall 安全边界」，将站在本课「权限检查」与既有 syscall 调度器
（`syscall_dispatch` 返回 `-ENOSYS`、`WRITE_CONSOLE` 不用用户指针）之上，讲解
syscall 入口的安全边界如何被教学模型固化为新的 checkpoint 模型（命令
`l159test`）。两课的衔接点是「安全边界检查」：本课讲「能力/权限的检查」，
下节课讲「系统调用入口的边界防护」。
