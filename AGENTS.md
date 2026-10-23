# 工程维护记录

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
