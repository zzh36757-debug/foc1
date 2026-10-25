# STM32F103C8T6 首次运行检查

用 VS Code 打开工程文件夹 `D:/test/foc1`。

## 运行

1. 接好板子电源和 ST-Link 的 USB、SWD 连线。
2. 按 F5，选择 `STM32F103C8 - Live Watch (ST-Link)`。VS Code 会先用 CMake 的 Debug preset 编译，再执行 `prepare GDB` 准备任务，随后启动下载调试。
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

当前入口见 [电流控制说明](CURRENT_CONTROL.md)，约1秒定位/单向识别后进入状态9。
本轮默认current_mode=0：固定Iq=0.10A、Id目标0，电流P/I仍0.20/100；
电流PI随2kHz同步采样更新PWM，duration=0持续运行，状态9/故障0/current_test_done=0。
不控制转速；速度PI不参与，编码器/测速仍用于换相。重新下载并运行以清掉旧积分和测试状态。

优先添加：

- `current_test_iq_a`、`current_test_elapsed_ms`、`current_test_done`
- `current_test_iq_error_max_a`、`current_test_id_abs_max_a`
- `speed_target_rpm`、`speed_command_rpm`、`speed_measured_rpm`
- `current_iq_target_a`、`current_iq_a`、`current_id_a`
- `current_ud_volts`、`current_uq_volts`、`current_voltage_limited`
- `live_adc_sample_count`、`current_control_period_us`、`current_control_max_us`
- `current_alignment_stage`、`motor_test_state`、`motor_test_fault`

目标Iq默认限幅1A，总电压预算3.8V。电流PI与速度PI初值均待实物整定。
`current_mode=0`时使用`current_test_iq_a=0.10A`持续固定电流运行，不保持指定转速；
`current_mode=1`为速度+电流双环。停机用`speed_run=0`，不会自动重启。
`current_test_duration_ms=0`表示取消按时长结束；非0才会到时正常停机并置done=1。
结束后current_id_a/iq_a保留停止前最后一组控制反馈，current_iq_target_a和输出电压指令清零。
误差峰值略过前100ms建立期，之后按每次ADC反馈统计并保留；停机后的Id/Iq不是此刻的实时电流。

旧`speed_uq_volts`现在显示电流PI限幅后Uq，`speed_requested_uq_volts`显示限幅前Uq。
ADC原始值和相电流现为一组采样，已不再是每相32点平均。
若故障9，看`current_alignment_stage`及`current_alignment_along_a/across_a`。
故障2新增DMA细分原因5；故障7在电流模式包含角度超过15ms未更新。

保留原有下载调试流程，以下下载说明未改。实物运行和中断耗时需下载后验证。

## 常见情况

- 弹窗提示 `Could not start gdb, no response from gdb`：这是电脑端 GDB 启动超时。
  本工程已加入 `-nx` 和 `prepare GDB`，2026-10-20 调整后用户确认恢复。
  保留这些设置；再次发生时按[下载调试故障记录](debug/README.md)检查。
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
# USART1 波形补充（2026-10-23）

Iq/Id 可通过 USART1 的 FireWater 文本波形查看，三位小数/A，配置见 [VOFA_CURRENT.md](VOFA_CURRENT.md)。
`vofa_sent_frames` 为已发帧数；`vofa_dropped_frames` 和 `vofa_tx_errors` 正常应为0。
停机排空队列后不再发送旧电流值。原 Live Watch 变量保持原用途。
