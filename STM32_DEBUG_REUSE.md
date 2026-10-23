# STM32 F1 / F4 下载调试复用说明

更新：2026-10-22。以后修改或复用下载配置，先读本文件，再读 `debug/README.md` 的故障记录。

## 两套模板分开保存

| 用途 | 目录 | 模板对应芯片 | 默认 ELF |
| --- | --- | --- | --- |
| 当前电机板实际配置 | `.vscode/` + `debug/` | STM32F103C8T6 | `build/Debug/foc1.elf` |
| F1 通用复用模板 | `.vscode.f1/` | STM32F103C8T6 | `build/Debug/foc1.elf` |
| F4 通用复用模板 | `.vscode.f4/` | STM32F407ZGT6 | `build/Debug/jixiebi1.elf` |

原 `D:/test/jixiebi1/.vscodef_4/` 已同步为与 `.vscode.f4/` 相同的新版模板。
该目录是复用模板，没有为旧 F4 工程创建或激活 `.vscode/`，也没有给当前 F1 板下载 F4 固件。

F4 模板按原 `jixiebi1.ioc` 和链接脚本中的 **STM32F407ZG、1MB Flash** 制作。
F407ZE、F407VE、F411、F103CB 等其他型号需要另行核对容量和参数，不能只按“F1/F4”直接套用。

## 两个系列沿用的共同方案

1. 用 CMake 的 Debug 预设编译当前工程。
2. 用 stlink 1.9.0 的同步 RAM loader 写入映像覆盖的 Flash 页/扇区，并完整读回比较。
   库根据识别到的芯片选择 F1 或 F4 的写入算法，不把 F1 Flash 寄存器套到 F4 上。
3. 默认烧录 240kHz；任一步失败，释放连接后以 100kHz 完整重写、完整校验一次。
   低速重试仍失败则任务报错，禁止启动后面的调试。
4. GDB 预热 `warm GDB` 与上述编译、烧录并行；`prepare GDB` 等两条任务都成功后才结束。
5. OpenOCD 以 100kHz 连接调试，保留 Live Watch，软件复位后停在 `main()`。

保留两项启动配置中的 `debuggerArgs: ["-nx"]`、`preLaunchTask: "prepare GDB"`、
`loadFiles: []`、`reset_config none` 和 `cortex_m reset_config sysresetreq`。
`loadFiles: []` 避免让 OpenOCD 再下载一遍；符号仍从 ELF 加载。
不要恢复有 Windows 反斜杠转义问题的绝对路径 `monitor verify_image`，校验已在烧录任务中完成。
不要把约两分钟的 `-work-area-size 0` 历史应急配置设回默认。

**运行 `prepare GDB` 或 `flash ST-Link` 都会真实烧录。** `warm GDB` 只输出 GDB 版本。
需要只检查 ELF 时用脚本的 `-ValidateOnly`，此选项不连接 ST-Link、不写入芯片。
只验证目标连接可使用 `-ConnectOnly`：会连接并暂停 MCU，Foc1 板会清除 DRV_EN，
但不写 Flash。目标连接和有效 CPUID 检查失败时应报告连接问题，不应误报型号不匹配。

## 各系列必须区分的参数

| 项目 | F1：STM32F103C8 | F4：STM32F407ZG |
| --- | --- | --- |
| OpenOCD target | `target/stm32f1x.cfg` | `target/stm32f4x.cfg` |
| 烧录脚本 `-Target` | `STM32F103C8` | `STM32F407ZG` |
| Flash 上限 | 64 KiB | 1024 KiB |
| 允许的初始栈区域 | 20 KiB SRAM | 128 KiB SRAM 或 64 KiB CCM RAM |
| 编译器/补全 CPU 参数 | Cortex-M3，Thumb | Cortex-M4，Thumb，FPv4-SP-D16，hard float |
| CMSIS 型号宏 | `STM32F103xB` | `STM32F407xx` |

这些 CPU 参数用于模板的代码补全；实际编译仍以该工程自己的 CMake 工具链为准。
F4 地址和容量已核对原链接脚本、[ST 数据手册](https://www.st.com/resource/en/datasheet/stm32f407zg.pdf)
及 stlink 1.9.0 的 `F4x5_F4x7.chip` 定义。
复用时也要保留各自的 `.ioc`、启动文件、链接脚本和 HAL，不要从另一个系列覆盖这些文件。

## 当前电机板与通用模板的区别

当前 `.vscode/tasks.json` 明确指定 `-Target STM32F103C8 -Board Foc1`，
并在 OpenOCD 中加载 `debug/motor-test.cfg`。这两处会在烧录/暂停时清除本板 PA3/DRV_EN。

两套通用模板指定 `-Board Generic`，不加载电机暂停钩子，也不操作 PA3。
F4 使用 STM32F4 自己的 GPIO 寄存器布局；不能复制 F1 的 `0x40010814` 操作过去。
脚本会拒绝 `-Target STM32F407ZG -Board Foc1` 的组合。
为保持现有手动命令兼容，未传 `-Board` 时，F103C8 默认 Foc1，F407ZG 默认 Generic；
复用模板均显式传参，不依赖这个默认值。

## 给下一个工程使用

1. 看新工程 `.ioc` 的完整 MCU 型号，选相应模板。
2. 将模板目录里的 `launch.json`、`tasks.json`、`settings.json`、`c_cpp_properties.json`
   和整个 `debug/` 子目录复制到新工程 `.vscode/`。已有配置先备份并合并；
   最终应为 `.vscode/debug/flash-stlink.ps1`，避免多套一层模板目录。
3. 如果 ELF 名称不同，修改 `launch.json` 和 `tasks.json` 中 `build/Debug/*.elf` 的名称。
   保证工程具有 `configurePresets.Debug` 和 `buildPresets.Debug`。
4. 工具默认使用本电脑的 EIDE GCC/OpenOCD、用户目录下的 CMake/Ninja/stlink 1.9.0。
   换电脑时核对这些路径；工具安装说明见 `debug/README.md`。
5. 先编译，再按 F5。首次在另一块板上使用，检查终端的 `STLINK_FLASH_VERIFIED`，
   确认已校验并停在 main，再继续运行应用。

离线检查 F4 ELF 示例（不连接下载器）：

```powershell
powershell.exe -NoProfile -File .vscode/debug/flash-stlink.ps1 `
  -Target STM32F407ZG -Board Generic `
  -ElfPath build/Debug/jixiebi1.elf -ValidateOnly
```

## 已验证范围

- F1 原方案 240kHz 四次写入与完整校验为 6.48、6.40、6.68、6.63 秒；
  100kHz 回退重试和 OpenOCD 独立全映像读回、双 GDB 变量读取均通过。
- 本次增加 F1/F4 参数分离后，另外验证实际 ELF 的容量、栈、复位向量和错误组合拒绝。
- 本次 F1 CMake 配置、编译及 GDB 预热通过；用户确认板子已断电/拔掉，
  因此没有完成参数分离后的新增实物回归。旧的 F1 实测记录仍有效，但不是这次的新测试。
- F4 使用原 `jixiebi1` 的实际 ELF（80140 字节）完成离线检查。
  **尚未连接 F4 实物验证烧录，F1 耗时不能当作 F4 的测速结果。**
- 修改记录和原始故障处理见 `debug/README.md`；备份与本次验证资料保存在
  `D:/PCB/stm32-debug-templates-20261022/`。

下载支持脚本的维护源为 `debug/flash-stlink.ps1`。修改后同步 `.vscode.f1/debug/`、
`.vscode.f4/debug/` 和原 `.vscodef_4/debug/` 中的同名文件，保持实现一致。
