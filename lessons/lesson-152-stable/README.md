# Lesson 152: user namespace — 精讲文档

> **课号**：Lesson 152 ｜ **主题**：user namespace（用户命名空间）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课为「namespace 三连」之后的新成员，连接 PID namespace（151）与 cgroup 系列（153–156）
> **前置课程**：[`../lesson-151-stable/README.md`](../lesson-151-stable/README.md)（PID namespace）
> **后续课程**：[`../lesson-153-stable/README.md`](../lesson-153-stable/README.md)（cgroup 层级）
> **一句话目标**：讲清 user namespace 如何让容器内外「同一进程看到不同的 UID/权限」，对照 Linux `kernel/user_namespace.c`、`kernel/cred.c` 与 `include/linux/uidgid.h`，并把教学内核中继承的**访问控制设施**（`uaccess_validate`/`uaccess_copy` 的 canonical/range/permission 检查、VMA 权限位、CPL3 用户态进入）按 user namespace 主题系统化复述，运行 `l152test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（user namespace / UID 映射）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l151test` 恢复为历史命名 `l144test`（挂 `lesson_144_state`），新增 `lesson_145_model`/`lesson_145_state` 与 `l152test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l152test`（旧 README 所写 `l145test` 按源码勘误）；另保留历史检查点 `l100test`–`l144test`，以及 `copytest`/`vmatest`/`vmfaulttest`/`cpl3test`/`userpitest`/`syscallinfo` 等访问控制回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「UID 从 0 开始的伪根 + 权限位映射」解释 user namespace（容器里 `root` 其实是个普通 UID）；说出 Linux 中 `user_namespace`、`cred` 与 `capability` 的关系（`kernel/user_namespace.c`、`kernel/cred.c`、`kernel/capability.c`）；在教学内核中沿 `uaccess_validate`（canonical/range/vma/permission 四重检查）→ `copytest` → `cpl3test` 观察访问控制与用户态进入；运行 `l152test`/`copytest` 验证。

**在课程主线中的位置**：Lesson 151 讲完 PID namespace（进程号隔离），本课转向**用户与权限隔离**——namespace 家族中与「谁有权做什么」直接相关的一支。作为检查点课，源码 diff 极小（只有模型号与主题串推进），任务是把继承机制中与「用户/权限」相关的设施（`uaccess` 元数据、VMA 权限位、CPL3 段选择子与 `enter_user`）按 user namespace 主题系统化复述。下一课（Lesson 153）转入 cgroup 系列。

**前置知识清单**（学本课前必须掌握）：
1. VMA 模型：`vma_table`、`VMA_R/VMA_W/VMA_X` 权限位与 `vma_lookup`（Lesson 41s/64s）。
2. uaccess 元数据：`USER_CANONICAL_MAX`/`USER_RANGE_MAX`/`USER_COPY_MAX`、`struct uaccess_result`（Lesson 42/66s）。
3. CPL3 进入：`USER_CS 0x33`/`USER_DS 0x2b`、`enter_user`、`cpl3test`/`userpitest`（Lesson 30s）。
4. syscall 面：`SYS_GETPID`/`SYS_WRITE_CONSOLE`/`SYS_EXIT` 与 `-ENOSYS`（Lesson 30s）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–151）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 152: user namespace`；
- 新命令 `l152test` 输出 `l152test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `copytest`/`vmatest`/`cpl3test` 继续展示权限检查与用户态进入。

---

## 2. 核心概念精讲

### 2.1 user namespace：每个「世界」里的 root

**直觉**：容器里执行 `whoami` 显示 `root`，UID 是 0；宿主机 `ls -n` 却显示这个文件属于 UID 1000 的普通用户。同一进程在两个「世界」里有不同的用户身份——这就是 **user namespace**。

**准确定义**：user namespace 是**用户 ID（UID/GID）与权限（capability）的隔离域**。每个 user namespace 有自己的 UID/GID 映射表（`uid_map`/`gid_map`）：把容器内的 0 映射到容器外的某个普通 UID。进程的「有效权限」由它**当前所在 user namespace** 决定——在容器内它拥有全部 capability，出了容器它只是普通用户。Linux 里 `setuid` 后的 PID 1 若处于不同 user namespace，其 `root` 身份只在该 namespace 内成立。

### 2.2 为什么需要 user namespace（动机）

1. **无特权容器**：没有 user namespace 时，容器内「root」就是宿主 root，逃逸即提权；有了它，容器内 UID 0 的真实身份是宿主普通用户，权限被封在 namespace 里。
2. **capability 隔离**：`CAP_SYS_ADMIN` 这类能力是按 user namespace 判定的——namespace 内的完整能力集在宿主机视角被裁剪。
3. **挂载/权限兼容**：rootfs 里的文件属主是 0 号 UID，容器需要「看到」0 号 UID 才能正常 chown，user namespace 提供映射而非真实切换。

### 2.3 Linux 中 user namespace 的工作机制

- **数据结构**：`kernel/user_namespace.c` 定义 `struct user_namespace`（`uid_map`/`gid_map`、`parent`、`level`）；`kernel/cred.c` 的 `struct cred`（`uid/gid/euid/egid/cap_effective`）挂在 `task_struct->cred` 上。
- **映射**：`/proc/self/uid_map` 形如 `0 1000 1`——「namespace 内 UID 0 ↔ namespace 外 UID 1000，长度 1」。`from_kuid()`/`make_kuid()` 负责双向换算。
- **权限判定**：内核在每次文件/设备访问时查 `capable()`（`kernel/capability.c`）——能力以「所在 user namespace」为上下文求值。
- **教学简化**：教学内核没有 `user_namespace`/`cred` 对象，但「权限判定」以 **uaccess 模型**存在：每次用户指针拷贝都做 canonical/range/VMA/permission 四重检查，CPL3 段选择子（0x2b/0x33）划定用户态边界——相当于「单一用户域、无 UID 映射」的特例。

### 2.4 教学内核中与「用户/权限」有关的既有设施

本课主题机制（user namespace / UID 映射）**未在源码中实现**，但「用户边界与权限判定」素材完整存在：

| 素材 | 源码 | 主题含义 |
|---|---|---|
| VMA 权限位 | `VMA_R/VMA_W/VMA_X`、`struct vma_model{start,end,backing; prot,kind,valid}` | Linux `vm_area_struct` 的 `vm_flags`（权限位）浓缩 |
| uaccess 元数据 | `USER_CANONICAL_MAX`、`USER_RANGE_MAX`、`USER_COPY_MAX`、`struct uaccess_result{address,length,access,canonical,range,vma,permission,copied}` | Linux 的 `access_ok()` + `get_user` 权限检查浓缩 |
| `uaccess_validate` | canonical/range/VMA 命中/`prot&VMA_R/W` 权限位 | 内核态「越权访问」拒绝 = user namespace 能力判定的教学对应 |
| CPL3 边界 | `#define USER_CS 0x33`、`#define USER_DS 0x2b`、`enter_user`/`enter_user_c` | x86 的 DPL 隔离用户态/内核态（对照 Intel SDM） |
| 用户进程 | `struct process`、`user_processes[]`、`user_threads[]` | 「用户」侧进程对象 |
| syscall 面 | `SYS_GETPID 1U` 等与 `-ENOSYS` | 用户态经 syscall 进入内核的接口 |

### 2.5 检查点模型：lesson_145_model 与 l152test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `145→148` 标记 Origin 为 Lesson 145（`a=145,b=146,c=147,d=148`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「用户域连续性」。本课同时把上一课新增的 `l151test` 恢复为历史命名 `l144test`（挂 `lesson_144_state`，计数 `144→147`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理。

### 2.6 机制继承 + 检查点增量

本课主题机制（user namespace 隔离与 UID 映射）**不是本课新写的代码**：uaccess 与权限位来自 Lesson 42/66s，CPL3 进入来自 Lesson 30s。本课实际增量只有三处：`l151test`→`l144test` 更名、`lesson_145_model`+`l152test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「用户与权限」主题重新组织，并如实说明：**user namespace 对象（`struct user_namespace` 式结构）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l151test`→`l144test` 恢复命名；新增 `lesson_145_model`/`lesson_145_state`/`l152test`；`about` 与开机横幅更新。user namespace 主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`user namespace`/`l152test`/`Lesson 152`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（用户/权限机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_145_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_145_model lesson_145_state;
static TEXT64 void l152test(u16*c){lesson_145_state=(struct lesson_145_model){145U,146U,147U,148U,1,1,1,1};int ok=lesson_145_state.valid&&lesson_145_state.active&&lesson_145_state.ready&&lesson_145_state.accounted&&lesson_145_state.b==lesson_145_state.a+1U;text64(c,"l152test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 145 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `145→148`（Origin Lesson 145），四布尔位全置 1，`b==a+1U` 校验计数连续。
2. **逻辑分析（≥3 行）**：结构体字面量一次性写入 `lesson_145_state`，`ok` 由四布尔位 + `b==a+1U` 合取；字面量全 1 使断言恒真，成功串必输出；`Lesson 145 fallback reported` 是防御性兜底，仅在模型计数被破坏时命中。
3. **输出串（逐字抄录）**：成功 `l152test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 145 fallback reported`。
4. **恢复的 `l144test`**：本课把上一课 `l151test` 更名回 `l144test`（同为 `lesson_144_state`，计数 `144→147`）；`l100test`–`l143test` 历史检查点全部保留。

#### 3.2.2 权限判定：uaccess 模型（user namespace 的教学对应）

```c
#define USER_CANONICAL_MAX 0x00007fffffffffffULL
#define USER_RANGE_MAX 0x0000000100000000ULL
#define USER_COPY_MAX 256U
#define UACCESS_READ 1U
#define UACCESS_WRITE 2U
struct uaccess_result { u64 address,length; u8 access,canonical,range,vma,permission,copied; };
static u64 uaccess_attempts,uaccess_successes,uaccess_failures,uaccess_bytes;
```

1. **常量语义**：`USER_CANONICAL_MAX` 是 47 位用户态地址上限（canonical 地址），`USER_RANGE_MAX` 限定用户拷贝区，`USER_COPY_MAX` 限定单次拷贝字节数——三者共同把「用户指针」圈定在合法范围内。
2. **结果记录**：`struct uaccess_result` 把一次检查的六项结论全部记录（canonical/range/vma/permission/copied 逐位布尔），`access` 区分读/写——这是「权限判定元数据」。
3. **统计**：`uaccess_attempts/successes/failures/bytes` 计数一次访问的成败——对照 user namespace 能力判定的「允许/拒绝」计数。

```c
static TEXT64 int uaccess_validate(u64 address,u64 length,u8 access,struct uaccess_result*r){u64 end;const struct vma_model*v;r->address=address;r->length=length;r->access=access;r->canonical=address<=USER_CANONICAL_MAX&&(length==0||address<=USER_CANONICAL_MAX-length+1);r->range=length<=USER_COPY_MAX&&address<USER_RANGE_MAX&&length<=USER_RANGE_MAX-address&&address+length>=address;r->vma=0;r->permission=0;r->copied=0;if(r->canonical&&r->range&&length){end=address+length;v=vma_lookup(address);if(v&&end<=v->end){r->vma=1;r->permission=(access==UACCESS_READ?(v->prot&VMA_R):(v->prot&VMA_W))!=0;}}else if(r->canonical&&r->range&&!length){r->vma=1;r->permission=1;}
```

1. **canonical 检查**：`address<=USER_CANONICAL_MAX` 并防 `length` 回绕（`address<=MAX-length+1`）——非 canonical 地址直接判失败，对应 Linux `access_ok` 的用户地址合法性校验。
2. **range 检查**：长度上限、地址下界与溢出防护 `address+length>=address` 一次断言——`USER_COPY_MAX` 与 `USER_RANGE_MAX` 双重夹逼。
3. **权限位判定（核心）**：命中 VMA 后，`permission=(access==UACCESS_READ?(v->prot&VMA_R):(v->prot&VMA_W))!=0`——读/写请求必须与 VMA 的 `VMA_R/VMA_W` 位匹配，否则拒绝。这正是「以权限位决定访问是否越权」的教学对应（对照 user namespace 的 `capable()` 判定）。

```c
static TEXT64 int uaccess_copy(u64 address,u64 length,u8 access){struct uaccess_result r;uaccess_attempts++;if(!uaccess_validate(address,length,access,&r)){uaccess_failures++;return 0;}r.copied=1;uaccess_successes++;uaccess_bytes+=length;return 1;}
```

1. **计数先增**：`uaccess_attempts++` 记录一次访问尝试，验证失败则 `uaccess_failures++` 返回 0。
2. **成功后记录**：`copied=1`、`successes++`、`bytes+=length`——成功路径的元数据完整。
3. **绝不 dereference**：`uaccess_copy` 只改元数据，**从不真正读写用户内存**——教学模型的安全铁律（源码注释 `no source/destination bytes touched`）。

#### 3.2.3 用户态边界：CPL3 与 enter_user

```c
#define USER_DS 0x2b
#define USER_CS 0x33
static TEXT64 void enter_user(struct long_mode_handoff*h){if(!user_process_enter(h)){return;}__asm__ volatile("cli; call enter_user_c":::"memory");}
```

1. **段选择子**：`USER_CS 0x33`（RPL=3）/`USER_DS 0x2b`——x86 DPL=3 的用户态段，与内核 `KERNEL_CS 0x08`/`KERNEL_DS 0x10` 构成特权级边界（对照 Intel SDM 段权限检查）。
2. **进入路径**：`user_process_enter(h)` 校验用户上下文合法后才 `call enter_user_c`，汇编里 `pushq $0x2b`（SS）、`pushq $0x00801000`（RSP）、`pushq $0x002`（RFLAGS）、`pushq $0x33`（CS）、`pushq $0x00400000`（RIP）然后 `iretq`——一次受控的 CPL3 跳转。
3. **教学意义**：user namespace 划定「用户世界」，CPL3 划定「用户态执行层」——教学内核的用户边界由硬件段特权级实现，而非 UID 映射。

#### 3.2.4 权限回归命令：copytest

```c
static TEXT64 void copytest(u16*c){int a=uaccess_copy(VMA_DATA_START,16,UACCESS_WRITE),b=uaccess_copy(VMA_CODE_START,16,UACCESS_WRITE),d=uaccess_copy(USER_CANONICAL_MAX-3,8,UACCESS_READ),e=uaccess_copy(VMA_DATA_START,USER_COPY_MAX+1,UACCESS_READ);text64(c,"copytest: ");text64(c,a&&!b&&!d&&!e?"copy_to_user/from_user bounded success/failure accounting passed":"BROKEN");text64(c,"\nno source/destination bytes touched; pointers were never dereferenced\n");}
```

1. **四个用例**：`a`=对可写 VMA 写（应成功）、`b`=对只读 VMA 写（应失败）、`d`=非 canonical 地址（应失败）、`e`=超长拷贝（应失败）——覆盖成功、权限拒绝、地址非法、长度越界四类。
2. **判定**：`a&&!b&&!d&&!e` 合取，成功串 `copytest: copy_to_user/from_user bounded success/failure accounting passed`。
3. **安全声明**：第二行 `no source/destination bytes touched; pointers were never dereferenced` 强调「权限判定只基于元数据」——与 user namespace「权限按元数据判定」同构。

#### 3.2.5 exec64 增量与开机横幅

- `about` 输出 `Lesson 152: user namespace\n`；检查点分支：
```c
else if(eq64(word,"l144test")){if(!noargs64(arg))usage64(c,"l144test");else l144test(c);}else if(eq64(word,"l152test")){if(!noargs64(arg))usage64(c,"l152test");else l152test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 152: user namespace\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` → `kernel64.ld` → `objcopy` → `boot.S` 嵌入 → `grub-mkrescue`）。`make check` 断言 README 含 `user namespace`、`Lesson 152`，kernel64.c 含 `l152test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pmm_init / vma_init（含 VMA 权限位）/ reclaim_init / vfs_init
 ├─ 横幅 "Lesson 152: user namespace"
 └─ 主循环：命令 → exec64
     ├─ l152test / l144test → 阶段检查点（lesson_145_state / lesson_144_state）
     ├─ copytest → uaccess 四用例（成功/拒绝/非法/越界）
     ├─ vmatest / vmfaulttest → VMA 权限与缺页分类
     ├─ cpl3test / userpitest → CPL3 用户态进入（USER_CS/USER_DS + iretq）
     └─ syscallinfo → 系统调用面（GETTICKS/GETPID/WRITE_CONSOLE/EXIT）
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：VMA 初始化完成（数据区可写、代码区可读可执行），打印横幅 `Lesson 152: user namespace`。
2. **`l152test`** → `l152test(c)` → 初始化 `lesson_145_state` → 五条件断言 → `l152test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`copytest`** → `uaccess_copy` 四连发（可写命中/只读写拒/非 canonical/超长）→ `copytest: copy_to_user/from_user bounded success/failure accounting passed` + `no source/destination bytes touched; pointers were never dereferenced`。
4. **`cpl3test`** → `enter_user` → `user_process_enter` 校验 → `enter_user_c` 的 `iretq` 序列 → 用户态 stub 依次 syscall 0/1/2/99/3（3=EXIT 主动停机）。
5. **`about`** → `Lesson 152: user namespace`。

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
Multiboot2 and Lesson 152 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 152: user namespace` 横幅 |
| `l152test` | `l152test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l144test` | `l144test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `copytest` | `copytest: copy_to_user/from_user bounded success/failure accounting passed` 及 `no source/destination bytes touched; pointers were never dereferenced` |
| `cpl3test` | `entering CPL3 syscall stub with IF=0; calls 0,1,2,99,3 (EXIT)` |
| `about` | `Lesson 152: user namespace` |

判定成功：`l152test` 输出 passed、无 fallback，`copytest` 输出 passed、无 `BROKEN`，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l152test` 输出 `Lesson 145 fallback reported` | `lesson_145_state` 初始化/断言失败（stale 镜像） | `grep -n "l152test" kernel64.c`；确认初始化串 `{145U,146U,147U,148U,1,1,1,1}` 与 `b==a+1U` |
| `copytest` 输出 `BROKEN` | 四用例中某断言反转：可写命中失败、只读写未被拒、canonical 判断错误或超长未拒 | 对照 `uaccess_validate`：`prot&VMA_R/W`、`USER_CANONICAL_MAX` 与 `USER_COPY_MAX` 常量 |
| `copytest` 第二行缺失 | 输出串拼接问题 | 确认源码第二行 `\nno source/destination bytes touched...` 完整输出 |
| `cpl3test` 不进入用户态 | `user_process_enter` 上下文校验失败 | 对照 `user_context_valid` 与 `enter_user_c` 的 `iretq` 帧（SS/RSP/RFLAGS/CS/RIP 五段） |
| `vmatest`/`vmfaulttest` 缺页分类异常 | VMA 权限位与 `pf_classify` 不匹配 | `pf_classify` 按 `v->prot&VMA_W/VMA_R` 分类为 PF_PROTECTION/PF_NOT_PRESENT/PF_UNMAPPED |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 152' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `user namespace` 与 `Lesson 152` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| VMA 权限位 `VMA_R/VMA_W/VMA_X` | `include/linux/mm_types.h` 的 `vm_area_struct.vm_flags`；`mm/mmap.c` | 模型无 `vm_flags` 全位集（无 `VM_MAY*`/`VM_SHARED` 等），仅 R/W/X 三读位 |
| `uaccess_validate` 的 canonical/range 检查 | `arch/x86/include/asm/uaccess.h` 的 `access_ok()`/`user_access_begin` | 模型无 `addr_limit` 与异常表（`exception_table`）恢复机制 |
| `permission=(access?prot&VMA_R:prot&VMA_W)` | `mm/memory.c` 的 `__access_remote_vm` 与 `fs/namei.c` 的 `inode_permission` | 模型无 inode 属主/组位（r/w/x 九位）与 ACL |
| `struct uaccess_result` 六布尔记录 | `kernel/user_namespace.c` 的 `from_kuid`/`make_kuid`；`kernel/cred.c` 的 `cred->uid/gid` | 模型无 UID/GID 与映射表，权限判定不依赖「用户身份」 |
| `copytest` 的成功/失败计数 | `kernel/capability.c` 的 `capable()`/`ns_capable()`；LTP 安全测试 | 模型无 capability 集合，只有权限位布尔 |
| `USER_CS 0x33`/`USER_DS 0x2b` + `iretq` | `arch/x86/entry/entry_64.S` 的 syscall/iret 路径；Intel SDM 段权限 | 模型无 SYSCALL/SYSRET 指令与 `swapgs`，iretq 手动构造帧 |
| `-ENOSYS` 未知命令 | `include/uapi/asm-generic/errno.h` `-ENOSYS`；`kernel/user_namespace.c` 对未写映射返回 `EINVAL` | 模型把 syscall 面当命令面，无 errno 表 |

**权威来源**：Linux `kernel/user_namespace.c`、`kernel/cred.c`、`kernel/capability.c`、`include/linux/uidgid.h`、`arch/x86/include/asm/uaccess.h` 为对照；Intel SDM 的段特权级（DPL/RPL）与 CPL 切换仍为硬件权威来源。

**如实说明**：本课**没有** `struct user_namespace`、`uid_map` 或 `capable()` 的等价实现——user namespace 是「主题宣告」，教学内核停留在「单一用户域、权限由 VMA 位与 uaccess 四重检查决定」的模型上。

---

## 8. 思考题与练习

1. **概念理解**：为什么容器内 UID 0 在宿主机只是普通用户？user namespace 的 uid_map 如何完成「容器内 0 ↔ 容器外 1000」的映射？
2. **源码定位**：在 `kernel64.c` 中找出 `uaccess_validate` 的四重检查（canonical/range/vma/permission）分别由哪些表达式实现，并说明 `USER_COPY_MAX` 的作用。
3. **动手实验**：把 `copytest` 的用例改为「非 canonical」与「合法但超 range」各一，验证 `uaccess_validate` 的 range 分支，重新构建运行。
4. **动手实验**：给 `vma_model` 增加一个 `user` 位并让 `uaccess_validate` 要求该位，模拟「内核只允许访问用户拥有页」的规则，观察 `copytest` 输出变化。
5. **Linux 对照**：阅读 `kernel/user_namespace.c` 的 `map_write`（写入 uid_map 的校验），对比教学模型「UACCESS 只读权限位、无映射表」的简化。

---

## 9. 本课小结与下一课预告

1. user namespace 是 UID/GID 与 capability 的隔离域：容器内 root 映射到容器外普通用户，权限按所在 namespace 判定。
2. Linux 用 `struct user_namespace`（uid_map/gid_map）+ `struct cred` + `capable()` 实现（`kernel/user_namespace.c`、`kernel/cred.c`、`kernel/capability.c`）。
3. 教学内核没有 namespace 对象，但「权限判定」素材完整：VMA 权限位、`uaccess_validate` 四重检查、`copytest` 四用例。
4. `uaccess_copy` 只改元数据、从不 dereference——「基于元数据的权限判定」与 user namespace 同构。
5. CPL3（`USER_CS 0x33`/`USER_DS 0x2b` + `iretq`）从硬件层划定用户态边界，`cpl3test` 验证。
6. 检查点增量：`l151test`→`l144test` 更名、新增 `lesson_145_model`+`l152test`、横幅与 `about` 更新为 `Lesson 152: user namespace`。
7. 下一课（Lesson 153）主题为 **cgroup 层级**（对照 `kernel/cgroup/cgroup.c`）：从 namespace 的「隔离」转向 cgroup 的「限制与分组」，教学内核将开启 cgroup 系列检查点。
