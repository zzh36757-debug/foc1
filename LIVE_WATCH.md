# STM32F103C8T6 首次运行检查

用 VS Code 打开工程文件夹 `D:/test/foc1`。

## 运行

1. 接好板子电源和 ST-Link 的 USB、SWD 连线。
2. 按 F5，选择 `STM32F103C8 - Live Watch (ST-Link)`。VS Code 会先用 CMake 的 Debug preset 编译，再下载程序。
3. 首次会停在 `main()`，再按一次 F5 继续运行。
4. 在“运行和调试”侧栏展开 **CORTEX LIVE WATCH**，点 `+`，逐个添加下面的变量。需要保持程序运行，才能看到数字变化。

| 变量 | 正常现象 |
| --- | --- |
| `live_counter` | 大约每 100 毫秒加 1 |
| `live_tick_ms` | 开机后的毫秒计数，持续增加 |
| `live_core_clock_hz` | 初始化完成后为 `72000000`，是软件计算的内核时钟值 |
| `live_error` | 保持 `0`；若进入 HAL 初始化的 Error_Handler 则为 `1` |

Live Watch 每秒采样 4 次，所以 `live_counter` 画面上可能一次跳过 2～3 个数字，这是正常的。用 **CORTEX LIVE WATCH** 面板添加变量；普通 WATCH 主要在暂停时刷新。

## 本次测试程序

`Core/Src/main.c` 保留约每 100ms 更新的心跳计数。**当前入口为[AS5600 手转测角度](AS5600_ANGLE_TEST.md)：PA3/DRV_EN 保持低，不启动原来的开环试转。** 编码器约每 10ms 在主循环读取一次；按新说明添加 `live_as5600_*` 变量。旧电流、PWM 模块继续保留，但本阶段不启动其采样和控制流程。

计数正常变化，说明下载、主循环和 SysTick 已经能够运行；电机驱动、电流采样、编码器还要分别验证。

## 常见情况

- OpenOCD 提示 `Error: open failed`：下载器没有被打开，先确认电脑能够识别 ST-Link、USB 数据线和驱动。
- 停在 `main()`，数字不变：按 F5 继续运行。
- 已运行但 `live_error` 为 `1`：暂停后查看调用栈，定位失败的初始化步骤。
- `Ctrl+Shift+B`：只编译；F5：编译并启动下载调试。

配置使用这台电脑已有的 GCC、GDB、OpenOCD。调试目标为 `target/stm32f1x.cfg`，可执行文件为 `build/Debug/foc1.elf`，无需沿用 F407 的 SVD 文件。

SWD 速度设置为 **100kHz**，与已有 `D:/test/jixiebi1/.vscode/launch.json` 中的设置相同。首次使用 1000kHz 连接失败，降到 100kHz 后连接成功。

## 2026-10-17 板上验证记录

- OpenOCD 识别到 Cortex-M3、器件 ID `0x20036410` 和 64KiB Flash。
- 通过 OpenOCD 完成 `foc1.elf` 下载、校验和复位，结果为 `Verified OK`。
- 在 CPU 持续运行时，两次读取间隔约一秒：`live_counter` 从 `251` 增加到 `261`，`live_tick_ms` 从 `25352` 增加到 `26362`。
- `live_core_clock_hz` 为 `72000000`，`live_error` 为 `0`。
- GPIOA ODR 的 PA3 位为 `0`，TIM2 CR1 的 CEN 位为 `0`，电机驱动保持关闭且 PWM 未启动。

以上是通过 OpenOCD 在板上读取的结果；VS Code 的 Live Watch 面板仍按上面的步骤添加观察变量。

用户实测板上电源为 3.29V。当前下载器只接 CLK、DIO、GND，已完成上述连接和烧录；其报告的目标参考电压仍约为 1V，和实测值不一致。下载器具体型号及参考电压接口尚未确认，不能仅凭工程配置判断某个标为 3.3V 的引脚是输入还是输出。

原始 Flash 备份和联调日志保存在 `D:/PCB/bringup-foc1-20261017/`。

参考：[Cortex-Debug 官方 Live Watch 说明](https://github.com/Marus/cortex-debug/issues/810)。
