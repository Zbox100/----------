# RTOS学习笔记

## 文档代码阅读笔记

### 关于main.c文件的阅读笔记

#### 发现P3问题记录（可以锦上添花）
1. “单一任务“注释命名不形象。
2. 命名拼写错误 ADX1345Handler
- 位置： main.c L55 及所有引用处
- 问题：变量名把`ADXL345` 拼成`ADX1345` ，且`DTH11` /`DHT11` 误用
- 后果：影响可读性、易引发混淆
- 修复：全局统一改为`ADXL345Handler` ，并统一`DHT11` 拼写
3. 魔法数字无宏定义
- 位置： main.c L71（`720000` ）、L86（`25` ）、L94（`700` ）
- 问题：硬编码数字无注释含义
- 后果：无法理解采样周期/阈值依据，改起来要翻上下文
- 修复：抽成`#define` ，配合`vTaskDelayUntil` 一并重构
4. main 中冗余代码
- 位置： main.c L436（`xReceive = pdPASS` ）、L467（`return -1` ）
- 问题：`xReceive` 初始值无意义（下一行被覆盖）；MCU 裸机 main 返回值毫无作用
- 后果：`return -1` 后行为未定义，反而可能跑飞
- 修复：删掉初始值；`return -1` 改为死等`while(1)` 兜底
5. 注释/命名职责混乱
- 位置： 多处（如 L274 注释写在数组后）
- 问题：部分注释错位、任务命名不形象、职责不清
- 后果：影响维护与阅读
- 修复：随重构整理命名与注释
#### 发现P2问题记录（影响维护与可读性）
1. 用了大量全局变量，导致代码可维护性差。
2. 变量可读性差，也没有加注释解释变量意义。
3. DHT11误写为DTH11。
4. 跌倒标志`Flag` 只增不清 L95-96 只要一次超阈值 Flag 就永远 >0，用户一进计步界面立刻误报警 
5. Z 轴脏数据未过滤 L92-93 只清了 X/Y，Z 的 I2C 垃圾值会直接污染计步 
6. 计步算法粗糙 L86-88 只认正向 delta、25mg 阈值过灵、无回滞/滤波，随便晃动就计步 
7. 队列满时静默丢步数 L90 超时 0 + 队列长度 1，show 任务未运行时发送失败无感知 
8. 6 个`xQueueCreate` 返回值都没检查 L456-461 堆不足时返回 NULL，后续 send/receive 对 NULL 操作是未定义行为
9. Z 轴脏数据未过滤
- 位置： main.c L92-93
- 问题：只对`temp_X` 、`temp_Y` 做了越界清零，`temp_Z` 未处理
- 后果：I2C 偶发读到垃圾值时，Z 轴脏值直接进入计步计算（L86`buf = temp_Z - buf` ）
- 修复：对`temp_Z` 同样加`if(temp_Z < 1 || temp_Z > 200000) temp_Z = 0;`

10. 计步算法粗糙
- 位置： main.c L86-88
- 问题：只认 Z 轴正向 delta、25mg 阈值过灵、无滤波/回滞
- 后果：随便晃动就可能记步，计步精度差
- 修复：用加速度模值 + 阈值回滞 + 滤波，或多轴综合判断

11. 队列满时静默丢步数
- 位置： main.c L89-90
- 问题：`xQueueSend(ADX1345Handler,&Normal_num,0)` 超时 0，且队列长度仅 1
- 后果：show 任务未运行时发送失败无感知，步数更新丢失
- 修复：加大队列长度或用超时/非阻塞 + 覆盖策略，必要时保留最新值

12. 工程内存在 3 套重复的软件 IIC 位带实现
- 位置： adxl345.c L7-57、Oled.c L14-15、xiic.h（MAX30102 使用）
- 问题：三个传感器（ADXL345 / OLED / MAX30102）各自实现了几乎相同的"位带翻转软 IIC"（Start/Stop/SendByte/RecvByte/应答），仅引脚和命名不同（PA4/5、PB8/9、PA11/12），`Delay_*` 阻塞时序多处重复
- 后果：重复代码约 200+ 行，职责分散，焊耦合于各设备驱动内，高耦合、低内聚；修改 IIC 时序需改 3 份，极易漏改不一致
- 修复：抽取唯一软 IIC 底层驱动（以 xiic 为基底，总线/引脚参数化），重写 adxl345.c 与 Oled.c 改用其接口，各设备驱动只保留寄存器地址与数据换算；RTOS 场景下更推荐直接统一迁移到硬件 I2C1，省 CPU 且无阻塞延时

13. DHT11 通讯过程忙等`while()` 自旋不释放 CPU
- 位置： DHT11.c Read_Byte L63、L66；Read_DHT11 L126-127
- 问题：读 40bit 及从机应答全程用裸`while(DHT11_DATA_IN()==Bit_RESET/SET)` 自旋，无 `taskYIELD`、无阻塞
- 后果：采样期间死占 CPU，挤压同优先级（均为 4）任务，系统响应变差（对应忙等类问题）
- 修复：轮询处加`taskYIELD()`；或改用硬件定时/中断采样

14. DHT11 通讯内含 18ms 硬阻塞延时
- 位置： DHT11.c Read_DHT11 L119（`Delay_fnms(18)`）
- 问题：主机拉低后用毫秒级硬延时阻塞，期间不参与调度
- 后果：每轮通讯至少 18ms 纯阻塞等待，放大 CPU 占用与周期抖动
- 修复：RTOS 下改用`vTaskDelay` 让出 CPU，或在专用任务中等待

15. `UsartPrintf` 栈占用大 + 发送忙等
- 位置： usart.c L109（局部缓冲）、L120（发送忙等）
- 问题：`unsigned char UsartPrintfBuf[256]` 是局部变量，每次调用吃 256B 栈；逐字节发送用`while(USART_GetFlagStatus(TC)==RESET)` 忙等自旋
- 后果：在 512B（128word）栈的任务里调用即占一半栈，叠加`vsnprintf`内部开销，是"栈不足"最大单点来源（呼应堆栈水位方案）；115200 波特率下约 87μs/字节阻塞，长文本耗 CPU 数 ms，不释放调度
- 修复：缩短打印文本；避免 %f（改整数输出）；长文本改用发送缓冲/中断 DMA；调用侧确认任务栈余量（monitor 实测）
#### 发现P1问题记录（偶发设计缺陷）
1. `720000/72M = 10ms` 的前提是"1 次循环 = 1 个 CPU 周期"，这不可能成立，误把一次循环当作一次始终周期，存在设计缺陷。
2. 类似以上问题，是逻辑代码粗硬移植到RTOS中导致的设计缺陷。
3. L64 的`unsigned char ReadAdxl345;` 没有初始化 ，第一次进循环读的是栈上垃圾值。如果恰好等于 1，上电后会立刻误触发一次检测。
4. ADX1345Handler 接收端类型不匹配 → 任务栈越界
- 位置： main.c L120
- 问题：发送端发`uint32_t` （4 字节，L90），接收端用`uint8_t dis_buf_R` 接收 → 4 字节拷进 1 字节变量，向栈上多写 3 字节
- 后果：破坏`Adxl345_Show_f` 栈上相邻数据，可能显示错乱或随机死机（触发条件是进入计步界面）
- 修复：接收变量改成`uint32_t dis_buf_R;`
5. 软件定时器依赖循环速度，周期不确定
- 位置： main.c L71-79
- 问题：`time==720000` 是"迭代次数"不是时间，实际周期 ≈ 自旋耗时 + 100ms，随优化等级/中断频率漂移
- 后果：检测周期不准且不稳定，计步统计失真
- 修复：改用`vTaskDelayUntil` 精确周期唤醒

6. 堆容量存疑 + 无创建判空
- 位置： main.c L456-462
- 问题：堆仅 10KB（FreeRTOSConfig L176），11 个任务栈≈7KB+6 队列+空闲/定时器任务，可能建失败。6 个`xQueueCreate` + 多数`xTaskCreate` 返回值都没检查
- 后果：任务/队列创建失败返回 NULL，后续 send/receive 对 NULL 句柄操作是未定义行为 → 随机崩溃，且极难定位（B10 的具体场景）
- 修复：`configTOTAL_HEAP_SIZE` 加大到 12~16KB；对每个 create 判空并错误处理

7. rxBuffer 数据处理竞争
- 位置： main.c L291（读） && L18（全局定义，串口中断改写）
- 问题：全局`rxBuffer` 由串口 ISR 写入，`Menu_handling_f` 任务里裸读，无锁、无队列同步
- 后果：读中断改的变量是数据竞争，蓝牙选功能时可能读到残缺/过期值，行为不确定
- 修复：串口中断把选择项`xQueueSendFromISR` 到队列，任务用`xQueueReceive` 读

8. Time_Show 相对延时漂移
- 位置： main.c L171
- 问题：`vTaskDelay(1000)` 是相对延时（tick=1ms 时为 1 秒），多次累加会累积相位漂移
- 后果：长期运行显示节奏缓慢漂移（虽被 L164 阻塞接收部分掩盖）
- 修复：改`vTaskDelayUntil` 绝对延时

9. Max30102 忙等不释放 CPU
- 位置： main.c L227
- 问题：`while(MAX30102_INTPin_Read()==0)` 忙轮询不挂起，INT 未触发前死占 CPU
- 后果：挤压其他同优先级（均为 4）任务，系统响应变差
- 修复：改用 INT 中断唤醒，或忙等中加`taskYIELD()`

10. beep 任务无退出/自限
- 位置： main.c L390-405
- 问题：`vTaskResume` 后无限循环翻转蜂鸣器，无次数/时长上限
- 后果：误触发后一直报警，无停止手段（配合 P2-1 Flag 不清更糟）
- 修复：加"持续 N 次后自挂起"或超时自停

11. MAX30102 采样在数据未就绪时退化为空转窗口
- 位置： main.c L225-234
- 问题：内层 `while(MAX30102_INTPin_Read()==0){ max30102_read_fifo(); }` 只有在 INT 引脚为低电平(0，表示 FIFO 有数据)时才读取；当 INT 为高电平(1，数据未就绪)时条件为假，跳过读取，外层 `for` 直接空转 128 次，再执行 `blood_Loop()`
- 后果：传感器数据未就绪时，代码用空窗口（未采集到 128 组真实样本）照样去算血氧/心率，结果失真；且 `for` 空转毫无阻塞，白耗 CPU、挤压同优先级（均为 4）任务，忙等不释放调度（结合 P1 第 9 条忙等问题）
- 修复：改为"等待 FIFO 数据就绪再采，凑满 128 组才算"——用 `blood_Loop()` 需要的样本数 `vTaskDelay` 或 `vTaskDelayUntil` 轮询等待，或由 MAX30102 的 INT 中断触发采集；确保采样窗口实际填满后再计算

12. `DHT11_Data_TypeDef` 结构体重复定义
- 位置： DHT11.c L96-103 && DHT11.h L4-11
- 问题：同一个 `DHT11_Data_TypeDef` 结构体在 `.c` 内部和 `.h` 里各定义了一遍，字段完全一致
- 后果：任一文件同时包含两处定义即触发编译 "redefinition of type" 错误；当前未报是因 `.c` 内部定义与 `.h` 的 extern 使用分离，属"迟早踩雷"的隐患
- 修复：删除 DHT11.c L96-103 的重复定义，统一复用 DHT11.h 中的一份

13. DHT11 采样频率过高，违反器件最小读取间隔
- 位置： main.c L182-183（`vTaskDelay(100)`）&& DHT11_SCAN()
- 问题：`DHT11_SCAN()` 每 100ms 调用一次；DHT11 数据手册规定两次读取间隔须 ≥ 1s，本实现频率偏高
- 后果：读取过频时 DHT11 内部数据未刷新，多数读到的是上一次的旧值，温湿度显示滞后失真
- 修复：采样间隔改到 2s 左右，用 `vTaskDelayUntil` 精确周期触发

14. `blood.c` 幅值计算时 `s1` 被 `s2` 覆盖，RED 通道频谱丢失
- 位置： blood.c L119-120
- 问题：幅值计算处写了两行都对 `s1[i].real` 赋值（第二行 `s1[i].real=sqrtf(s2[i].real*s2[i].real+s2[i].imag*s2[i].imag)` 把第一行 RED 幅值覆盖），`s1` 里最终存的是 IR 通道频谱而非 RED
- 后果：`find_max_num_index(s1,30)`（L148）找 RED 主频时实际用 IR 数据，心率（L152）与血氧（L156-158）计算基于被污染通道数据，结果失真；RED 幅值还白算一次`sqrtf`
- 修复：第二行改为写 `s2[i].real`：`s2[i].real = sqrtf(s2[i].real*s2[i].real + s2[i].imag*s2[i].imag);`
#### 发现P0问题记录（致命错误）
1. DTH11Handler 队列项大小写成了指针大小
- 位置： main.c L460
- 问题：`xQueueCreate(1, sizeof(&DHT11_Data_R2)*1)` → 队列项只有 4 字节 ，但发送方 DHT11.c L166 发的是 5 字节的`DHT11_Data_TypeDef` 结构体（ DHT11.h L4-11 ）
- 后果： 每次发送往堆里多写 1 字节 ，DHT11 每 100ms 发一次 → 堆持续被破坏，表现为随机死机、数据错乱，很难排查
- 修复：`xQueueCreate(1, sizeof(DHT11_Data_TypeDef))`

2. Max30102Handler 队列项大小同样写错 
- 位置： main.c L461
- 问题：队列项 4 字节，发送方 blood.c L175 发的是 8 字节 的`BloodData` （int + float， blood.h L24-28 ）
- 后果： 每次发送往堆里多写 4 字节 ，同样的堆破坏风险；且接收端只拷到 4 字节，`SpO2` 浮点会读到残留脏值
- 修复：`xQueueCreate(1, sizeof(BloodData))`
3. ADX1345Handler 接收端类型不匹配 → 任务栈越界（已在你笔记 L24-28 记录过，这里修正版本）
- 位置： main.c L119-120
- 问题：发送端发`uint32_t` （4 字节，L90），接收端用`uint8_t dis_buf_R` 接收 → 4 字节拷进 1 字节变量，向栈上多写 3 字节
- 后果：破坏`Adxl345_Show_f` 栈上相邻数据，可能显示错乱或随机死机（触发条件是进入计步界面）
- 修复：接收变量改成`uint32_t dis_buf_R;`


#### 优化方向记录（未实施，仅记录思路）
> 对应 P2-9，暂不写码，先记大纲
1. 统一 IIC 驱动（三层分离）
- LL 层：软 IIC / 硬件 I2C 字节原语，总线引脚用结构体参数化（不写死 GPIO）
- 协议层：`I2C_WriteReg/ReadReg/ReadMulti` 自动拼 7 位地址+R/W 位、处理 ACK
- 设备层：adxl345.c / Max30102.c / Oled.c 只留寄存器+数据换算，删各自内联位带
- 地址统一传 7 位（MAX30102=0x57、ADXL345=0x53、OLED=0x3C）
2. 回调函数优化
- 事务完成/错误回调 + RTOS 二进制信号量（发起即返回，不忙等；超时可选）
- 上电设备探测回调（按已知地址表发地址问 ACK，无则置故障标志）
- 硬 I2C 可中断化；软 IIC 保留时序但补"超时 + taskYIELD"
- 落地建议：先做第一步（统一驱动，纯重构低风险）；回调/中断化第二步再做
3. FreeRTOS 堆栈水位（High Water Mark）监测与优化
- 目标：用 `uxTaskGetStackHighWaterMark()` 监测 12 个任务栈使用，识别不足/过度分配并优化（未来实施，先记钩子）
- 前置开关（FreeRTOSConfig.h）：`INCLUDE_uxTaskGetStackHighWaterMark=1`、`configCHECK_FOR_STACK_OVERFLOW=2`、`configUSE_MALLOC_FAILED_HOOK=1`（当前均为 0/注释）
- 做法：新建低优先级常驻 `StackMonitor_f` 任务，5s 周期逐个打印各任务 `minFree`（返回值×4=字节，从外部任务调用更准）及 `xPortGetFreeHeapSize()`
- 现状关键数据：12 任务栈合计 1792word=7168B，堆仅 10KB，余量极小；风险任务 Max30102_Work/DTH11_Work（UsartPrintf %f 浮点）、Time_Work（localtime 库）
- 优化方向：栈按"实测峰值+40~64word"调整；printf 去 %f 改整数输出；优先级分级（重 Work=5、其余 Work=4、Show=3）；堆提至 16KB
- 验证标准：溢出钩子 0 触发、全部任务 minFree≥64word、堆余量≥2KB
- 详见会话记录，未来实施时按"开关→监测→分析→调整→复测"闭环执行
4. 空闲任务（Idle Task）利用
- 现状：空闲任务必存在（FreeRTOS 强制创建，优先级 0），但当前 `configUSE_IDLE_HOOK=0`，未实现 `vApplicationIdleHook()`，CPU 空闲时纯空转烧电
- 附带注意：`configUSE_PREEMPTION=0`（非抢占/协作式），所有功能任务同优先级 4，靠阻塞/让出切换，空闲任务机会较多
- 可用途：① 省电（idle 里 `__WFI()`/停机，对电池供电手环最关键，电流可几十mA→几mA）；② CPU 负载统计（idle 计数器->空闲占比）；③ 看门狗喂狗兜底；④ 低优先级后台清理
- 约束：idle hook 内禁调任何阻塞/挂起/删除任务的 API，只放非阻塞短操作
- 开启方式：FreeRTOSConfig.h `configUSE_IDLE_HOOK=1`，实现 `void vApplicationIdleHook(void)`

### 修改记录
1. P3-1已修改（修改为“专用任务”）。
