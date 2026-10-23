# VS Code 下载调试故障记录

## 2026-10-22：连接失败被误报为型号不匹配

用户 16:50/16:51 的终端先出现 `Can not connect to target`，再出现
`Connected MCU does not match STM32F103C8 (64 KiB)`。
上游 `stlink_open_usb` 可以在 `stlink_target_connect` 失败后仍返回 USB 句柄，
旧脚本只检查句柄和寄存器读取返回码，因而把不可靠的 ID 数据当作型号判断。
这不能证明芯片换成 F4，也不能证明实际 Flash 容量有误。

本次修正：显式检查 `stlink_target_connect` 返回值，验证 CPUID 中 ARM 标识及架构字段，
再读取、输出设备 ID 和 Flash 容量。无效数据报连接/读取失败；只有有效识别与预设不同，
才显示实际 ID、实际容量和预期值。检查失败时不会写 Flash。修正已同步到 F1/F4 三份模板。

增加 `-ConnectOnly`：验证芯片并暂停 CPU，当前 Foc1 板清除 DRV_EN，**不写 Flash**；
这会访问硬件并改变运行状态，与完全不访问硬件的 `-ValidateOnly` 不同。

17:00 的 stlink 只读测试先读到异常 CPUID `0x00000031`、IDCODE `0x00000010`，
后续读取失败；17:01 OpenOCD 100kHz 独立连接同样报 `unable to connect to the target`。
SWD 会话重新进入未恢复。当前证据说明连接尚未建立，底层原因未确定，
不能声称修正报错信息就修好了实际连接。等待用户确认当前上电和连接状态。
本轮未写入 Flash；资料在 `D:/PCB/stlink-connect-20261022/`。

上游依据：[USB 打开流程](https://github.com/stlink-org/stlink/blob/v1.9.0/src/stlink-lib/usb.c)、
[目标连接与 CPUID 判断](https://github.com/stlink-org/stlink/blob/v1.9.0/src/stlink-lib/common_legacy.c)。

F1/F4 的分开复用方法见 [STM32_DEBUG_REUSE.md](../STM32_DEBUG_REUSE.md)。
`.vscode.f1/`、`.vscode.f4/` 和原 F4 `.vscodef_4/` 已在 2026-10-22 同步本次方案。
当前工程仍是 F103C8 电机板配置，F4 模板尚未做 F4 实物烧录验证。
本次参数分离后 F1 编译和离线检查通过；实物回归连接时用户确认板子已断电/拔掉，
按用户要求停止硬件访问。下方的烧录测速和完整读回记录是此前完成的实物测试。

## 当前下载设置（2026-10-22）

当前默认改为 **stlink 1.9.0 同步写入并校验，OpenOCD 负责调试与 Live Watch**。
使用原来的三根 SWCLK/SWDIO/GND 线，默认烧录 **240kHz**，OpenOCD 调试保持 **100kHz**。
当前固件 30508 字节在 240kHz 下四次实物写入与完整校验通过，耗时分别为
6.48、6.40、6.68、6.63 秒；此前 100kHz 为约 13 秒，不再默认使用两分钟的逐半字写入。
480kHz 两次直接测试均失败，因此未选用；失败后自动退回 100kHz 的完整重烧也已实测通过。

按 F5 的完整流程：

1. `prepare GDB` 同时启动两条任务：`configure Debug` → `build` → `flash ST-Link`，
   以及只输出 GDB 版本的 `warm GDB`。二者都完成后才允许启动调试器。
2. `flash ST-Link` 调用 `debug/flash-stlink.ps1`，转换 BIN、复位并暂停 MCU、写入映像覆盖的页、
   逐字节读回校验，然后再次通过软件复位保持 CPU 暂停，确认 PA3/DRV_EN 为 0。
   默认 240kHz；遇到连接、写入或校验错误会释放连接，改用 100kHz 重做完整写入及校验。
   100kHz 仍失败时任务报错，禁止继续进入调试。
3. **手动运行 `prepare GDB` 也会重新烧录**，不能把它当成纯离线检查；
   需要纯离线预热时运行 `warm GDB`。
4. OpenOCD 建立调试连接，GDB 软件复位后运行至 `main()`；用户继续运行后才启动固件。

GDB 预热实测仍约 7.22 秒；它不访问 ST-Link，因此与编译、烧录并行。
按 `tasks.json` 实际依赖执行，无源码改动时完整前置任务实测 7.77 秒，
其中烧录子进程约 7.40 秒（含 PowerShell 启动）、写入与校验约 6.63 秒。
这不是从按 F5 到调试就绪的完整 UI 耗时，后面还需启动 OpenOCD 和调试连接。
保留 `debuggerArgs: ["-nx"]`、`preLaunchTask: "prepare GDB"` 和原 PA3 暂停停机钩子。
两个启动项都使用当前流程，诊断项额外显示详细 OpenOCD/GDB 日志。

`launch.json` 中的重要设置：

```json
"loadFiles": [],
"openOCDLaunchCommands": [
    "adapter speed 100",
    "reset_config none",
    "cortex_m reset_config sysresetreq"
],
"preLaunchCommands": ["set remotetimeout 15", "monitor reset halt"]
```

`loadFiles: []` 只关闭 OpenOCD 再次执行 `target-download`，ELF 符号仍正常加载。
真实写入和校验由前置任务执行，任何写入/校验错误都会中止任务和调试启动。
没有修改电机固件、链接脚本或 Flash 保护位，也没有增加硬件接线。

`flash-stlink.ps1` 使用上游 `libstlink.dll` 的 `stlink_write_flash`，复用同步 RAM loader、
按映像范围擦页和完整读回校验，不自行实现 Flash 算法。
上游普通 `st-flash` 会在写完后直接运行应用；本板脚本跳过这个运行步骤，
并执行 `RESET_SOFT_AND_HALT`，防止烧录与调试器接管之间电机自行启动。

### 工具安装与复用

当前已安装 [stlink 1.9.0 Windows x64 发布包](https://github.com/stlink-org/stlink/releases/tag/v1.9.0)，
解压位置为 `%USERPROFILE%/tools/stlink-1.9.0/`，其中应包含：

```text
stlink-1.9.0-x86_64-w64-mingw32/bin/libstlink.dll
stlink-1.9.0-x86_64-w64-mingw32/share/stlink/config/chips/
```

下载资产名为 `stlink-1.9.0-x86_64-w64-mingw32.zip`，本次包的 SHA256：
`655d952a523a5d7730a3309920c320b23c8a7770fda55065764b55028a465d0c`。
脚本默认从用户目录寻找此工具和 EIDE 的 `arm-none-eabi-objcopy.exe`，
当前任务显式传入 `-Target STM32F103C8 -Board Foc1 -ElfPath build/Debug/foc1.elf`；
通用 F1/F4 模板使用 `-Board Generic`。脚本按具体芯片验证容量及初始栈，
F4 的识别、Flash-size 寄存器和 CCM RAM 范围单独配置，不执行 F1 的 PA3 寄存器操作。
`-ValidateOnly` 只转换并检查 ELF，不连接下载器。
也接受 `-StlinkBin` / `-ObjcopyPath` 参数。`-FrequencyKhz 100` 可强制原来的低速方式；
默认是 240，且失败后只自动重试一次 100，不会跳过校验。
前置任务使用 Windows 自带的 64 位 PowerShell，
不依赖 Python、宿主 GCC 或 VS Code 外的交互窗口。BIN 在忽略的 build 目录生成。

### 本次验证与先前方案的区别

- 原 100kHz 三次真实完整写入和校验均通过；随后 240kHz 四次均通过。
  全部使用前置任务实际调用的 Windows PowerShell 5.1，且按当前 `tasks.json`
  的真实并行依赖关系执行了配置、编译、烧录及 GDB 预热。
- 自动降速重试经过实物测试：480kHz 写入失败后，脚本自动用 100kHz 重新擦除映像页、
  写入并完整读回校验，最终 CPU 暂停、DRV_EN=0。没有把第一次失败当作成功。
- 烧录后使用 GDB MI 协议连接 OpenOCD，运行至 main，另一条同时连接的 GDB MI 会话
  成功读取 Live Watch 变量；最后确认 CPU 暂停且 PA3=0，并释放测试连接。
- 240kHz 写入后，另外通过 OpenOCD/GDB 在 100kHz 下完整读回全部 30508 字节，
  与构建的 BIN 逐字节一致；随后停在 main、双 GDB 读取变量均通过。
  独立测试服务器需和 Cortex-Debug 的 helpers 一样设置 `-gdb-max-connections 2`，
  否则第二条连接会被默认的单连接上限拒绝；这与烧录或速度无关。
- 本次此前弹窗 `Protocol error with Rcmd: 4E` 的日志实际是
  `couldn't open D:\test` 加换行/转义后的 `oc1/build/Debug/foc1.elf`。
  原因是 `postLaunchCommands` 中 Windows 绝对路径在命令传递中被转义。
  手工测试使用正斜杠路径，因而没有覆盖这一处 VS Code 变量展开差异。
  当前删除了该监控校验命令，校验已由 stlink 库在调试连接前完成。
- 之前的逐半字方式确实两次写入校验通过，但需要约 116 秒，不再作为日常配置。
  `launch-before-fast-flash.json` 保留了该应急方案；若单独使用其 `verify_image`，
  采用项目工作目录下的相对路径 `build/Debug/foc1.elf`。

相关证据和脚本在 `D:/PCB/flash-diagnosis-20261022/`。
连续成功验证了当前替代烧录路径；原 OpenOCD 异步访问错误的唯一底层原因尚未锁定。
后面的 HLA、50kHz、增加日志和逐半字记录均属历史排查，不应覆盖当前配置。
参考：[stlink 官方开源项目](https://github.com/stlink-org/stlink)、
[写入和完整校验的上游实现](https://github.com/stlink-org/stlink/blob/v1.9.0/src/stlink-lib/common_flash.c)、
[OpenOCD ST-Link 原生 DAP 接口](https://www.openocd.org/doc/html/Debug-Adapter-Configuration.html)。

## 2026-10-20：GDB 启动超时，调整后用户确认恢复

适用工程：`D:/test/foc1`，STM32F103C8T6，CMake + Cortex-Debug + ST-Link。
当时使用 Cortex-Debug 1.12.1、Arm GNU Toolchain 15.2.Rel1 中的 GDB
16.3.90.20250906-git，以及 EIDE 的 OpenOCD 0.12.0-rc2。

### 报错及定位证据

弹窗为：

```text
Failed to generate gdb commands:
Error: Could not start gdb, no response from gdb
```

- 已检查当前安装的 Cortex-Debug：它在启动阶段发送 `gdb-version`，等待 5 秒，
  收不到响应就报这个错误；不是在等待电机动作或 AS5600 角度。
- 在电脑上用 Node 子进程、与插件相同的 MI2 方式启动 GDB，一次版本响应耗时
  约 7.227 秒，超过上述等待时间。其他复测又只有几十毫秒，说明慢启动是间歇性的。
- 加 `-nx` 的测试中，版本响应约 39～46 毫秒；普通方式在后续复测中也能很快响应。
  因而不能认定某个初始化文件就是唯一原因，也没有证据确认是杀毒软件或其他系统服务造成。
- 用户在下面的两项调整完成后回复“现在可以了”。这是一次恢复确认，
  不表示以后不会复发，也不表示所有 Flash 写入故障都已解决。

### 已保留的调整

两个启动项都在 [.vscode/launch.json](../.vscode/launch.json) 中加入：

```json
"debuggerArgs": ["-nx"],
"preLaunchTask": "prepare GDB"
```

`-nx` 跳过自动初始化文件。Cortex-Debug 后续显式加载的 `gdbsupport.init`、
`gdb-swo.init` 仍会执行；已通过离线检查验证。

[.vscode/tasks.json](../.vscode/tasks.json) 中新增 `prepare GDB`，依赖原来的 `build`：

```json
{
    "label": "prepare GDB",
    "type": "process",
    "command": "C:/Users/30952/.eide/tools/gcc_arm/bin/arm-none-eabi-gdb.exe",
    "args": ["-nx", "-q", "--batch", "-ex", "show version"],
    "dependsOn": "build",
    "dependsOrder": "sequence",
    "options": { "cwd": "${workspaceFolder}" },
    "problemMatcher": []
}
```

按 F5 后的流程为：配置 CMake → 编译 → 离线启动 GDB 检查并退出 →
正式启动 GDB/OpenOCD → 连接、下载 → 停在 main。再按 F5 继续运行。
准备任务不连接板子，只输出 GDB 版本，作用是预先加载并检查电脑端的调试器。
它不会延长插件内部的 5 秒等待时间，也不能保证下一次启动一定不会超时。

- 平时选 `STM32F103C8 - Live Watch (ST-Link)`。
- 出问题时选 `STM32F103C8 - 下载诊断（详细日志）`；额外启用 OpenOCD `-d3`、
  GDB 原始输出和时间戳。两项使用同一份 ELF；最初 SWD 均为 100kHz。
  当日后续出现 SWD AP 访问错误后，诊断项单独改为 50kHz 作对比，普通项仍为 100kHz。
  50kHz 下仍有间歇性故障，随后按用户要求将诊断项恢复为 100kHz；当前两项均为 100kHz。
- 本次调整只涉及 VS Code 配置，验证时没有自动连接、复位或烧录板子。
- 离线准备任务验证通过；显式调试脚本、ELF 符号及观察变量的离线加载检查约耗时 0.078 秒。

### 再次发生时如何检查

1. 先核对两个启动项仍有 `debuggerArgs: ["-nx"]`、`preLaunchTask: "prepare GDB"`，
   且 `prepare GDB` 任务存在并依赖 `build`。工具路径更换时同步修改任务和启动项。
2. 看 `prepare GDB` 是否正常输出版本并退出，再检查本次调试控制台的第一条错误。
   同时保留本次 gdb-server 输出，避免混用之前成功或退出的日志。
3. 按失败阶段处理，不根据最后几行暂停、断开信息判断是否下载成功：

| 提示 | 含义与下一步 |
| --- | --- |
| `Could not start gdb, no response from gdb` | 电脑端 GDB 启动未及时响应，先检查上述准备流程和调试控制台。 |
| `error writing to flash ...` / `target-download` | 已进入 Flash 下载阶段；与 GDB 启动超时分开排查，检查当次完整日志。 |
| `ECONNRESET` | 调试连接被断开；单凭这一行不能确定原因，查看其前面的错误和调试控制台。 |
| `SIGINT`、停在 `HAL_Delay(10)` | 通常是调试器暂停程序，本身不表示程序崩溃，也不能证明本次下载成功。 |
| `detached`、`exit-code: 0` | GDB 正常退出；不等于每次下载或程序运行都已验证正常。 |

本次 SWD 已为 100kHz，不能仅凭上述 GDB 启动弹窗继续归因于“下载速度太快”。
当时用户实测板上有 3.3V，而 ST-Link 日志曾显示约 1.0～1.45V；下载器型号与参考电压
接法仍未确认。这是另一个待核对问题，不能直接把 ST-Link 的读数当成 MCU 实际供电。

## 2026-10-20 后续：SWD AP 访问失败导致下载中止

用户随后遇到 `OpenOCD: GDB Server Quit Unexpectedly`。读取插件自动保存的
最新完整日志，发现这次 GDB 已连接、器件识别正常，待写入固件为 25224 字节。
日志中 Flash 擦除流程结束并返回 OK，写入阶段出现以下顺序：

```text
target_write_buffer(): writing buffer of 446 byte at 0x20000044
adapter_write_memory(): adapter_write_memory 0x20000044 4 111
stlink_usb_error_check(): STLINK_SWD_AP_ERROR
target_write_u32(): address: 0x2000003c, value: 0x00000000
flash_driver_write(): error writing to flash at address 0x08000000 at offset 0x00000000
```

故障首先发生在向 RAM 烧录缓冲区传输数据的 SWD 访问中，OpenOCD 随后中止烧录。
这与此前电脑端 `gdb-version` 5 秒启动超时属于不同阶段。
当前证据不能确定是 SWD 接线/接触、下载器、目标端电气状态，还是下载工具的访问问题；
不能仅凭后面的通用 Flash 错误认定芯片 Flash 损坏或被写保护。
寄存器转储中的 `r7=0x14` 在该加载算法中是检查错误位的掩码，不是独立读出的 FLASH_SR。

此次只调整诊断启动项：`adapter speed 100` → `adapter speed 50`。
日志列出的 ST-Link 支持速度包含 50kHz；普通启动项继续保留 100kHz。
保留 `-nx` 和 `prepare GDB`，不修改复位方式、Flash 保护位或电机固件。
配置与参数已离线检查。用户随后重试，50kHz 下仍有失败，降速没有稳定解决问题。
实际自动日志记录了以下两次不同结果（北京时间）：

- 17:56:05 开始的会话：日志确认 `clock speed 50 kHz`，随后
  `STLINK_SWD_AP_ERROR`、读取 `0x20000040` 失败、`failed to get read pointer`，
  最后 Flash 写入失败。记录保存在
  `D:/PCB/flash-diagnosis-20261020/latest-swd50-session.log`。
- 17:56:48 开始的会话：仍为 50kHz，日志显示
  `wrote 25224 bytes from vFlash image to flash` 并返回 OK；随后建立第二条 GDB
  连接，Live Watch 持续读取内存。本次未出现 `STLINK_SWD_AP_ERROR` 或 Flash 写入错误。
  快照保存在 `D:/PCB/flash-diagnosis-20261020/swd50-followup-session.log`。
  这是写入成功和调试连接的证据，本次没有另行执行整片 Flash 校验。

这说明故障是间歇性的，不能把一次成功当作稳定修复。
最新成功会话的 ST-Link 参考电压仍约 1.45V，失败会话约 1.12V，
而用户已实测板上有 3.3V；不能单凭这些读数确定故障原因。
下一步核对下载器本体、接口标识及 SWD 实际接线，再决定后续操作。

用户随后明确要求恢复 100kHz，已将诊断项的 `adapter speed 50` 改回
`adapter speed 100`，当前两个启动项都为 100kHz。`-nx`、`prepare GDB`
和诊断日志设置继续保留；此次只修改配置和记录，没有操作板子。

### 如何直接读取自动日志

当前 Cortex-Debug 1.12.1 即使没有配置 `dbgServerLogfile`，也会将 gdb-server 终端
写入 `%TEMP%/gdb-server-console-<扩展进程PID>.log`。
以后优先按修改时间找到最新文件，再按带时间戳的 `gdb-server session connected`
分隔，读取最近一次会话，不必总让用户手动复制终端。
这次读取的是 `C:/Users/30952/AppData/Local/Temp/gdb-server-console-50900.log`；
后续 VS Code 会话的 PID 和文件名可能变化。
最新失败会话已另存为 `D:/PCB/flash-diagnosis-20261020/latest-failed-session.log`。

## 2026-10-22：100kHz 下再次发生 Flash 写入失败

读取当前自动日志最近一次会话（北京时间 13:53:58～13:54:03），记录为：

```text
Info : clock speed 100 kHz
Info : Target voltage: 1.182799
Info : device id = 0x20036410
Info : flash size = 64 KiB
Error: error writing to flash at address 0x08000000 at offset 0x00000000
```

同日 13:39:25 的另一会话还出现 `jtag status contains invalid mode value - communication failure`。
这是通过 `hla_swd` 连接时的 OpenOCD 通信错误文本，不代表当前使用了 JTAG 接线。
当前日志确认已连接并识别芯片，失败发生在 Flash 写入阶段；普通日志没有给出更细的
写入失败原因，不能将 10 月 20 日的 SWD AP 错误直接当作这次已确认的底层错误。

用户再次确认板上电压正常。ST-Link 的上述电压检测值与板上实测值需分开对待；
下载器型号和参考电压引脚接法尚未确认，不能据此认定 MCU 只有 1.18V，
也不能在未知引脚功能时建议直接并接下载器的 3.3V 输出。

最近这次启动项是普通 Live Watch，所以本次为普通项也加入 `serverArgs: ["-d3"]`，
便于下一次由用户 F5 重试时自动保存底层访问错误。两项继续保持 100kHz、`-nx`
和 `prepare GDB`。这项修改增加日志细节，尚未证明解决烧录问题，没有自动连接或操作板子。
本次日志快照和修改前启动配置保存在 `D:/PCB/flash-diagnosis-20261022/`。

## 记录与复用

修改前配置和离线验证记录：`D:/PCB/gdb-startup-fix-20261020/`。
启动测量脚本、历史下载诊断：`D:/PCB/flash-diagnosis-20261020/`。
这些电脑本地文件用于追溯，工程的正常运行不依赖它们。

复用 `.vscode.f1/` 或其他工程的旧配置时，要同时核对并带上本记录中的启动参数和
准备任务；当前复用模板尚未同步这次调整，不要直接覆盖已修复的 `.vscode/`。

参考：[GNU GDB 的 `-nx` 参数](https://www.sourceware.org/gdb/current/onlinedocs/gdb.html/Mode-Options.html)。
