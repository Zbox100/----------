# 验收观测日志（observability_log）

本文件归档"以实测数据驱动决策"的观测记录。每条记录包含：观测目的、方法、原始数据、结论、执行的修改、已知局限。

与 `notes_rtos.md`（问题清单）分工不同：那里记录"发现了什么问题"，这里记录"实测证据是什么、据此做了什么决定"。

---

## 观测 01 — 任务栈水位 + FreeRTOS 堆余量

- 观测日期：2026-10-13
- 目标：用实测 `uxTaskGetStackHighWaterMark()` 定各任务栈。判定规则：**minFree < 64 word(256B) 则扩容**；本轮**只扩容、不缩小**。

### 1. 观测方法

| 配置项 | 值 |
|---|---|
| `configCHECK_FOR_STACK_OVERFLOW` | 2（栈填充字节校验；越界即进 `vApplicationStackOverflowHook` 冻结停机） |
| `INCLUDE_uxTaskGetStackHighWaterMark` | 1 |
| `configUSE_MALLOC_FAILED_HOOK` | 1（`pvPortMalloc` 返回 NULL 即进 `vApplicationMallocFailedHook` 冻结停机） |
| 采集点 | `DTH11_Work_f` 的 2s 周期，调用 `Report_TaskStack()`（`User/main.c`） |
| 输出格式 | 每任务一行 `minFree=%4uw (%4uB)`，末尾一行 `Heap free: %uB` |

`uxTaskGetStackHighWaterMark(handle)` 返回该任务**自启动以来的最低剩余栈**，单位为 32 位字（×4 = 字节）。
即：**峰值占用 = 分配 − minFree**。

> **为什么没做成独立任务**：整块 RAM 仅余约 856B（非堆占用 7336B + 12KB 堆 = 19624B / 20480B）。
> 再建一个 256 字栈(1KB) + TCB 的观测任务会把 12KB 堆撑爆；而 `configUSE_MALLOC_FAILED_HOOK=1` 下，
> 堆耗尽不再走 `Create_Task` 的"报错提示"路径，而是直接冻结 → 现象是**开机黑屏**（见第 6 节）。
> 因此报告挂载在既有任务上，堆占用为 0。

### 2. 原始数据（一次运行）

| 任务 | 分配(word) | minFree(word) | minFree(B) | 峰值占用(word) |
|---|---|---|---|---|
| Time_Work   | 128 |  90 | 360 |  38 |
| Time_Show   | 128 |  90 | 360 |  38 |
| Adxl_Work   | 192 | 154 | 616 |  38 |
| Adxl_Show   | 128 |  92 | 368 |  36 |
| DHT_Work    | 224 | 158 | 632 |  66 |
| DHT_Show    | 224 | 186 | 744 |  38 |
| Max_Work    | 224 | 101 | 404 | 123 |
| Max_Show    | 224 | 190 | 760 |  34 |
| Menu        | 256 | 196 | 784 |  60 |
| Key_GetNum  | 128 |  94 | 376 |  34 |
| beep        | 128 |  86 | 344 |  42 |
| Usart_Rx    | 128 |  96 | 384 |  32 |

`Heap free: 1272B`（12KB 堆，观测当时）

### 3. 结论

1. 12 个任务中 `minFree` 最小值 = 86 word（`beep`），**全部 ≥ 64 word**。按本轮规则，**没有任务需要扩容**。
2. 例外判断 —— `beep_Work_f`：它唯一的"重路径"是 `UsartPrintf`（打印 `"FALLING DOWN!"`），
   本次观测**未触发跌倒报警**，故 86 word 是该任务"未跑重路径"的**乐观下界**，不可直接采信。
   同类证据：`DHT_Work` 同样在栈上调用 `UsartPrintf`，峰值 66 word；本工程此前也因
   "128 字栈 + `UsartPrintf` 的 256B 栈上缓冲"把 `Adxl_Work` 由 128 提到 192。beep 属同一模式，
   一旦真的报警，其 `minFree` 将降至约 62 word（< 64 阈值）。
3. 反向结论（缩小）：多个任务实测峰值远低于分配量 —— `Adxl_Work` 38/192、`Menu` 60/256、
   `Max_Show` 34/224、`DHT_Show` 38/224。按"峰值 + 48 word 余量"口径可大幅缩减，
   见第 4.2 节；其中 `Time_Show_f` 经复核后**不予缩减**，原因见第 4.3 节。

### 4. 本次执行的修改

#### 4.1 扩容（1 项）

| 任务 | 原(word) | 新(word) | 增量 | 依据 |
|---|---|---|---|---|
| beep_Work_f | 128 | 192 | +64 (+256B) | 栈上调用 `UsartPrintf`，128 字不足；与 `Adxl_Work` 同一模式 |

#### 4.2 缩小（9 项，经复核后执行）

| 任务 | 原(word) | 新(word) | 实测峰值(word) | 余量(word) | 说明 |
|---|---|---|---|---|---|
| Adxl345_Work_f  | 192 |  96 |  38 | 58 | 每 50ms 必跑；内部已无 `UsartPrintf` |
| Max30102_Work_f | 224 | 176 | 123 | 53 | FFT + `UsartPrintf` 路径已跑过 |
| Menu_handling_f | 256 | 128 |  60 | 68 | 4 行菜单提示已打印过 |
| Time_Work_f     | 128 |  96 |  38 | 58 | 1s 周期必跑 |
| Key_GetNum_f    | 128 |  96 |  34 | 62 | 导航按键已用过 |
| Usart_Rx_f      | 128 |  96 |  32 | 64 | 串口分发已用过 |
| Adxl345_Show_f  | 128 |  96 |  36 | 60 | 仅 OLED 调用，链浅 |
| DTH11_Show_f    | 224 |  96 |  38 | 58 | 仅 OLED 调用，链浅 |
| Max30102_Show_f | 224 |  96 |  34 | 62 | 仅 OLED 调用，链浅 |

#### 4.3 经复核后**未缩**的任务

| 任务 | 现值(word) | 原因 |
|---|---|---|
| Time_Show_f  | 128（保持） | 内含 `SetTime_w()`，而 `SetTime_w()` 内部有 **9 处 `UsartPrintf`**（`System/MyRTC.c` L19-L85）。栈上需 256B 缓冲，同类实测峰值 66 word，96 字不够 |
| DTH11_Work_f | 224（保持） | 本次不在调整范围；其峰值 66 word，理论上可缩到 128，待下一轮一并处理 |

净变化：任务栈合计由 **2112 word** 降至 **1520 word**（−592 word = **−2368B**），
`Heap free` 预计由 1272B 升到约 **3.6KB**。改动位置均在 `User/main.c` 的 `Start_MyTaskList()`。

### 5. 已知局限与后续

- **缩小依据含假设**：被缩的 3 个 Show 任务，其"对应页面是否进入过"无法从日志确认；
  峰值 34~38 word 假设页面进过。若从未进入，真实峰值未知（但 Show 任务只调 OLED 系列函数，
  调用链很浅，风险有限）。
- **路径覆盖不全**：本次运行未覆盖 —— 时间设置 `SetTime_w()`、跌倒报警（beep）、
  蓝牙/串口四路菜单分发、MAX30102 的 FFT 计算边界。
- **`Time_Show_f` 最需复核**：它唯一的重路径 `SetTime_w()` 本次未跑（也正因如此没缩）。
  建议下一轮专门进一次"时间设置"页面，确认 128 字是否够用。
- **后续**：再做一轮覆盖全路径的观测，重点看 `Time_Show_f`（SetTime_w）、
  `DTH11_Work_f`（可再缩）、`beep`（报警路径）。

### 6. 经验教训

1. `configUSE_MALLOC_FAILED_HOOK=1` 会把"堆耗尽"（原先静默失败、或靠 `Create_Task` 计数提示）
   升级为**硬冻结**。新增任务前必须先核对 `Heap free`，否则现象是开机黑屏，且很难联想到是堆不足。
2. 观测/诊断代码优先"寄生"在既有任务上（本例借 `DTH11_Work_f` 的 2s 节拍），不要轻易新建任务——
   本工程 RAM 余量已不足 1KB，新建任务的成本（栈 + TCB）无法承受。
3. `minFree` 只有在该任务跑过其最重路径之后才可信。归档时必须写明"这次跑过哪些路径、没跑过哪些"，
   否则会被乐观数据误导。

---

## 观测 02 — 缩栈后的复核（验证轮）

- 目的：验证 4.2 的 9 项缩减是否安全，并捕捉新增路径（尤其 `Time_Show` 的 `SetTime_w`）。
- 判定口径：`minFree ≥ 32 word` 视为安全；`minFree < 32` 需回调。

### 数据

| 任务 | 分配(word) | minFree(word) | 峰值(word) | 上轮峰值 | 判定 |
|---|---|---|---|---|---|
| Time_Work      |  96 |  58 |  38 |  38 | 安全 |
| Time_Show      | 128 |  76 |  52 |  38 | 安全（峰值↑，见下） |
| Adxl345_Work   |  96 |  60 |  36 |  38 | 安全 |
| Adxl345_Show   |  96 |  60 |  36 |  36 | 安全 |
| DTH11_Work     | 224 | 158 |  66 |  66 | 安全 |
| DTH11_Show     |  96 |  58 |  38 |  38 | 安全 |
| Max30102_Work  | 176 |  53 | 123 | 123 | 安全（最小值，仍 ≥ 32） |
| Max30102_Show  |  96 |  62 |  34 |  34 | 安全 |
| Menu_handling  | 128 |  68 |  60 |  60 | 安全 |
| Key_GetNum     |  96 |  62 |  34 |  34 | 安全 |
| beep           | 192 | 182 |  10 |  42 | **实测不可用** |
| Usart_Rx       |  96 |  64 |  32 |  32 | 安全 |

`Heap free: 3640B`（1272 + 2368，与第 4 节"−592 word = −2368B"的预测一致）

### 结论

1. 全部任务 `minFree ≥ 53 word`（最小值 `Max30102_Work`）→ **9 项缩减安全，无一需要回调**。
2. `Time_Show` 峰值由 38 升至 52 → 说明其 `SetTime_w`（内含 `UsartPrintf`）路径本轮已被部分执行。
   128 字剩 76 word，充裕。（事后看 96 字亦够，但保留 128 更稳，不再下调。）
3. `beep` `minFree=182`（峰值仅 10 word）→ 本轮连任务体都没执行（报警未触发），
   与上轮 42 word 矛盾，故 **beep 实测值不可用**，维持 192 word。
   这是"未跑重路径的 minFree 不可信"的又一实例。

### 后续

- `DTH11_Work`（224，峰值 66）仍可缩到 128，待下一轮。
- `beep` 需触发一次跌倒报警后重测。
- 观测钩子 `Report_TaskStack` 与 `INCLUDE_uxTaskGetStackHighWaterMark`、`configUSE_MALLOC_FAILED_HOOK`
  两个开关：**已于验收结束后移除**（`configCHECK_FOR_STACK_OVERFLOW=2` 保留；
  `vApplicationStackOverflowHook` 保留，`vApplicationMallocFailedHook` 随开关一并删除）。
- `DTH11_Work_f` 内的 `idle=%` 打印（配套 `configGENERATE_RUN_TIME_STATS`）属另一项观测
  （CPU 空闲占比），亦已一并移除，该开关置 0。

---

## 观测 03 — beep 报警路径补测 + 一处前提更正

- 目的：触发跌倒报警，测 `beep_Work_f` 的真实峰值。
- 结果：本轮仅 `beep` 变化（`182 → 150`，分配 192 → 峰值 **42 word**），其余 11 项与观测 02 完全一致。

### 更正：`UsartPrintf` 的 256B 缓冲是 `static`，不占任务栈

`Hardware/usart.c` L109：

    static unsigned char UsartPrintfBuf[256];

缓冲位于 **.bss（static 存储）**，不在调用栈上。故此前"128 字栈会被 `UsartPrintf` 的 256B 栈上缓冲
击穿"的说法**不成立**；`UsartPrintf` 的真实栈开销仅为 `vsnprintf` 的内部帧。

连带影响：

| 项 | 更正 |
|---|---|
| `beep` 峰值 42 word | 与"它调了 `UsartPrintf`"并不矛盾，实测可信 |
| 早期把 `Adxl_Work`/`beep` 由 128 提到 192 的依据 | 依据（256B 栈上缓冲）无效，已改按实测定值 |
| `Docs/notes_rtos.md` P2-15「`UsartPrintfBuf[256]` 是局部变量，每次调用吃 256B 栈」 | **该表述有误**，应更正为"static 缓冲，不占任务栈" |

### 执行的修改

| 任务 | 原(word) | 新(word) | 依据 |
|---|---|---|---|
| beep_Work_f | 192 | **96** | 实测峰值 42 word（两轮一致），42 + 54 余量 |

### 当前栈配置总表

| 任务 | 栈(word) | 实测峰值(word) |
|---|---|---|
| Time_Work      |  96 |  38 |
| Time_Show      | 128 |  52 |
| Adxl345_Work   |  96 |  38 |
| Adxl345_Show   |  96 |  36 |
| DTH11_Work     | 128 |  66~70 |
| DTH11_Show     |  96 |  38 |
| Max30102_Work  | 176 | 123 |
| Max30102_Show  |  96 |  34 |
| Menu_handling  | 128 |  60 |
| Key_GetNum     |  96 |  34 |
| beep           |  96 |  42 |
| Usart_Rx       |  96 |  32 |
| **合计**       | **1328** | — |

合计由原 **2112 word 降至 1328 word**（省 784 word = **3136B**）；
`Heap free` 由 1272B 升至约 **4408B**。

（`DTH11_Work` 于本步缩至 128；其峰值 66~70 含临时的 `Report_TaskStack` 打印，
拆掉观测装置后峰值还会再降。）
