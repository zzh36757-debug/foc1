# F4 复用模板：STM32F407ZG

更新于 2026-10-22。完整用法见 D:/test/foc1/STM32_DEBUG_REUSE.md。

把本目录中的四个 JSON 文件和 debug 子目录复制到新工程的 .vscode/。
如果新工程已有 .vscode/，合并文件内容，避免复制成 .vscode/.vscode.f4/。
debug 子目录也要带上，不能只复制 launch.json。

默认 ELF 是 build/Debug/jixiebi1.elf。新工程名称不同，修改 launch.json 和
tasks.json 中的 ELF 路径；保留该工程自己生成的 CMake、启动文件、链接脚本和 HAL。
先核对 .ioc 的完整 MCU 型号；本模板仅按 STM32F407ZG 制作。

流程：编译 -> stlink 1.9.0 同步写入和完整读回校验 -> OpenOCD 调试。
GDB 预热与编译烧录并行；烧录 240kHz，失败后 100kHz 完整重试一次；调试 100kHz。
保留 -nx、loadFiles: []、软件复位和 Live Watch。手动运行 prepare GDB 也会烧录，
只想离线检查 ELF 时使用脚本的 -ValidateOnly；它不连接 ST-Link、不写芯片。

这是通用芯片模板：-Board Generic，不会操作原 F1 电机板的 PA3，也没有电机暂停钩子。
当前 foc1 工程的实际 .vscode/ 继续用 -Board Foc1 和 debug/motor-test.cfg，保持 DRV_EN 关闭行为。

F4 已用 jixiebi1 的实际 ELF 检查容量、初始栈和复位向量；尚未连接 F4 板做实物烧录，不能沿用 F1 的测速结论。
