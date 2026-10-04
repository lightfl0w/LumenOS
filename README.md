# LumenOS

> LumenOS 是一个 x86_64 长模式操作系统内核

## 第三方模块

`third_party/` 下以 git submodule 形式引入第三方代码，构建时从源码编译，不重新分发任何二进制产物。版权归各自上游作者所有：

- [musl](https://git.musl-libc.org/cgit/musl)：标准 C 库
- [libc-testsuite](https://git.musl-libc.org/cgit/libc-testsuite)：musl 配套 libc 行为测试
- [busybox](https://git.busybox.net/busybox)：核心用户态工具箱
- [fish](https://github.com/fish-shell/fish-shell)：交互式 shell
- [pcre2](https://github.com/PCRE2Project/pcre2)：正则表达式库

## 构建与运行

### 工具链

- `nasm`：汇编
- `zig cc`（或 gcc/clang 交叉）：C 编译（freestanding）
- `ld.lld` / `ld`：链接
- `python3`：构建系统与镜像脚本
- `qemu-system-x86_64`：运行验证

### 构建

```bash
python3 build.py            # 构建内核
python3 build.py run        # 构建并在 QEMU 中启动
python3 build.py run --sm 2 # 使用多核
python3 build.py run --gdb  # 使用GDB
python3 build.py clean # 清理构建内容
```

### 运行

```bash
python3 ./build.py run
```

## 参考资料与致谢

- **OSDev Wiki** <https://wiki.osdev.org>：引导、GDT/IDT、APIC、长模式、Multiboot2 等。
- **musl libc** <https://musl.libc.org>：系统调用 ABI 兼容目标与 libc 测试套件。