# 工程维护记录

- 2026-10-25 用户要求试速度+电流双环：默认current_mode由0改为1。
  保留电流d轴0.20/100、q轴3.40/150，外环目标100rpm、Kp0.001、Ki0，可能存在稳态速度误差。
  固定0.10A仅在mode=0生效；当前Iq由速度环给出。保存标定直接启动、限幅和串口均沿用。
  ARM编译通过，未烧录或运行实物；模式切换不等于电流环或外环已实物调稳。

- 2026-10-25 用户要求删上电定位，并明确确认电机、相线、编码器/磁铁安装均未变。
  复用已校验实物记录not-turning-213647：方向+1、零点4.233786106rad，配置于motor_config.h。
  speed_power.c已删除SetVoltageVector及定位/扫角分支，WAIT等待100ms新鲜有效角度后直接SPEED。
  使能前原子发布有效零Iq指令，初始角度按实际轴角计算，下一轮按原模式给目标；保留ADC零点及运行保护。
  不再做定位期间相电流映射检查，current_alignment_stage=0；speed_calibrated=1表示加载历史标定。
  mode=0/Iq0.1A持续、d轴0.2/100、q轴3.4/150及电压电流限幅均未改；旧开环流程保留。
  安装/相线变化后必须重新标定；不可把任意轴角当零点。未自动烧录、复位或启动电机。
  ARM编译与实际speed_power.c主机启动/保护测试通过；新启动尚未实物验证。
  备份/测试：D:/PCB/skip-alignment-20261025/。此条覆盖旧每次上电约1秒校准流程。

- 2026-10-23 用户要求改成FireWater文本并减少显示位数：当前vofa.c发送`Iq,Id\n`，
  单位A、四舍五入至三位小数(1mA)，不发二进制帧尾；VOFA选FireWater并关闭HEX显示。
  主循环格式化到独立DMA缓冲区，不使用浮点printf，电流ISR仍只保存原始float，内部控制精度不变。
  USART1 460800/8N1、2kHz采集和队列规则沿用，控制参数未改；此条覆盖下面旧JustFloat协议。
  ARM编译与文本/队列/DMA主机测试通过，未烧录或实测串口。记录D:/PCB/vofa-firewater-20261023/。

- 2026-10-23 用户要求USART1上传Iq/Id到VOFA：新增vofa.c/h，PA9 TX，460800/8N1，
  JustFloat通道0=Iq(A)、通道1=Id(A)，同一轮电流计算后2kHz入队，主循环启动DMA发送。
  DMA1 Channel4普通模式，DMA/UART IRQ均优先级2，64槽队列可用63槽；DMA数据在UART TC完成前不释放。
  满队列丢新帧计数，不阻塞控制；串口错误停遥测，不改电机状态。
  观察vofa_sent_frames/vofa_dropped_frames/vofa_tx_errors；停机后排空队列，不把最后反馈反复当新样本发送。
  用户当前d轴0.20/100、q轴3.40/150以及模式/限幅均未改；没有串口改参功能。
  ARM编译及真实vofa.c主机传输/队列/错误测试通过；未烧录、复位或启动电机，未实测串口及新增耗时。
  说明见VOFA_CURRENT.md，备份和验证在D:/PCB/vofa-usart1-20261023/。

- 2026-10-23 用户要求看持续固定电流反馈，并明确未施加阻力。已通过现有OpenOCD只读，
  逐Flash段确认258792af...持续版本生效，mode=0、目标Iq=0.10A、速度PI不参与。
  15秒234次读取：Iq平均0.09923A、观察范围0.00324~0.22492A；
  Id平均-0.00231A、范围-0.11123~0.16640A，速度约953~972rpm。
  ADC/电流约2kHz、周期499~500us、最长记录182us，状态9/故障0，抽样点未见电压限幅。
  结论只支持平均跟踪接近目标，反馈波动仍明显，不能声称内环已调稳或真实相电流已准确标定。
  SWD约15Hz抽样不是2kHz同步波形；历史误差峰值包含启动100ms之后的全部运行，不等于该15秒窗口。
  本轮未改PI、在线写参、烧录或复位。资料：
  D:/PCB/current-loop-20261023/running-watch-20261023-205839/。

- 2026-10-23 用户明确要求固定电流一直运行：current_test_duration_ms已由1000改为0，
  0表示不按时长结束；非0仍可选限时运行。到时判断新增非0条件，不能把0解释为立即停机。
  当前默认mode=0、Iq=0.10A、Id目标0，完成原约1秒校准后持续运行；速度PI不参与。
  手动speed_run=0及原故障保护仍停机且不自动重启。持续运行时done=0、状态9/故障0，
  elapsed继续累计；电流PI、3.8V/1A/3500mA及采样/角度/时序均未改。
  ARM编译和36个主机场景通过，含超过原1秒仍运行、手动停机和故障；未烧录或操作实物。
  本次明确要求覆盖下方旧的1秒限时默认记录。备份与验证：
  D:/PCB/current-loop-20261023/fixed-iq-continuous/。

- 2026-10-23 用户要求单独固定目标电流环：本轮默认current_mode=0，current_test_iq_a=0.10A，
  current_test_duration_ms=1000（const，源码配置）。沿用约1秒定位/编码器校准，之后固定Iq运行1秒，
  到时MotorControl_Stop(NONE)关闭DRV_EN，状态3/故障0/current_test_done=1，不自动重试。
  mode=0直接跳过速度PI和速度目标/增益检查，测速仍用于电角度外推；mode=1保留现有速度双环。
  d/q PI仍0.20/100，Id目标0，3.8V/1A/3500mA及传感器/ADC/时间保护均保留。
  新增current_test_elapsed_ms、current_test_done、current_test_iq_error_max_a、current_test_id_abs_max_a。
  峰值统计略过前100ms阶跃建立期，停机保留；current_id_a/iq_a也保留最后成功控制值，停机后不是实时电流。
  ARM编译及35个主机场景通过（含默认/正负/零/限幅Iq、自由轴、速度参数不参与、限时停机及故障优先），
  未实物验证本轮固定电流版本、未自动烧录/复位/启动/在线写参。
  软件固定轴跟踪不代替自由轴或实物表现；模型自由轴加速时仍有瞬态误差，不得声称已完成PI整定。
  备份和验证：D:/PCB/current-loop-20261023/fixed-iq-100ma/。

- 2026-10-23 用户再次确认：接入电流环之前，原电压模式速度环已经调稳；忽快忽慢是接入后出现。
  优先排查新增采样/电流变换/电角度/内环及双环匹配，不把旧速度环本身有故障作为已知前提。
  接入时速度PI输出由V改为A，初值也由0.017/0.005改为0.005/0.010；两种增益单位不能混用。
  用户已下载100rpm/P0.001/I0诊断版，逐Flash段匹配7c02cd0c... ELF。
  本轮15秒238次只读：目标Iq约0.033~0.049A、反馈0.024~0.059A，Id约-0.018~0.024A，
  速度反馈约51~66rpm，状态9/故障0，ADC/电流约2kHz、最长回调179us。
  小电流时Id接近零，不足以认定应先调d轴；当前数据也不证明300rpm或大电流瞬态已稳定。
  本轮未继续改PI或在线写参、烧录、复位；读取记录在
  D:/PCB/current-loop-20261023/running-watch-20261023-200856/。

- 2026-10-23 用户问当前如何继续调参：备好低速诊断版本，速度目标300->100rpm，
  Kp 0.005->0.001 A/rpm，Ki 0.010->0。电流d/q P/I仍0.20/100，mode=1；
  3.8V、1A目标限幅、3500mA保护、20ms滤波、1kHz/2kHz时序和启动校准均保留。
  这是减弱外环以观察内环的诊断试值，不是已调好的最终参数；无速度积分时允许稳态误差/达不到目标。
  仅将Ki写0不会清除已有积分，需要重新初始化；本次只编译备好，未改板上变量、烧录或启动。
  此前用户已下载触发修正版并保持空载：Flash与dbb1bd50... ELF匹配，状态9/故障0，
  实读约2kHz、周期499~500us、回调最长180us；15秒268次读取的速度反馈62~494rpm。
  软件当前运行不代表PI稳定，不能将此前模型稳态指标当实物结果；连续读取非原子同步波形。
  实读记录：D:/PCB/current-loop-20261023/running-watch-20261023-192804/；
  本轮参数备份和编译检查：D:/PCB/current-loop-20261023/pi-diagnostic-100rpm/。

- 2026-10-23 双环首次上板不转：已实读并校验运行镜像，live_error=1、control_ready=0、
  live_adc_sample_count=0；TIM4计数和CC4IF存在，但ADC无转换、DMA CNDTR=4。
  根因是新增TIM4 CH4误用冻结模式；改为内部PWM2（CCMR2 OC4M=111），无GPIO输出。
  仅在DRV_EN关闭、Error_Handler锁死状态临时切换该模式，实测ADC/DMA开始取数2045~2050；
  已恢复原寄存器，未烧录、复位或启动电机。不可恢复TIM4冻结模式，也不可把CC4IF当ADC触发证据。
  初始化无样本现锁存ADC原因1、motor_test_fault=2；驱动初始化失败锁存配置故障6。
  ARM Debug编译、25个主机场景和边沿模型通过；完整双环运行/实物测时/整定仍未验证。
  诊断快照、模式验证及修复记录：D:/PCB/current-loop-20261023/not-turning-190602/。

- 2026-10-23 用户确认实施速度+电流双环。新增 Core/Src/current_control.c：
  Clarke/Park、d/q PI、sqrt(Ud²+Uq²)<=3.80V 总限幅/回算积分、逆 Park、alpha/beta SVPWM。
  用户3.80V预算、300rpm目标、无额外RPM上限、120rpm/s斜坡、20ms测速滤波及约1秒启动保留。
  速度输出改为A，初始Kp/Ki=0.005/0.010 A/rpm、A/(rpm*s)，不得恢复旧电压单位0.017/0.005。
  d/q初始P/I=0.20/100，目标Iq默认1A，current_mode=1双环、0固定Iq验证（0.2A）。
  这些是待实物整定的初值，不得声称硬件已调好。3500mA软件停机阈值非硬件额定电流。
  ADC改为TIM2内部CH4(PWM2,CCR1680,TRGO_OC4REF)->TIM4 ITR1外部时钟/10->内部CH4 PWM2触发，
  2kHz V/W顺序扫描、13.5采样周期、四项DMA双银行，HT/TC回调执行电流计算；无TIM4中断。
  TIM4由CurrentSense_Init管理，内部CH4不连接PA3，PA3继续DRV_EN。ioc同步ADC/DMA/TIM3优先级。
  原raw/ma/peak现在是2kHz单组值，不能当旧32点平均比较；100~4090量程与3500mA阈值仍在。
  DMA可抢占TIM3，状态切换/发布/停机短暂互斥并保留首次故障；校准增加零/90度电流映射检查，故障9。
  原速度层5500ms宏保留，但电流运行新增15ms角度年龄要求，超时故障7；不能以旧宏说明运行允许5秒旧角度。
  AS5600仍允许弱磁ML=1；角度字节读取后记DWT/毫秒时刻，以最近实测速率做有限时长的角度外推。
  电流环入口到PWM最长用时超250us/回调间隔超750us故障5，原TIM3超500us检查保留。
  三个计算文件局部-O2，其他Debug仍-O0；Live Watch全局量保留。
  Debug ARM编译和主机模型检查通过；尚未下载/实物测时/整定，无自动烧录/启动/提交Git。
  说明见 CURRENT_CONTROL.md、SPEED_POWER.md；备份与可复现验证在 D:/PCB/current-loop-20261023/。

- 2026-10-23 用户明确要求取消额外目标转速上限：已移除 speed_power.c 的 SPEED_MAX_RPM
  和 target=Clamp(... +/-120rpm)。用户当前默认目标 300rpm，120rpm/s 加速斜坡继续保留。
  保留用户当前 P/I=0.017/0.005、20ms 滤波、5500ms 传感器过期阈值及已有电压/电流上限。
  用户此前已经把 Uq 改为直接使用 PI 请求；本次清理未使用的电压限速宏和过时注释，未重启限速。
  早期记录里的 60rpm、P/I=0.04/0.07、10ms 滤波和电压斜坡都是历史值，不要据此覆盖用户现值。
  Debug 编译通过；实际固件的软件验证覆盖正反编码器下 +300/-300/+600rpm 指令、加速斜坡与正常停止。
  备份与验证记录在 D:/PCB/remove-speed-cap-20261023/；未自动烧录或修改运行参数。
- 2026-10-23 负载响应：用户已把 Kp/Ki 调为 0.04/0.07，后续修改保留此值。
  speed_power.c 的电压变化速率从用户当前 3.5V/s 改为 20V/s，测速滤波从 20ms 改为 10ms；
  PI/换相仍为 1ms，电压上限仍为 3.50V。speed_requested_uq_volts 是限幅后的 PI 请求，
  speed_uq_volts 是限速后送去计算 PWM 的指令，二者均非实测绕组电压；启动/停机均清零。
  Debug 编译和模型回归通过，瞬间掉速减少但 +/-5% 恢复窗口未缩短，不得宣称实物负载问题已解决。
  尚未下载实测；备份、比较模型及日志在 D:/PCB/speed-response-20261023/，详见 SPEED_POWER.md。
- 2026-10-23 用户要求速度 PI 每 1ms 计算。SPEED_PI_PERIOD_MS 已由 10U 改为 1U，
  TIM3 1ms 内计算 PI 和换相，积分 dt=0.001s；Kp/Ki、每秒目标/电压变化速率及 20ms 测速滤波保持。
  测速只消费新编码器样本，使用真实间隔；I2C 数据更新并非严格 1kHz，不可声称实际响应快 10 倍。
  Debug 编译和实际固件函数的软件模型通过，包含每毫秒 PI 斜坡验证；未实物烧录和测时。
  资料在 D:/PCB/speed-pi-1ms-20261023/，说明见 SPEED_POWER.md。
- 2026-10-23 用户要求上电约 1 秒后闭环转动，不再做长时间正反扫。当前 speed_power.c
  使用 100ms 编码器等待 + 300ms 定位升幅 + 100ms 保持 + 300ms 单向 90 电角度识别
  + 200ms 保持，共约 1 秒进入状态 9；不进入旧反扫状态 8。目标仍 60rpm。
  用户尚无有效校准值，因此保留这 1 秒内必要的短定位/单向识别，不得假定任意角度为电角度零点。
  旧开环等待也改为 1000ms。新版编译与软件模型通过，未实物烧录，详见 SPEED_POWER.md。
- 修改本工程的 VS Code 下载、调试配置前，先阅读 [debug/README.md](debug/README.md)。
  其中记录了 2026-10-20 的 GDB 启动超时、修复配置及用户确认恢复的结果。
- F1/F4 的最新复用入口是 [STM32_DEBUG_REUSE.md](STM32_DEBUG_REUSE.md)。
  当前 `.vscode/` 是 F103C8 电机板；`.vscode.f1/` 是通用 F103C8 模板；
  `.vscode.f4/` 是通用 F407ZG 模板。原 `D:/test/jixiebi1/.vscodef_4/` 已同步 F4 模板。
- 保留两个启动项的 `debuggerArgs: ["-nx"]` 和 `preLaunchTask: "prepare GDB"`，
  以及依赖 `build` 的 `prepare GDB` 任务；若确需调整，结合故障记录验证后再更新说明。
- 两套模板均已同步 -nx、并行 GDB 预热、同步烧录完整校验、240kHz/100kHz 回退及 loadFiles: []。
  复制时四个 JSON 和 debug 子目录都要带上，并核对 ELF 名称和完整芯片型号。
  维护源为 `debug/flash-stlink.ps1`；修改共用逻辑后同步三份模板中的脚本。
- 烧录脚本明确区分 `-Target STM32F103C8/STM32F407ZG` 和 `-Board Foc1/Generic`。
  本板保留 Foc1 + motor-test.cfg 的 PA3 停机行为，通用模板使用 Generic，不操作 PA3。
  F4 沿用流程但使用自己的 ID、Flash/RAM 范围和 OpenOCD target；尚未做 F4 实物烧录，
  不能声称 F4 已实测或沿用 F1 耗时。`-ValidateOnly` 才是无硬件访问的离线镜像检查。
- 参数分离后的 F1 编译、模板一致性和离线检查已通过；用户确认板子已断电/拔掉，
  要求先完成配置记录，因此没有继续进行新的实物烧录回归。不要把断电连接失败当作新软件故障。
- 区分电脑端 GDB 启动超时和连接后的 Flash 写入失败；不要用旧的正常退出日志证明新一次启动成功。
- 2026-10-22 后续连接报错：上游 open_usb 句柄非空不保证 MCU 连接成功。
  已补显式 target_connect 返回值、有效 ARM CPUID 和 ID/容量输出，避免连接失败误报型号不匹配。
  `-ConnectOnly` 会连板、暂停并关闭本板 DRV_EN，但不写 Flash；`-ValidateOnly` 才完全离线。
  17:01 两个独立工具都连接失败，原因待当前硬件状态确认，不可声称已解决实际连接。
- 2026-10-22 当前配置：`prepare GDB` 并行等待 `build` -> `flash ST-Link` 和 `warm GDB`，
  两者均成功后启动 OpenOCD 调试。GDB 预热只输出版本，不访问 ST-Link，可以与烧录并行。
  `prepare GDB` 现在间接执行真实烧录，不再是只输出 GDB 版本的离线任务。
  `debug/flash-stlink.ps1` 调用 stlink 1.9.0 库同步写入、完整读回校验并保持暂停；
  当前默认 240kHz，四次约 6.4～6.7 秒通过；遇错自动用 100kHz 重写和完整校验一次，
  重试也已实测。480kHz 直接测试失败，勿设为默认。原 100kHz 三次约 13 秒通过。
  完整并行前置任务实测 7.77 秒，不能误称完整 F5 UI 耗时。OpenOCD 使用 `loadFiles: []` 防止二次下载，
  保留原生 DAP、三线软件复位与 Live Watch。详见 `debug/README.md`。
- 不要恢复已弃用的 `postLaunchCommands` 绝对路径 `monitor verify_image`：
  VS Code 展开的 Windows 反斜杠曾导致路径转义和 `Protocol error with Rcmd: 4E`。
  校验已在烧录任务中完成；约两分钟的 `-work-area-size 0` 仅为历史应急方案。
