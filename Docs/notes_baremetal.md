# 裸机学习笔记

> 用途：第 2 周裸机专题（启动流程 / NVIC / 时钟树 / GPIO / EXTI）的机制笔记。
> 写法：每条机制后面必须挂一句"**我项目里哪儿用到了它**"。
> 验收：能不看本文件讲出来。第 9 周会抽这三条 ——
> ①逐行讲清 `Reset_Handler`；②HardFault 时说出 LR / PC / PSP 的含义与定位思路；③说出 PendSV 为何保存 R4\~R11。

> ✅ **【09-27 已修正 · 启动文件 = `startup_stm32f10x_md.s`】**
> 本文件第 1 节的行号已按**工程实际编译的 `startup_stm32f10x_md.s`（307 行 / 向量表 59 项）**逐条核对更正。
> 依据：`手环项目.uvprojx` 里 `<Device>STM32F103C8`，编译的 `.s` 文件只有 `startup_stm32f10x_md.s`。
> `Start/` 下并存 8 个启动文件，`hd`（大容量 / 358 行 / 76 项）与 `md`（中容量 / 307 行 / 59 项）**行号完全不同**，
> 早期版本误引 `hd.s` 行号，已全部替换。完整对照表见 `Docs/architecture_draft.md` **附录 A**。

***

## 1. 启动文件与复位流程

### 1.1 文件结构分区（先做这个，避免"想读懂 450 行"）

| 段落              | 行号       | 处理方式                                                                                                                        |
| --------------- | -------- | --------------------------------------------------------------------------------------------------------------------------- |
| 栈 / 堆大小定义       | L33-49   | `Stack_Size=0x400`(1KB, L33)/`Heap_Size=0x200`(512B, L44)。这是 MSP主栈+C库堆，**与 FreeRTOS 10KB 堆无关**（后者在 heap\_4.c 的 ucHeap\[]）             |
| 中断向量表           | L56-124  | L56 `AREA RESET` 开段；L61 `__Vectors` 起表；L122 `__Vectors_End`；L124 算长度。第0项 `__initial_sp`(L61)=栈顶；第1项 `Reset_Handler`(L62)；后续=各异常/中断入口。设栈指针由**硬件**复位时读第0项自动完成，不在 Reset\_Handler 里手写                          |
| `Reset_Handler` | L129-137 | `IMPORT __main/SystemInit`(L131-132) → `BLX SystemInit`(L134, 配时钟) → `BX __main`(L136, C库: 拷.data/清.bss/调main)。拷data/清bss**不在此文件**，在 C 库 \_\_main |
| 各默认中断处理函数       | L139-303 | L139 起；各系统异常壳 L160-181（`B .` 死循环兜底 + `[WEAK]` 弱符号）；L183 `Default_Handler` 起批量声明外设弱符号（至 L285 左右）；L288-303 为 `__user_initial_stackheap`（两段式内存时给出栈/堆起止，本项目单区内存故不生效）；被 stm32f10x\_it.c 中已实现的中断处理覆盖 |

### 1.2 Reset\_Handler 逐行

- 记下：设栈指针、调 `SystemInit`、调 `__main` 分别在哪一行
- 关键提问：**设栈指针这一步真的写在** **`Reset_Handler`** **里吗？** 如果不是，是谁做的？

| 动作           | 行号       | 说明                                                                    |
| ------------ | -------- | --------------------------------------------------------------------- |
| 设栈指针         | 非代码行     | 上电复位时**硬件**自动读取向量表第 0 项 `__initial_sp`（L61）载入 SP，**不在 Reset\_Handler 手写** |
| 调 SystemInit | L133-134 | `LDR R0,=SystemInit` → `BLX R0`（BLX 可返回）                              |
| 调 \_\_main   | L135-136 | `LDR R0,=__main` → `BX R0`（**BX 不返回**，\_\_main 最终接管并调 main）           |

### 1.3 从复位到 main 的完整链路

按顺序写清 5 步，并标注每一行的**源码位置**（启动文件 / 库函数 / 你的代码）：

1. 上电复位，**硬件**自动读向量表第 0 项 `__initial_sp` 载入 SP（非软件写）
2. **PC** 跳到 reset\_handler 开始执行（硬件读向量表第 1 项给的地址，L62→L129；SP 不动，仅 PC 跳转）
3. 在 reset handler 中，先调用 system\_init 函数进行时钟配置
4. 然后调用 \_\_main 函数在 C 库中进行 .data 的拷贝和 .bss 的清零
5. **PC** 跳转到 main.c 函数进行处理（\_\_main 内最终调用 main，仍是 PC 跳转）

关键提问：拷 `.data`、清 `.bss` 的代码**在启动文件里找不到** —— 它们在哪儿？
回答：在 **C 运行库的** **`__main()`** 里，由分散加载描述文件 `.sct` 引导实现。`Reset_Handler` 执行到 `BX __main` 即退出，data/bss 的搬运清零由 C 库接管。

> **图省事总结（三句话）**
> ① 复位后：硬件设SP(栈顶) → PC跳Reset\_Handler → 调SystemInit配时钟 → 跳\_\_main拷data/清bss → 进main()。
> ② PC = "接下来要跑哪条"的指针。`BLX`(带Link)=函数调用，存返回地址再跳、完了能回来；`BX`=纯跳转、一去不回。
> ③ SystemInit 两行：L133 先把 SystemInit 地址放进 R0；L134 `BLX R0` = 按 R0 地址跳过去执行 SystemInit，跑完回 L135。
> 一条核心：**"跳转=把PC改成目标地址"；"调用(BL)还额外存个返回地址好能回来"；压栈=存现场/返回地址，为的是事后回来。**

> **追问汇总：为什么压栈后 PC 去 SystemInit 而非 L135 的下一行？**
> ① PC 的"默认行为"只有一种：**普通指令执行后 PC +4 顺延到下一条**（如 L133→L134）。
> ② 但 **跳转指令会"覆盖"这个默认**：`BLX R0` = **PC ← R0(SystemInit地址)**，执行后 PC 不再顺延到 L136，而是被强制改去 SystemInit。**控制 PC 去哪的是"当条指令是普通还是跳转"**。
> ③ 所以 L134→SystemInit 不是"本该死守 L135 却跑去别处"，而是**跳转指令主动改变了 PC 目标**；顺延(L134→L135)在本就被 BLX 打断。
> 一句话记：**默认顺延 +4，跳转指令覆盖目标；BLX/BX 属于"覆盖"，所以压栈后 PC 去 SystemInit。**

### 1.4 伪指令词汇表

| 伪指令             | 含义                               | 是"摆放说明"还是"CPU 指令" |
| --------------- | -------------------------------- | ----------------- |
| EQU             | 给常量赋值/起名（`Stack_Size EQU 0x400`） | 摆放说明（伪指令）         |
| AREA            | 声明/开启一个段                         | 摆放说明（伪指令）         |
| DCD             | 占 4 字节并初始化（中断向量表每项用它）            | 摆放说明（伪指令）         |
| SPACE           | 预留 N 字节空间                        | 摆放说明（伪指令）         |
| EXPORT / IMPORT | 导出/导入符号，给链接器导航                   | 摆放说明（伪指令，不生成机器码）  |
| PROC / ENDP     | 子程序起止边界括弧，不执行动作                  | 摆放说明（伪指令，不生成机器码）  |

### 1.5 栈指针机制问答（易错点归纳）

**Q: 栈 / 堆的标签（Stack\_Mem / \_\_initial\_sp / \_\_heap\_base / \_\_heap\_limit）是什么？**
它们是段内存的**地址符号（标签，不是变量）**，标出内存范围：

| 符号             | 指向            | 作用                        |
| -------------- | ------------- | ------------------------- |
| `Stack_Mem`    | 栈区**底部**(低地址) | 栈 1KB 空间的起始               |
| `__initial_sp` | 栈区**顶部**(高地址) | 栈顶地址，放进向量表第 0 项，复位自动装入 SP |
| `__heap_base`  | 堆区**底部**(低地址) | 堆起始                       |
| `__heap_limit` | 堆区**顶部**(高地址) | 堆结束                       |

> 栈的"顶"=高地址(`__initial_sp`)，地址排序：`[Stack_Mem 低]──栈──[__initial_sp 高]`。栈满时 SP 从顶冲向底(低地址)。

**Q: 栈指针每次移动几个字节？是 DCD 的 4 字节吗？**
**不是固定 4 字节。** DCD 是"向量表放一个 4 字节数据"（编译期/静态定义），和运行时 SP 无关。SP 每次移动量 = **本次压入内容的实际字节数**：

| 入栈内容                       | 移动量      |
| -------------------------- | -------- |
| 返回地址(压 PC)                 | 4 B      |
| 保存 R0\~R3                  | 4×4=16 B |
| 保存 R4\~R11                 | 8×4=32 B |
| 压 1 个 uint8\_t             | 1 B      |
| 压 `uint8_t buf[256]`       | 256 B    |
| 中断压入 8 个现场(r4\~r11+r0\~r3) | 8×4=32 B |

**Q: "压 PC" 是什么意思？**
\= 把**当前 PC 的下一条指令地址**（即返回地址）存入栈，SP 下移 4 字节。执行 `BL/BLX` 时：先压 PC 存返回地址，再把 PC 设为子程序地址跳过去；子程序结束用 `BX LR`/`POP PC` 取回，回到调用处。**不压这个 PC，函数就不知道完成后回哪。**（见 1.2：`BLX SystemInit` 压入 L135 地址）

**Q: 是否"SP 移动到某个处理函数"？**
**不成立。** "跳到哪个函数"是 **PC** 的职责，和 **SP 无关**。SP 只指向"栈当前位置"，永不知晓函数；复位时：

- SP ← `__initial_sp`（栈顶内存，L37 符号 / L61 表项）
- PC ← 向量表\[1] = Reset\_Handler（跳函数，L62 表项）
  即 **L36 是 `Stack_Mem SPACE Stack_Size`（栈空间）→ L37 `__initial_sp` 是 SP 到栈顶；L62 是 PC 到 Reset\_Handler**，各管一事。

**Q: 导入/导出符号（EXPORT/IMPORT）干嘛用的？**
给**链接器**在"不同编译单元间连线"用，**不生成机器码**。

| 伪指令    | 作用              | 项目实例                                 |
| ------ | --------------- | ------------------------------------ |
| EXPORT | 把本文件符号对外开放      | `EXPORT Reset_Handler`，供向量表 `DCD` 引用 |
| IMPORT | 声明"要用别的文件定义的符号" | `IMPORT __main/SystemInit`，跳转到它们     |

> 相当于 C 语言的 `extern`：告诉链接器符号在哪儿 / 我要找哪个外部符号。

***

## 2. 中断向量表与 NVIC

### 2.1 向量表结构

- 第 0 项是什么？\
  回答：第0项存放的是**栈顶地址值 `__initial_sp`**，复位时硬件读取此项并载入 SP，作为 MSP 的初始栈顶。
- 表项地址怎么算？（提示：VTOR + 异常号 × 4）\
  回答：表项地址 = **VTOR + 异常号 × 4**（每个表项占 4 字节，故 ×4）。
- 异常号与 `IRQn` 的关系？\
  回答：`IRQn` 是库枚举给每个中断/异常起的编号。`IRQn < 0` 表示**系统异常**（如 `HardFault_IRQn = -13`）；`IRQn ≥ 0` 表示**外设中断**（如 `EXTI9_5_IRQn = 23`）。两者的向量表**表项号 = IRQn + 16**（前 16 项被栈顶 + 系统异常占满）。
- `EXTI5~9` 为什么共用同一个向量？\
  回答：外部引脚数量多于 EXTI 中断线数量，所以 5\~9 号引脚共用同一条中断线，在向量表中共用一个表项 `EXTI9_5_IRQHandler`，触发时进入同一个 ISR。

### 2.2 优先级的两个维度

| 维度          | 决定什么                            | 能否产生嵌套 |
| ----------- | ------------------------------- | ------ |
| 抢占优先级       | 决定"谁可以打断谁"：数值越小优先级越高，抢占优先级不同的中断才可互相打断 | 可以（仅当抢占优先级不同） |
| 响应优先级（子优先级） | 多个抢占优先级**相同**的中断同时挂起时，决定谁**先执行**（数值越小越先） | 不能（只定先后，不打断） |

### 2.3 优先级分组

- F1 的优先级字段是几位？→ 取值范围
- `抢占位宽 = ？ − PRIGROUP`
- 分组 0 / 2 / 4 各是几比几？

**答题参考：**

1. **字段位数**：F1 为 **4 位**（`configPRIO_BITS = 4`），取值范围 **0~15 共 16 级**。这 4 位可被"优先级分组"拆成两段：高几位 = 抢占，低几位 = 子优先级，拆法由 `AIRCR` 的 `PRIGROUP` 字段（3 位）决定。

2. **抢占位宽公式**（两套编号别混）：
   - **SPL 库分组号**（`NVIC_PriorityGroup_0~4`）：抢占位宽 = **分组号**，子位宽 = **4 − 分组号**
   - **AIRCR 的 PRIGROUP 字段值**（0~7）：抢占位宽 = **7 − PRIGROUP**
   - 两者关系：`PRIGROUP = 7 − 分组号`（如分组 4 对应 PRIGROUP = 3）。笔记公式中的 `？` = **7**

3. **分组 0 / 2 / 4 配比**：

   | 分组 | 抢占:子 | 含义 |
   | --- | --- | --- |
   | 分组 0 | **0 : 4** | 没有抢占位，所有中断只能按子优先级排队，谁都不能打断谁 |
   | 分组 2 | **2 : 2** | 抢占 0~3（4 级）× 子 0~3（4 级） |
   | 分组 4 | **4 : 0** | 只有抢占 0~15，无子优先级（FreeRTOS 强制用此分组） |

   > 记忆技巧：**分组号 = 抢占位数**。分组 N → 抢占 N 位、子 (4−N) 位。

4. **与项目/FreeRTOS 联系**：FreeRTOS 强制分组 4（4:0），因此手环 `NVIC_Init` 只需填**抢占优先级**一个值（0~15），子优先级参数填了也不生效；这与 2.4 的 FromISR 门槛（抢占 ≥ 5）配套。

### 2.4 与 FreeRTOS 的关系

- FreeRTOS 强制哪个分组？为什么？
- 项目对照：手环 `EXTI8_Config` 里填的两个优先级参数是多少？这个数值能不能调 `xQueueSendFromISR`？

**答题参考：任务优先级 vs 中断优先级（两套不同体系）**

| | 任务（FreeRTOS） | 中断（NVIC 硬件） |
| --- | --- | --- |
| 优先级维度 | **1 个**：`uxPriority` | **2 个**：抢占 + 子优先级 |
| 取值范围 | 0 ~ `configMAX_PRIORITIES-1`（本项目=32，即 0~31） | 0~15（F1 为 4 位，`configPRIO_BITS=4`） |
| 数值方向 | **越大越优先** | **越小越优先** |
| 同优先级处理 | 时间片轮转（本项目 `configUSE_TIME_SLICING=1` 已开） | 按子优先级排队；仍相同则按中断号 |
| 能否打断彼此 | 高优先级任务抢占低优先级任务 | 抢占优先级不同才可嵌套 |
| 由谁管理 | RTOS 调度器（软件） | 中断控制器（硬件） |

> 注意：任务没有"子优先级"维度。`xTaskCreate` 只填一个 `uxPriority` 参数。所谓"任务的抢占式调度"是 RTOS 的**调度策略**，不是任务的属性；NVIC 的"抢占优先级"才是中断的**硬件属性**，两者同名不同物。

**FreeRTOS 的两个约束（决定 2.4 答案）**

1. **强制 NVIC 优先级分组 4**（4 位全当抢占位、0 位子优先级）——FreeRTOS 只关心"这个中断能不能打断内核临界区"这个二值判断，把分组统一成全部抢占位可保证确定性；任何子优先级位都会干扰 `BASEPRI` 掩码的判定。
2. **FromISR 调用门槛**：本项目配置 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`，即硬件值 `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5<<4 = 80`。含义：**NVIC 优先级数值 ≥ 5（5~15 档）的中断才允许调 `xQueueSendFromISR` 等 FromISR API；0~4 档被禁止**（`port.c` 用 BASEPRI 把 0~4 屏蔽，违规触发 `configASSERT`）。

**项目对照结论**：`EXTI8_Config` 填的抢占优先级只要 **≥ 5**，就能在 ISR 里安全调 `xQueueSendFromISR`；填 0~4 则不行。内核自身（PendSV/SysTick）被设到最低优先级 15，保证任何合法中断都能抢占内核、且不被更高优先级中断骚扰。

***

## 3. 时钟树

### 3.1 主链路

- HSI / HSE → PLL → SYSCLK 的倍频关系
- 我板子上的实际值：HSE = \_\_\_\_\_\_ MHz，SYSCLK = \_\_\_\_\_\_ MHz

**答题参考（本项目实测值）：HSE = 8MHz，SYSCLK = 72MHz**

> 来源：[system_stm32f10x.c](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Start/system_stm32f10x.c#L1053-L1056) 的 `SetSysClockTo72()`：`RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLMULL9`，即 PLLCLK = HSE × 9 = 72MHz。

1. **HSI / HSE 区分**：HSI 是内部 RC 振荡器，约 8MHz（精度差，约 ±1%，出厂校准）；HSE 是外部晶振，频率由板载晶振决定（本项目 8MHz）。
2. **PLL 是倍频器，输出单一值**：输入源可选 HSI/2、HSE 或 HSE/2，乘上倍频系数（F103 为 2~16）得 PLLCLK。本项目 **PLLCLK = 8 × 9 = 72MHz**。PLL 只倍频、不分频，不存在"72 或 36 二选一"。
3. **正确链路**：
   ```
   HSE(8MHz) → PLLSRC(选HSE) → PLL ×9 → PLLCLK=72MHz → SYSCLK=72MHz(选PLL为源，不分频)
                                                         ├─ AHB ÷1 → HCLK=72MHz
                                                         │           ├─ APB2 ÷1 → PCLK2=72MHz
                                                         │           └─ APB1 ÷2 → PCLK1=36MHz
   ```
   **关键**：SYSCLK = PLLCLK = 72MHz（选 PLL 作系统时钟源时不分频）；"÷1" = 不分频、频率不变（72MHz 不会变 36MHz）；**36MHz 是 APB1 的总线时钟 PCLK1**（HCLK ÷2 得到），不是 SYSCLK 分频产物。

### 3.2 总线分频

| 总线   | 分频     | 频率     |
| ---- | ------ | ------ |
| AHB  | ÷1（不分频） | 72MHz |
| APB1 | ÷2 | 36MHz |
| APB2 | ÷1（不分频） | 72MHz |

> 来源：`SetSysClockTo72()` 中 `HPRE_DIV1`（[L1022](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Start/system_stm32f10x.c#L1022)）→ HCLK=72M；`PPRE2_DIV1`（[L1025](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Start/system_stm32f10x.c#L1025)）→ PCLK2=72M；`PPRE1_DIV2`（[L1028](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Start/system_stm32f10x.c#L1028)）→ PCLK1=36M。
>
> **记忆口诀：APB1 是低速总线、APB2 是高速总线。** 低速（APB1）才 ÷2 压到 36MHz；高速（APB2）÷1 保持 72MHz。
>
> **与 3.3 的伏笔**：APB1 ÷2（≠1），所以挂 APB1 上的定时器时钟会 ×2 补回 → 36MHz × 2 = 72MHz。

### 3.3 定时器时钟的特殊规则

- APB 分频 = 1 时，定时器时钟 = ？
- APB 分频 ≥ 2 时，定时器时钟 = ？
- 验证：算出我项目里 **APB1 上的定时器**实际跑多少 MHz

**答题参考（STM32F1 硬件规则）：**

| APB 分频 | 定时器时钟（TIMxCLK） | 原因 |
| --- | --- | --- |
| **= 1**（不分频） | = **APB 时钟 PCLKx** | 没被分频，无需补偿 |
| **≥ 2**（被分频） | = **2 × APB 时钟 PCLKx** | 硬件 ×2 把"被分掉的"补回，让定时器仍能跑满 72MHz |

> **一句话记忆：分频 = 1 跟 APB 走；分频 ≥ 2 翻倍补。**

**本项目验证：**
- APB2 ÷1 → PCLK2 = 72MHz → APB2 上的定时器（TIM1/TIM8）= **72MHz**（=APB 时钟）
- APB1 ÷2 → PCLK1 = 36MHz → APB1 上的定时器（TIM2~TIM7）= **2 × 36 = 72MHz**（触发 ×2 规则）

所以手环里挂 APB1 的定时器（如延时/定时用 TIM）实际跑 **72MHz**，与 APB2 定时器同速。

***

## 4. GPIO 八种模式

### 4.1 四种输入

| 模式   | 什么时候用  |
| ---- | ------ |
| 浮空输入 | 内部上/下拉都断开，电平完全由外部决定。用于外部信号源**自带强驱动**（对方推挽输出、或外部已有上/下拉电阻）的场景 |
| 上拉输入 | 内部上拉电阻，平时默认高。用于按键（一端接地：平时高、按下低）或读取开漏输出器件的电平 |
| 下拉输入 | 内部下拉电阻，平时默认低。项目实例：[Key.c L34/L39](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Key.c#L34-L39) 用 `GPIO_Mode_IPD`——按键另一端接 3.3V，平时低、按下变高 |
| 模拟输入 | 关闭数字输入缓冲（施密特触发器），引脚直接进 ADC 采样（模数转换） |

### 4.2 四种输出

- 正交的两个维度：**输出结构（推挽 vs 开漏）** × **是否复用（通用 GPIO vs 复用外设 AF）**
- 关键提问：**开漏"写 1"到底是什么意思？** I²C 为什么必须用开漏？\
  开漏只有下拉管（N-MOS）：**写 0** → 内部导通拉低；**写 1** → 输出管**截止/高阻**，引脚悬空，**必须靠外部上拉电阻**才显示高电平。I²C 用开漏的原因：① **线与（Wire-AND）**——任何设备拉低总线即整条总线为低，多设备共享不会两强输出打架短路；② **电平兼容**——上拉电阻接哪个电压域，总线就是哪个电平，可跨电压域；③ 支持**应答机制**（从机拉低 SDA = ACK）。项目实例：[Oled.c L24](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Oled.c#L24) 的 SCL/SDA（PB8/PB9）= `GPIO_Mode_Out_OD` + 外部上拉。
- F1 的 CNF 编码：**00 = 通用推挽 / 01 = 通用开漏 / 10 = 复用推挽 / 11 = 复用开漏**\
  记忆：**低位 bit0 = 是否开漏**（0 推挽、1 开漏）；**高位 bit1 = 是否复用**（0 通用、1 复用）。

### 4.3 复用功能的配置顺序

- 记下完整步骤
- 关键提问：**哪一步最容易漏？**（提示：和引脚本身无关的那个时钟）

**答题参考（完整五步）：**

| 步骤 | 内容 | 说明 |
| --- | --- | --- |
| ① | 使能 **GPIO 端口时钟** | 引脚所在端口（如 GPIOA/GPIOB，挂 APB2） |
| ② | 使能**复用外设自身的时钟** | 如 USART1 在 APB2、USART2/3 在 APB1、I2C/SPI 各自总线 |
| ③ | 使能 **AFIO 时钟**（`RCC_APB2Periph_AFIO`） | 只管**引脚重映射（Remap）**和 **EXTI 线选择** |
| ④ | 配置引脚模式：**复用推挽 AF_PP / 复用开漏 AF_OD** + 速度 | `GPIO_Init` 写入 CRL/CRH 寄存器 |
| ⑤ | 配置外设本身（如 `USART_Init`）并 Enable | 外设层，不属于 GPIO 层 |

**关键提问答案：最容易漏的是 ③ AFIO 时钟。**

> 提示"和引脚本身无关的那个时钟"——GPIO 时钟和引脚"有关"不会忘；**AFIO 是辅助功能时钟**，既不属于某个引脚、也不属于某个外设，专门负责重映射和 EXTI 线选择，名字抽象、职责隐蔽，**最容易漏**。漏了的表现：重映射的引脚不工作、EXTI 触发不了（对应 5.3 检查项"时钟使能（GPIO + AFIO）"）。
> 记忆锚点：一提到**重映射**或 **EXTI 线选择**，第一反应就是"要开 AFIO 时钟"。

### 4.4 项目对照

| 引脚 | 配置成什么模式 | 为什么 |
| --- | --- | --- |
| OLED：PB8(SCL)/PB9(SDA) | 开漏输出 `Out_OD` | 软件 I²C 需要**线与**，靠外部上拉电阻出高电平（[Oled.c L24](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Oled.c#L24)） |
| 传感器 I²C：PA11(SCL)/PA12(SDA) | 开漏输出 `Out_OD` | MAX30102/ADXL345 共用的软件 I²C（[xiic.h L23-28](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/xiic.h#L23-L28)） |
| MAX30102 INT：PA6 | 浮空输入 `IN_FLOATING` | 读传感器中断状态，芯片主动拉低产生中断，电平由外部决定（[MAX30102.c L17](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Max30102.c#L17)） |
| 按键：PB13/PB14/PB15、PA8 | 下拉输入 `IPD` | 按键另一端接 3.3V：平时低、按下变高（[Key.c L34-L42](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Key.c#L34-L42)） |
| LED：PA1/PA2 | 推挽输出 `Out_PP` | LED 需要**强驱动能力**灌/拉电流点亮（[Led.c L26-L29](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Led.c#L26-L29)） |
| DHT11：PA7 | 推挽输出（发信号）/ 浮空输入（读数据） | 单总线双向复用一根线：主机发起始信号用强驱动，读数据时切回输入由外部上拉决定电平（[DHT11.c L26-L38](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/DHT11.c#L26-L38)） |
| USART1：PA9(TX) | 复用推挽 `AF_PP` | 引脚交给 USART 外设控制输出（[usart.c L33](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/usart.c#L33)） |
| USART1：PA10(RX) | 浮空输入 `IN_FLOATING` | 接收方向，电平由对端决定（[usart.c L39](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/usart.c#L39)） |
| 控制脚：PB1/PB12 | 推挽输出 `Out_PP` | 按键模块内部控制信号（PB1 高、PB12 低）（[Key.c L44-L50](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Key.c#L44-L50)） |

***

## 5. EXTI 中断流程

### 5.1 从引脚到 ISR 的链路

按顺序写出信号经过的每一站，并标注在哪一层配置：

1. 外部信号到达**引脚**（配置成输入模式）—— **GPIO 层**（开时钟 + 输入模式）
2. 引脚 → **EXTI 线**：AFIO 把引脚映射到中断线（如 PA8 → EXTI8）—— **AFIO 层**（`GPIO_EXTILineConfig`）
3. EXTI 检测**触发边沿**（上升/下降/双边沿），置**挂起位 Pending**—— **EXTI 层**（`EXTI_Init` 选边沿）
4. **中断屏蔽判断**：IMR/EMR 放行后才向 NVIC 发请求—— **EXTI 层**（`EXTI_Init` 屏蔽位）
5. NVIC **优先级仲裁**，响应后查向量表取表项—— **NVIC 层**（`NVIC_Init`）
6. 跳转执行 **ISR**（先清 Pending 再干活）—— **用户代码**（stm32f10x_it.c）

> **一句话版**：`引脚 →(AFIO映射)→ EXTI线 →(边沿检测,置Pending)→(屏蔽放行)→ NVIC仲裁 → 向量表 → ISR`。
> 注意：信号不是"线直接连 NVIC"，中间隔着 **EXTI 控制器**（边沿检测 + 屏蔽 + Pending 都在它里面）。

### 5.2 配置六步

1. 使能 **GPIO 时钟**（引脚所在端口）
2. 使能 **AFIO 时钟**（EXTI 线选择需要，NVIC 没有时钟概念）
3. `GPIO_EXTILineConfig`：**绑定**引脚 → EXTI 线（如 PA8 → EXTI8）
4. `EXTI_Init`：**模式 + 触发边沿 + 使能**（`EXTI_Mode_Interrupt` / `EXTI_Trigger_Rising` / `ENABLE`）
5. `NVIC_Init`：**通道 + 优先级 + 使能**（`EXTI9_5_IRQn`，抢占/子优先级）
6. 写 **ISR**（`EXTI9_5_IRQHandler`）：**先清 EXTI Pending 再干活**

> **易错点**：EXTI 的 GPIO 是**输入模式**（如 `IPD`），**不是复用模式**——打断"引脚=AF 模式"的惯性（AF_PP/AF_OD 是给 USART/SPI 等外设用的，与 EXTI 无关）。

### 5.3 项目对照：EXTI8\_Config 逐条核对

| 检查项                | 我的配置   | 是否正确   |
| ------------------ | ------ | ------ |
| 时钟使能（GPIO + AFIO）  | GPIOA + AFIO 均使能（[Key.c L29-31](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Key.c#L29-L31)） | ✅ |
| EXTI 线映射           | `GPIO_EXTILineConfig(GPIOA, Pin8)` → PA8→EXTI8（[Key.c L66](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Key.c#L66)） | ✅ |
| 触发边沿               | `EXTI_Trigger_Rising` 上升沿（[Key.c L69](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Key.c#L69)） | ✅（按键接 3.3V，按下=上升沿） |
| EXTI 中断屏蔽          | `EXTI_Line8` + `EXTI_Mode_Interrupt` + `ENABLE`（[Key.c L67-71](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Key.c#L67-L71)） | ✅ |
| NVIC 优先级           | `EXTI9_5_IRQn`，抢占 0 / 子 0（[Key.c L73-76](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/Hardware/Key.c#L73-L76)） | ⚠️ 与 FreeRTOS 冲突 |
| ISR 里清 pending 的顺序 | **最后才清** `EXTI_ClearITPendingBit`（[stm32f10x_it.c L186](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/User/stm32f10x_it.c#L186)），且 ISR 里做 OLED 重活 | ❌ 应先清再干活 |

- ISR 里先做什么、后做什么？（提示：清 pending 与干活的先后）
- ISR 里为什么不能 `vTaskDelay`？

**答题参考：**

1. **ISR 先后顺序**：**先清 pending 再干活**，且只做**轻体量**工作（如 `xQueueSendFromISR` 丢事件，复杂显示交给任务）。**反例**：本项目 ISR 里 `OLED_Clear()+OLED_ShowString()` 是重活、pending 又最后才清——若干活期间同线再来一个边沿，会在最后被误清掉 → **丢失中断**。正确写法：ISR 只发事件 + 最前面清 pending。
2. **ISR 不能 `vTaskDelay`**：`vTaskDelay` 依赖当前任务的 TCB 和调度器上下文，ISR 里**没有"任务上下文"**；ISR 只能调用 **FromISR 后缀**的 API（`xQueueSendFromISR` 等）。在 ISR 调任务级 API 会破坏内核状态，configASSERT 开启时直接断言。

**⚠️ 两个项目隐患（对照发现）：**

1. **NVIC 抢占优先级 0 + `xQueueSendFromISR` = 冲突**：项目配置 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`，只有**优先级 ≥ 5** 的中断才允许调 FromISR API（见 2.4）。抢占 0 在 0~4 禁区内：内核临界区用 BASEPRI=0x50 屏蔽不了它 → 会打断临界区、破坏内核数据结构。目前没崩是因为 [FreeRTOSConfig.h L300](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/FreeRTOS/FreeRTOSConfig.h#L300) 的 **configASSERT 被注释掉**掩盖了隐患。**建议：EXIT 抢占优先级改为 ≥ 5（如 5）。**
2. **ISR 做 OLED 重活**：违背"轻体量"原则，OLED 显示应交给任务，ISR 只负责 `xQueueSendFromISR` 发事件。

### 5.4 SysTick 与 xPortSysTickHandler（FreeRTOS 心跳）

**xPortSysTickHandler 在哪：** 定义在 [port.c L427](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/FreeRTOS/PORT/port.c#L427)（`FreeRTOS/PORT/port.c`，Cortex-M3 移植层），声明在 L119。它由项目的 `SysTick_Handler` 调用。

**调用链：**

```
SysTick 异常 → 向量表 → SysTick_Handler()   ← [stm32f10x_it.c L142-151]
                 └─ if 调度器已启动 → xPortSysTickHandler()
```

> [FreeRTOSConfig.h L345](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/FreeRTOS/FreeRTOSConfig.h#L345) 有被注释的 `#define xPortSysTickHandler SysTick_Handler`——那是另一种接法（宏替换中断名），本项目用的是手动转发（写 `SysTick_Handler` 再调它）。

**函数做什么（tick 推进三步）：**

| 步骤 | 代码 | 作用 |
| --- | --- | --- |
| ① | `vPortRaiseBASEPRI()` | 抬高 BASEPRI，屏蔽低优先级中断，进入保护 |
| ② | `xTaskIncrementTick()` | 推进 RTOS tick：更新就绪表 / 延时任务 / 时间片轮转 |
| ③ | 若返回 `pdFALSE` 之外 | `portNVIC_PENDSVSET_BIT` 置位 → **挂起 PendSV 触发任务切换** |
| ④ | `vPortClearBASEPRIFromISR()` | 恢复中断掩码 |

**一句话**：SysTick 每 1ms（`configTICK_RATE_HZ` 决定）触发一次中断，`xPortSysTickHandler` 让内核"心跳"跳一下——需要切换时并不直接切换，而是**挂起 PendSV**，等当前中断退出后再在 PendSV 里完成切换（这样切换发生在安全时刻）。

### 5.5 工程属性核对（uvprojx / map / 目录）

> 核对时间：2026-10-05。数据全部从 `手环项目.uvprojx`、`Listings/手环项目.map`、根目录实读。

**MCU 与启动文件：** `STM32F103C8`（[uvprojx L17](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/手环项目.uvprojx#L17)）；启动文件 `startup_stm32f10x_md.s`（[uvprojx L542-544](file:///e:/TongYongFiles/Keil_v5毕设备份/proj1/智能手环操作系统版本/手环项目.uvprojx#L542-L544)）；Flash 驱动 `STM32F10x_128.FLM`（L24）。

**构建工具链：**

| 项 | 值 | 位置 |
| --- | --- | --- |
| 工具链 | **Keil MDK**（`ToolsetNumber` = 0x4） | uvprojx L11 |
| 编译器 | **ARMCC V5.06 update 7 (build 960)** | uvprojx L13 `pCCUsed` |
| 优化等级 | **-O1**（`<Optim>` = 1） | uvprojx L316 |

**RTOS 配置 6 项（FreeRTOSConfig.h 实测）：** `configUSE_PREEMPTION=0`（协作式）／`configUSE_TIME_SLICING=1`／`configUSE_TIMERS=0`／`configUSE_IDLE_HOOK=0`／`configTOTAL_HEAP_SIZE=10KB`／`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`。

**代码规模口径（⚠️ 易混）：**

| 口径 | 数值 | 来源 |
| --- | --- | --- |
| 整工程 `Total RO Size`（Code + RO Data，含标准库/内核） | **34676B = 33.86kB** | map L3589 |
| 整工程 `Total RW Size`（RW + ZI） | 17312B = 16.91kB | map L3590 |
| 自研代码（User/+Hardware/+System/） | 需按 .map 各目标段累计 | 待算 |

> ⚠️ "33.86kB" 是**整工程**口径，**不含"只统计自己写的代码"**——填写架构文档第 1 节时两者要分开写。

**顶层目录清单（根目录实测，排除产物/配置）：** 核心 6 个 = `User/` `Hardware/` `System/` `Start/` `LIbrary/` `FreeRTOS/`；另有 `app/` `Docs/`。产物目录（排除）：`build/` `Objects/` `Listings/` `DebugConfig/`；配置目录：`.eide/` `.vscode/`。

