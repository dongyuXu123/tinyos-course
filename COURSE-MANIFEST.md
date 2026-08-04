# TinyOS unified course manifest

| Lesson | Main content | Origin |
|---:|---|---|
| 61 | Multiboot2 framebuffer 与像素绘制 | GUI-01 |
| 62 | 固定 bitmap 字体、canvas 与基本绘图 | GUI-02 |
| 63 | 键盘/鼠标输入事件队列 | GUI-03 |
| 64 | 窗口、widget 与事件分发 | GUI-04 |
| 65 | 桌面 compositor 与窗口管理器 | GUI-05 |
| 66 | 图形 shell 与系统状态面板 | GUI-06 |
| 67 | 图形桌面综合验证 | GUI-07 |
| 68 | 进程组与 session 元数据 | Lesson 61 |
| 69 | session 首领与控制终端所有权 | Lesson 62 |
| 70 | 前台进程组切换与停止组保护 | Lesson 63 |
| 71 | 进程组/调度/COW 元数据 checkpoint | Lesson 64 |
| 72 | 进程元数据 checkpoint | Lesson 65 |
| 73 | 孤儿进程组检测与安全 reparent | Lesson 66 |
| 74 | job-control 信号路由 | Lesson 67 |
| 75 | 终端 stop/continue 状态转换 | Lesson 68 |
| 76 | 调度策略元数据 | Lesson 69 |
| 77 | priority/nice 优先级状态 | Lesson 70 |
| 78 | runqueue 运行队列统计 | Lesson 71 |
| 79 | voluntary preemption 主动抢占 | Lesson 72 |
| 80 | 定时器驱动调度 | Lesson 73 |
| 81 | context switch 上下文切换元数据 | Lesson 74 |
| 82 | Copy-on-Write 基础元数据 | Lesson 75 |
| 83 | COW 写时复制缺页统计 | Lesson 76 |
| 84 | 共享页生命周期 | Lesson 77 |
| 85 | fork 内存屏障与一致性 | Lesson 78 |
| 86 | 调度公平性验证 | Lesson 79 |
| 87 | 负载均衡与进程组调度综合 checkpoint | Lesson 80 |
| 88 | VFS 层次与 mount 元数据 | Lesson 81 |
| 89 | 超级块与文件系统注册 | Lesson 82 |
| 90 | inode 生命周期与引用 | Lesson 83 |
| 91 | dentry 缓存与路径组件 | Lesson 84 |
| 92 | 路径解析与遍历边界 | Lesson 85 |
| 93 | mount namespace 元数据 | Lesson 86 |
| 94 | 文件权限与访问检查 | Lesson 87 |
| 95 | 文件打开与 file_operations | Lesson 88 |
| 96 | 文件偏移与引用计数 | Lesson 89 |
| 97 | 目录读取与固定缓冲区 | Lesson 90 |
| 98 | 字符设备注册 | Lesson 91 |
| 99 | 设备节点与 major/minor | Lesson 92 |
| 100 | 设备打开与 ioctl 元数据 | Lesson 93 |
| 101 | 块设备请求队列 | Lesson 94 |
| 102 | 设备生命周期与卸载 | Lesson 95 |
| 103 | poll 就绪队列 | Lesson 96 |
| 104 | epoll 实例与固定 watch 表 | Lesson 97 |
| 105 | epoll 边沿触发 | Lesson 98 |
| 106 | epoll 水平触发 | Lesson 99 |
| 107 | epoll wait/wake 集成 | Lesson 100 |
| 108 | 服务状态机 | Lesson 101 |
| 109 | 服务依赖拓扑 | Lesson 102 |
| 110 | 服务启动与失败回滚 | Lesson 103 |
| 111 | 守护进程生命周期 | Lesson 104 |
| 112 | VFS/设备/epoll/服务综合验证 | Lesson 105 |
| 113 | mutex 与 spinlock 竞争 | Lesson 106 |
| 114 | 原子操作与内存序 | Lesson 107 |
| 115 | 信号量与等待队列并发 | Lesson 108 |
| 116 | per-CPU 数据访问 | Lesson 109 |
| 117 | 竞态窗口与屏障 | Lesson 110 |
| 118 | SMP CPU 状态 | Lesson 111 |
| 119 | SMP 启动元数据 | Lesson 112 |
| 120 | 跨 CPU 唤醒 | Lesson 113 |
| 121 | per-CPU runqueue | Lesson 114 |
| 122 | SMP 负载均衡 | Lesson 115 |
| 123 | RCU reader 临界区 | Lesson 116 |
| 124 | RCU grace period | Lesson 117 |
| 125 | RCU callback 队列 | Lesson 118 |
| 126 | RCU 对象回收 | Lesson 119 |
| 127 | RCU 与调度集成 | Lesson 120 |
| 128 | tracing ring buffer | Lesson 121 |
| 129 | 事件过滤与采样 | Lesson 122 |
| 130 | 锁依赖图 | Lesson 123 |
| 131 | 死锁检测元数据 | Lesson 124 |
| 132 | 崩溃诊断快照 | Lesson 125 |
| 133 | 异常路径与故障分类 | Lesson 126 |
| 134 | 内存压力诊断 | Lesson 127 |
| 135 | 调度与并发综合诊断 | Lesson 128 |
| 136 | SMP/RCU 回归验证 | Lesson 129 |
| 137 | 并发、SMP、RCU、诊断综合 checkpoint | Lesson 130 |
| 138 | 网络 buffer pool | Lesson 131 |
| 139 | 网络接口与链路状态 | Lesson 132 |
| 140 | 收发队列与包记账 | Lesson 133 |
| 141 | loopback 接口 | Lesson 134 |
| 142 | IPv4 地址元数据 | Lesson 135 |
| 143 | UDP socket 状态 | Lesson 136 |
| 144 | socket 端口分配 | Lesson 137 |
| 145 | 连接状态机 | Lesson 138 |
| 146 | socket poll/epoll 集成 | Lesson 139 |
| 147 | 网络错误与超时 | Lesson 140 |
| 148 | 进程 namespace | Lesson 141 |
| 149 | mount namespace 隔离 | Lesson 142 |
| 150 | network namespace | Lesson 143 |
| 151 | PID namespace | Lesson 144 |
| 152 | user namespace | Lesson 145 |
| 153 | cgroup 层级 | Lesson 146 |
| 154 | cgroup CPU 统计 | Lesson 147 |
| 155 | cgroup 内存限制 | Lesson 148 |
| 156 | cgroup 设备策略 | Lesson 149 |
| 157 | 资源限制与回收 | Lesson 150 |
| 158 | capability 权限检查 | Lesson 151 |
| 159 | syscall 安全边界 | Lesson 152 |
| 160 | 审计事件缓冲区 | Lesson 153 |
| 161 | 安全策略决策 | Lesson 154 |
| 162 | 网络、namespace、cgroup、安全综合 checkpoint | Lesson 155 |
