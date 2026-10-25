# 速度外环 + d/q 电流内环（2026-10-23）

2026-10-25最新模式：current_mode默认改为1，试速度+电流双环；速度目标100rpm、Kp0.001/Ki0。
电流PI参数保留，未据此认定已调稳；下文mode=0默认描述为历史，仍可手动选0进行固定Iq测试。

2026-10-25更新：用户确认安装及接线未变，当前启动已取消定位/扫角，改为加载
motor_config.h中实测方向+1、零点4.233786106rad。取得有效角度、等待100ms后直接进入状态9。
ADC零电流标定及运行保护保留；定位相电流检查不再执行，current_alignment_stage=0。
当前源码d轴P/I=0.20/100、q轴=3.40/150，本次未改；下面的旧参数/定位过程为历史说明，以本段及源码为准。
新启动仅完成ARM编译与主机测试，未自动烧录或实物验证，详见SPEED_POWER.md顶部更新。

当前代码支持双环，本轮默认固定Iq电流环：current_mode=0、Iq=0.10A、持续运行。
电流算法集中在 `Core/Src/current_control.c`，电流P/I仍为0.20/100。
已做 ARM 编译和主机模型验证。首次上板发现并修复TIM4触发配置错误，
用户已下载过100rpm低速诊断版；本轮固定电流版本尚未下载或实物验证。

## 本轮固定电流运行

修改 `current_control.c` 顶部的 `current_test_iq_a` 设置测试目标（默认0.10A），
`current_test_duration_ms` 设置时长（默认0=持续运行，非0为限定毫秒数；const，修改后重新编译）。
默认 `current_mode=0`：先取得有效角度并加载保存的方向/零点，再给固定Iq、Id目标0；
固定Iq持续运行，直到手动 `speed_run=0` 或触发原有故障保护。关闭驱动不保证转子立即停止惯性转动。

速度PI整段不参与Iq计算，100rpm目标与速度P/I对此模式不起作用；
编码器、测速、电角度计算及外推仍工作，电流PI仍随ADC每500us执行。
`current_mode=1`可恢复速度+电流双环，沿用现有100rpm/P0.001/I0诊断参数。
切换试验方式或重新测试时，应重新初始化，清除旧积分和已完成的测试状态。

持续运行时 `current_test_done=0`、elapsed继续累计、`motor_test_state=9`、`motor_test_fault=0`。
只有把duration设为非0，才会到时正常结束并置done=1、状态3/故障0。
提前故障停机不置done；`speed_run=0`仍可提前正常停机，此时done也保持0。
`current_id_a/iq_a`保留停止前最后一组成功控制反馈，输出/目标指令会清零；
停机后该Id/Iq不代表此刻仍流过电机的电流，应对照 `current_test_iq_a` 看最后一次跟踪。
`current_test_iq_error_max_a`、`current_test_id_abs_max_a`记录测试100ms之后的采样误差峰值，停机保留。
这些指标包括采样噪声和加速动态，不是低通平均值，也没有未经实测的合格阈值。

本轮仅完成ARM编译与软件回归，没有自动烧录或启动。资料在
`D:/PCB/current-loop-20261023/fixed-iq-continuous/`；前一版1秒限时记录在fixed-iq-100ma/。

## 控制关系

```
目标转速 - 实测转速 -> 速度 PI -> 目标 Iq（A）
                                   |
V/W 采样 -> Iu=-Iv-Iw -> Clarke -> Park -> 实际 Id/Iq
                                   |
                        Id目标=0、Iq目标由速度环给出
                                   |
                         d/q 两个 PI -> Ud/Uq（V）
                                   |
                   总电压矢量限幅 -> 逆 Park -> SVPWM -> PWM
```

Id=0 是电流目标，Ud 由 d 轴 PI 调整，不能再固定 Ud=0。
电压总预算仍是 `MOTOR_TEST_VECTOR_VOLTS=3.80f`，限制 `sqrt(Ud*Ud+Uq*Uq)<=3.8V`。
超出时同比例缩小两轴，并回算积分，避免饱和时积分继续堆积。
速度积分既考虑目标 Iq 限幅，也考虑内环电压饱和方向。

## 文件和执行入口

| 文件 | 职责 |
| --- | --- |
| `Core/Src/current_control.c` | Clarke/Park、d/q PI、总电压限幅、逆 Park、控制参数 |
| `Core/Src/current_sense.c` | 定时触发链路、ADC/DMA 双银行、零点、电流换算、数据检查 |
| `Core/Src/speed_power.c` | AS5600 发布、测速、1秒准备、速度 PI 输出目标 Iq |
| `Core/Src/motor_control.c` | 1kHz 外环调度、2kHz 电流回调、统一启停 |
| `Core/Src/motor_pwm.c` | 新增 alpha/beta 入口，共用逆 Clarke 和 min/max SVPWM |

主循环仍读 AS5600，I2C 不进入电流中断，不在关中断区等待。
`TIM3 -> MotorControl_Update1ms -> SpeedPower_Update1ms` 每 1ms 更新目标 Iq。
`ADC DMA半满/全满 -> MotorControl_OnCurrentSample -> CurrentControl_Calculate`
每 500us 更新电流 PI 和三路 CCR；正常双环运行时只有这条路径写运行 PWM。
启动定位仍由原电压模式输出，切换时先发布零电流指令，再原子进入状态9。

DMA 优先级0，TIM3优先级1；发布指令、切换启动状态和停机时短暂屏蔽中断。
电流故障能抢占速度计算，第一次故障锁存，后续状态切换不能重新使能。
旧开环模式将 TIM3 恢复为与 DMA 同优先级，避免抢占旧的状态/PWM 更新。

## 采样时序的实际改动

原来是自由连续扫描、64项缓冲、每相32点平均、1ms读取，DMA半满/全满通知关闭。
现在改为：

1. TIM2 的内部 CH4 配成 PWM2，CCR4=1680，TRGO=OC4REF。
   20kHz 中心对齐计数顶点前约1.67us出现上升沿。CH4不连接GPIO，PA3仍是DRV_EN。
2. TIM4 通过 ITR1 接收 TIM2_TRGO，外部时钟模式1，每10个边沿计数一轮。
   内部CH4设为PWM2、CCR4=9，产生2kHz上升沿触发ADC。TIM4不输出引脚、不产生CPU中断。
   冻结模式虽会置CC4IF，但在本板不能触发ADC；必须保留内部通道波形。
3. ADC1 关闭连续转换，常规组触发选 TIM4_CC4；V/W两通道依次转换。
   12MHz ADC、13.5周期采样+12.5周期转换：每相约2.17us，一组约4.33us。
4. DMA四项循环：`V0,W0,V1,W1`。HT读前半，TC读后半，只使用已完成的一组。
   两相相差约2.17us，不是同时采样。DMA优先级由LOW提高到HIGH。
5. 驱动关闭时先收集64组零点，约32ms；初始化等待40ms后检查零点1800~2300。

TIM2 CH4 与 TIM4 的自定义配置集中在 `CurrentSense_Init()`，在 CubeMX 初始化之后执行。
`foc1.ioc` 已同步 ADC 的触发、采样时间、DMA优先级和TIM3优先级。
TIM4由用户模块管理，没有给CubeMX添加物理CH4引脚；不要因此把PA3改成TIM2_CH4。

现在 `live_adc_v_raw/w_raw` 和 `live_i_*_ma` 是一组采样的瞬时估算值，
`motor_test_peak_ma` 也改为2kHz样本的历史最大值，不能与旧32点平均峰值直接等同。
原100~4090量程检查、3500mA停机阈值保留，改在每次采样中检查；尖峰可能比旧版更早触发。

## 启动和角度

100ms等待 + 300ms定位升幅 + 100ms定位保持 + 300ms单向90电角度扫描 + 200ms保持，
仍在TIM3启动约1000ms进入状态9，外设初始化时间另计。定位电压仍0.50V。
零度保持与九十度保持各检查一次电流方向，均不额外延长启动时间。
沿电压方向电流应大于0.02A，垂直分量绝对值应小于沿向电流的0.75倍。
该检查用于拒绝明显的映射/极性错误与无反馈；转子尚未静止、零点错误等也可能导致不通过。

AS5600保留MD=1且MH=0的使用条件，ML弱磁仍允许；没有改回弱磁故障。
角度时间戳现在在两个原始角度字节读取完成时取得，后续AGC等诊断的读取耗时计入数据年龄。
电流回调用最近实测速度把电角度外推到处理时刻。外推不是新测量，
不能消除AS5600内部滤波、I2C传输延迟或快速加减速时的估计误差。

原速度层的5500ms阈值保留；**新增电流模式的有效角度上限15ms**，超过即故障7停机。
这意味着双环运行时实际采用15ms要求，不会拿5秒以前的角度继续电流换相。
测速仍按真实新样本时间间隔计算，滤波保持20ms，目标斜坡保持120rpm/s。

## 本次参数及单位

| 参数 | 当前初值 | 含义 |
| --- | --- | --- |
| `speed_target_rpm` | 100rpm | 本轮临时诊断目标，无额外转速上限 |
| `speed_kp` | 0.001 A/rpm | 由0.005调低，减弱外环以观察电流跟踪 |
| `speed_ki` | 0 A/(rpm·s) | 暂时关闭速度积分，重新初始化后才无旧积分 |
| `current_kp_d/q` | 0.20 V/A | 两轴初始比例参数 |
| `current_ki_d/q` | 100.0 V/(A·s) | 两轴初始积分参数 |
| `current_iq_limit_a` | 1.0A | 目标Iq限幅，不是板子额定电流，也不是供电电流 |
| `current_mode` | 0 | 本轮默认固定Iq测试；1=速度+电流双环 |
| `current_test_iq_a` | 0.10A | 仅mode=0使用，仍受目标Iq限幅 |
| `current_test_duration_ms` | 0 | 0=持续运行；非0为校准后运行毫秒数，源码const配置 |
| `MOTOR_TEST_VECTOR_VOLTS` | 3.80V | 保留用户电压预算，现在用于总矢量 |
| `MOTOR_TEST_LIMIT_MA` | 3500mA | 原相电流软件停机阈值，不是恒流目标或硬件额定值 |

这些PI是首次带电调试起点，不是实物已经调好的参数。先以小目标Iq验证反馈跟踪，
再整定速度外环。`current_mode=0`时不调速，固定Iq只要求扭矩，不保证轴转速固定。
mode=1仍采用较小的速度P和零速度I，可能有稳态误差、达不到100rpm，不能当最终稳速方案。
此前100rpm参数备份在 `D:/PCB/current-loop-20261023/pi-diagnostic-100rpm/`。
两种模式下`speed_run=0`都关闭驱动；写回1不自动重启。目标转速0仍是主动零速控制。
停机清除速度/电流积分及已发布指令，保留最后一次Id/Iq和相线检查反馈供诊断。

## Live Watch 与故障

首先添加 `current_iq_target_a`、`current_iq_a`、`current_id_a`、`current_ud_volts`、
`current_uq_volts`、`current_voltage_limited`、`speed_target_rpm`、`speed_measured_rpm`、
`motor_test_fault`。再检查：

- 固定电流测试优先看 `current_test_iq_a`、`current_test_elapsed_ms`、`current_test_done`、
  `current_test_iq_error_max_a`、`current_test_id_abs_max_a`，结束后的Id/Iq是保留值。
- `live_adc_sample_count`：运行时设计为每秒约增加2000，此前空载读取已观察到该刷新率。
- `current_control_count`：只在状态9电流计算成功时增加。
- `current_control_period_us`：相邻处理时刻间隔，预期约500us。
- `current_control_max_us`：从采样回调入口起，包括换算、诊断、算法及PWM写入的最长用时；
  不含之前的HAL中断分发开销，超250us故障5。
- 两次回调间隔超750us也故障5；原TIM3超500us检查保留，包含被电流中断抢占的时间。
- `current_alignment_stage`：0未通过零度检查，1零度通过，2九十度也通过。
- `current_alignment_along_a/across_a`：最后一次定位检查的电流投影，停机后保留。

原`speed_uq_volts`保留为电流PI限幅后Uq的兼容别名；
`speed_requested_uq_volts`现在表示电流PI限幅前Uq，可能超过3.8V，不再是速度PI的输出。
Live Watch逐项读取不是一组原子快照，4Hz画面也不能证明没有瞬态峰值。

故障0~8保留。新增9=定位电流与施加电压方向不符，应核对相线、符号、零点及转子是否已静止。
ADC细分原因保留1未刷新、2HAL错误、3V越量程、4W越量程；新增5=DMA通知积压/半区被覆盖。
初始化40ms内不足64组采样也锁存ADC原因1及电机故障2，再进入Error_Handler；
驱动初始化失败则锁存配置故障6，避免只看到live_error=1而没有电机故障信息。
ADC原始值接近4095时反馈可能失真，不能靠扩大软件阈值获得更大的可靠测量范围。

## 验证与限制

主机脚本、修改前备份和结果：`D:/PCB/current-loop-20261023/`。
运行 `D:/python/python.exe D:/PCB/current-loop-20261023/run_checks.py` 可重复主机验证，
不连接下载器、不访问电机板。脚本编译实际固件模块，并抽取当前main及TIM3回调到模拟环境。

覆盖两万组独立d/q变换、旧/新PWM入口等价性、两轴总电压限幅、积分饱和恢复；
实际控制代码加模拟外设/电机模型覆盖编码器正反方向、约1秒启动、负载变化、反转、
无额外RPM限幅、固定正负Iq、计数回绕、角度过期、ADC量程/刷新/错误、DMA覆盖、
电流超限、计算超时、驱动关闭、非法参数、抢占时故障锁存，以及旧开环定时停机（固定轴模型）。
当前共36个运行场景，包括持续固定Iq和冻结模式无采样的初始化故障复现。
原双环回归显式使用300rpm/P0.005/I0.010参数；固定电流场景验证固件实际默认0.10A/不限时。
固定轴模型检验正负/零电流阶跃跟踪，自由轴模型检验超过原1秒仍持续及手动停机，不把加速误差当稳态指标。
还验证速度目标/增益无效时不影响固定电流模式、ADC/非法Iq故障、限幅及停止后不重启。
定时器离线模型按内部波形边沿判断，
检查20kHz内部边沿10分频后为2kHz、冻结模式虽有比较匹配但无触发，以及理想采样窗口。

首次上板故障的读取和临时模式验证见 `D:/PCB/current-loop-20261023/not-turning-190602/`。
运行镜像与修复前ELF的所有Flash段一致；实读live_error=1、采样数0、DMA CNDTR=4。
只将TIM4 CH4从冻结模式临时改为PWM2，ADC/DMA就开始搬运2045~2050的原始值。
验证时PA3/DRV_EN始终关闭，程序仍在Error_Handler；之后恢复原TIM4寄存器，未下载或启动电机。
这验证了触发配置的根因，不代表完整双环或真实电流精度已经验证。

用户随后下载触发修正版，现有OpenOCD读取并逐Flash段校验与ELF一致。
用户确认没有施加阻力；15秒268次读取期间状态9、故障0，ADC/电流回调均约2000次/秒，
周期499~500us、回调最长记录180us。旧300rpm/速度P0.005、I0.010下反馈62~494rpm，
Id约-0.57~0.84A，说明实物尚未调稳；读取范围不是2kHz同步波形的精确峰值。
数据见 `D:/PCB/current-loop-20261023/running-watch-20261023-192804/`。

模型的电阻、电感、惯量和转矩系数均为模拟值；未证明实物PI稳定性、角度精度、
PWM尖峰抑制或持续带载能力。180us仅为该次运行中观察到的最长回调值，非所有工况的最坏用时；
仍需确认采样波形、电流精度和高负载下的中断余量。旧25场景通过对应原始300rpm PI配置，
不代表本轮100rpm/纯P参数满足原300rpm稳态测试。
没有修改下载调试方案，没有自动烧录、复位CPU、启动电机或提交Git。

参考：[SimpleFOC电流控制](https://docs.simplefoc.com/foc_current_torque_mode)、
[速度环](https://docs.simplefoc.com/velocity_loop)、
[ST RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)。
