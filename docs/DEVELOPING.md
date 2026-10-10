# LumenOS 开发者指南

## 1. 构建命令

```bash
python3 build.py            # 构建内核（默认目标）
python3 build.py all        # 全量构建（内核 + 用户态 + 镜像）
python3 build.py all -j4    # 4 并行编译
python3 build.py clean      # 清理 build/
python3 build.py lint       # 只跑护栏，不编译
python3 build.py smoke      # 构建 + 无头启动，验证跑到 fish shell
python3 build.py run        # 构建并在 QEMU 里启动
python3 build.py run --sm 2 # 双核启动，验证 SMP
python3 build.py run --gdb  # 停在入口等 GDB
python3 build.py run --bios # 走传统 BIOS 链而非 UEFI（不建议）
python3 build.py all --test # 额外编译 tests/ 下的探针
```

## 2. 分层

`scripts/check_layers.py` 定义层级，只能往下 include，不能往上：

```
L0  lib/                  纯算法库（字符串、链表、printf、加密）
L1  arch/                 硬件抽象（中断、页表、GDT、APIC、启动）
L2  mm/                   内存（页池、位图、访问权限）
L3  kernel/               内核核心（调度、信号、syscall、进程、GUI）
L4  drivers/ fs/ net/     设备驱动、文件系统、网络协议
L5  user/                 用户态程序
```

例外写进 `check_layers.py` 的 `ALLOW_LIST` 并说明理由。

## 3. 启动顺序

`kernel/main.c:kmain()`：

```
arch_early_init()              CPU 特性、栈保护
mb2_init() / boot_info()       解析 Multiboot2（内存图 / framebuffer / cmdline）
mm_init() / kheap_init()       物理页管理 + 内核堆
io_init()                      显存映射完成后控制台才能输出
gdt_init() / percpu_init()
tss_init() / idt_init()        此后才能收中断
syscall_init()
acpi_init() / pit_init() / apic_init()（失败回退 PIC）
drivers_init(PRE_THREAD)       早期驱动
thread_init()                  此后 current 有效
drivers_init(POST_THREAD)      后期驱动
filesys_init()                 探测并挂载根文件系统
smp_init() / net_init()
process_execute("/bin/shell.elf")
```

目前为止还不能修改顺序，之后可能可以

## 4. 加功能

### 4.1 加驱动

写 init 函数，放 `drivers/<类>/`，在 `.c` 末尾注册：

```c
DRIVER_REGISTER("mydrv", 10, mydrv_init);
```

level 小的先跑（0~19 在 `thread_init` 前，20+ 在后）。公开接口放 `include/drivers/<类>/<名字>.h`。

### 4.2 加中断处理程序

```c
IRQ_REGISTER(IRQ_IDE, my_irq_handler, "mydrv");
```

handler 签名 `void (uint8_t vector)`，收到的是中断向量号（IRQ 线号 + 32）。EOI 由架构层统一发送。行号常量在 `include/arch/x86_64/irq.h`。

### 4.3 加文件系统

全部写在驱动文件里：

```c
static const struct VFS_OPS myfs_ops = { ... }; 
static int myfs_probe_sb(const uint8_t *sb) { ... }  
VFS_REGISTER("myfs", &myfs_ops, myfs_probe_sb);
```

超级块偏移用具名常量（参考 `include/fs/ext4.h` 的 `EXT4_SB_MAGIC_OFF`）。

### 4.4 加网络协议

```c
ETH_PROTO_REGISTER(0x0800, myproto_input, "myproto");  // 以太网层
IP_PROTO_REGISTER(253,   myl4_input,    "myl4");      // IP 传输层
```

输入函数签名：`void (*)(NETIF *ifp, ..., const uint8_t *payload, uint32_t len)`。

### 4.5 加系统调用

在 `kernel/syscall/syscall.c` 写 `nsys_xxx(struct ARCH_REGS *r)`，挂进同文件的 `nsys_table[]`：

```c
static const nsys_fn nsys_table[] = {
    ...
    [SYS_MYTHING] = nsys_mything,
};
```

用户态调用约定见 `include/user/libc/syscall.h`。

## 5. 新增 .c 文件

放 `arch/ kernel/ mm/ lib/ drivers/ fs/ net/` 下不会自动编译（历史遗留问题，之后会修），必须加进 `buildsys/plan.py` 的 `kernel_c_sources`（网络在 `net_c_sources`）：

```python
("myname.o", KERNEL_DIR / "subdir" / "myname.c"),
```

## 8. 目录

| 描述 | 位置 |
| --- | --- |
| 中断/异常处理 | `arch/x86_64/irq/interrupt.c` |
| 页表 / 地址空间 | `arch/x86_64/mm/paging.c` |
| GDT / TSS / per-CPU | `arch/x86_64/cpu/` |
| PIT / APIC / ACPI | `arch/x86_64/irq/{pit,apic,acpi}/` |
| 引导（Multiboot2 / UEFI） | `arch/x86_64/boot/{mb2.c,uefi/main.c}` |
| 启动流程 | `kernel/main.c` |
| 调度器 / 上下文切换 | `kernel/sched/thread.c` |
| 内存管理 | `mm/pool/pool.c`、`mm/bitmap/` |
| 系统调用 | `kernel/syscall/syscall.c` |
| 进程 / exec / fork | `kernel/userprog/` |
| 信号 | `kernel/syscall/signal.c` |
| VFS 与文件系统 | `fs/vfs/vfs.c`、`fs/{ext2,ext4,proc}.c` |
| 网络栈 | `net/{eth,ip,tcp,udp,icmp,arp}.c` |
| 图形界面 | `kernel/gui/`、`drivers/video/framebuffer/` |
| 控制台 / 串口 | `drivers/char/serial/console/io.c` |
| 构建系统 | `buildsys/`（入口 `build.py`） |
| 链接脚本 | `arch/x86_64/linker.ld` |
| 第三方依赖 | `third_party/`（submodule） |