# 阶段 3 步 2（B2）：退出消息事件组广播改造

> 适用工程：STM32F103C8 + FreeRTOS（协作式调度，`configUSE_PREEMPTION=0`）
> 文档日期：2026-10-22
> 修订：2026-10-22 验收后拆除全部临时诊断（`Page_ExitSeen`、`dbg_exit_seen[]`、`exit_counted`、RX/寄存器打印），并补充“页面态丢弃 1~4 + 离页清空菜单队列”的防自动跳页处理。
> 相关文件：`LIbrary/head.h`、`User/main.c`、`User/stm32f10x_it.c`、`System/MyRTC.c`

## 1. 摘要

把“退出页面”从**单个全局标志 `g_exit_req`（谁清谁得）**改为 **FreeRTOS 事件组的位广播**，并把页面切换从 `vTaskResume / vTaskSuspend` 改为**页面位选择**。改造后：

- 4 个 Show 任务常驻，全部阻塞在同一个 `PageEventGroup` 上；
- 一次退出请求只置一个 `EVT_EXIT` 位，4 个任务都能观察到，不再出现“只有一个能收到”；
- 事件组同时承担“当前哪个页面在前台”的状态表示（4 个页面位）。

## 2. 背景

### 2.1 改造前的结构

| 环节 | 实现 |
|---|---|
| 退出源 1 | `EXTI9_5_IRQHandler` 里直接清屏 + `g_exit_req = 1` |
| 退出源 2 | `Usart_Rx_f` 收到 `0x00` 时清屏 + `g_exit_req = 1` |
| 消费方 | 4 个 Show 任务各自在循环顶部轮询 `g_exit_req`，读到后清零并 `vTaskSuspend(NULL)` |
| 页面切换 | `Menu_handling_f` 调 `vTaskResume(目标页)`，并挂起 `Key_GetNum_f` 与自己 |
| 启动 | 4 个 Show 任务创建后立即被挂起 |

### 2.2 存在的问题

1. **单消费语义**：`g_exit_req` 只有一个标志，谁先清谁得。历史上用长度 1 的 `KeyExitHandler` 队列时，4 个任务抢 1 条队列，只有一个能收到。
2. **挂起任务收不到广播**：3 个 Show 任务处于 `vTaskSuspend`，任何内核对象都无法唤醒它们，因此“4 个都收到”在物理上不可能。
3. **中断里跑软 IIC**：退出 ISR 直接调用 `OLED_Clear()` / `OLED_ShowString()`，位带时序在中断中执行，时间长且不可调度。
4. **残留标志**：菜单态按退出也会置位 `g_exit_req`，菜单不消费，导致下一次进入页面立刻被弹回。

### 2.3 目标与验收

- 交付：退出消息从“单消费标志”改为**事件组位广播**；
- 验收①：任意页面按退出，前台页正确退页；改造期间用一次性计数确认过 4 个任务都观察到该位（计数为临时诊断，验收后已拆除）；
- 验收②：退出后位被清除，连按两次不重复触发；
- 验收③：每个页面（时间/计步/温湿度/血氧）都能正常进入与退出。

## 3. 方案选型

### 3.1 事件组 vs 任务通知

| 维度 | 事件组 | 任务通知（逐个 give） |
|---|---|---|
| 广播 | 单对象、多等待者，天然广播 | 发送端需遍历 4 个任务句柄 |
| 事件类型 | 24 个可用位，可表达多种事件 | 每任务一个计数值，多事件会合并且无法区分 |
| ISR 置位 | 本版本 `xEventGroupSetBitsFromISR` 需开启定时器守护任务 | `vTaskNotifyGiveFromISR` 直接可用 |
| 状态机扩展 | 位可直接扩展为状态/事件集合 | 需额外全局变量配合 |
| 堆占用 | 事件组约 40~50B | 0 |

选择**事件组**的原因：广播语义、状态集中、后续状态机可直接扩位。

### 3.2 为什么没有开启定时器守护任务

`event_groups.c` 中 `xEventGroupSetBitsFromISR` 的编译条件是：

```c
#if ( ( configUSE_TRACE_FACILITY == 1 ) && ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) )
```

本工程这三项均为 0（`configUSE_TIMERS=0`）。开启它们会新增定时器守护任务（约 +1.3KB 堆）。因此采用**中继方案**：

```
EXTI ISR → xQueueSendFromISR(UsartRxHandler, 0x00) → Usart_Rx_f（任务上下文）→ xEventGroupSetBits
```

不新增任务、不开启定时器，物理键与蓝牙 `0x00` 天然统一到同一条路径。

## 4. 设计

### 4.1 位定义

`LIbrary/head.h`：

```c
#define EVT_EXIT        ( ( EventBits_t ) 0x0001U )   /* 退出请求（事件位） */
#define EVT_PAGE_TIME   ( ( EventBits_t ) 0x0002U )   /* 时间页在前台（状态位） */
#define EVT_PAGE_STEP   ( ( EventBits_t ) 0x0004U )   /* 计步页在前台 */
#define EVT_PAGE_DHT    ( ( EventBits_t ) 0x0008U )   /* 温湿度页在前台 */
#define EVT_PAGE_MAX    ( ( EventBits_t ) 0x0010U )   /* 血氧页在前台 */
#define EVT_PAGE_ALL    ( EVT_PAGE_TIME | EVT_PAGE_STEP | EVT_PAGE_DHT | EVT_PAGE_MAX )

extern EventGroupHandle_t PageEventGroup;
```

本工程 `configUSE_16_BIT_TICKS = 0`，`EventBits_t` 为 32 位；高 8 位被内核作为控制位，**可用 bit0~bit23**。

### 4.2 事件位与状态位的区别

| 类型 | 位 | 语义 | 置位者 | 清除者 |
|---|---|---|---|---|
| 事件位 | `EVT_EXIT` | “请求退出”（电平，处理完必须清） | `Usart_Rx_f` | 前台页面任务 |
| 状态位 | `EVT_PAGE_*` | “谁在前台”（同一时刻最多一个） | `Menu_handling_f` | 前台页面任务（退页时） |

区分原因：事件组**不计数**，同一个位重复置 1 只等于“仍然亮着”。因此退出位必须由处理它的前台页面在退页时清除；连按第二次时会因为页面位已被清而被 `Usart_Rx_f` 直接忽略。

### 4.3 参与者与读写权限

| 参与者 | 写 | 读 | 说明 |
|---|---|---|---|
| `Menu_handling_f` | 清 `EVT_PAGE_ALL \| EVT_EXIT`，置单个页面位 | — | 唯一的页面选择者 |
| `Usart_Rx_f` | 置 `EVT_EXIT` | `EVT_PAGE_ALL` | 只在页面态置位；菜单态忽略 |
| 4 个 Show 任务 | 前台页清 `页面位 \| EVT_EXIT` | `页面位 \| EVT_EXIT` | 同一份模板代码 |
| `SetTime_w` | — | `EVT_EXIT` | 编辑中途退出 |

每个位都只有一个主要写者，清除规则唯一；事件组 API 内部已用临界区保护，因此不需要额外互斥量。

### 4.4 任务模型

- 4 个 Show 任务**常驻**、同优先级 4、后台时阻塞在 `portMAX_DELAY`；
- `Menu_handling_f` 是唯一“导演”，进入页面后挂起自己与 `Key_GetNum_f`；
- 页面退出时由前台页调用 `Page_LeaveCommon()`：清空菜单残留指令，再恢复 Menu 与 Key；
- `beep_Work_f` 仍在启动时挂起（与本次改造无关）。

## 5. 公共函数

`User/main.c`：

```c
static EventBits_t Page_Wait(EventBits_t page_bit, uint8_t active, TickType_t active_timeout)
{
    EventBits_t mask = active ? EVT_EXIT : (page_bit | EVT_EXIT);

    return xEventGroupWaitBits(PageEventGroup,
                               mask,
                               pdFALSE,   /* 不在退出时清位：广播要求所有等待者都能看到 */
                               pdFALSE,   /* 任一位满足即可返回 */
                               active ? active_timeout : portMAX_DELAY);
}
```

```c
static void Page_Enter(EventBits_t page_bit, TaskHandle_t page_task)
{
    if(page_task == NULL) return;
    xEventGroupClearBits(PageEventGroup, EVT_PAGE_ALL | EVT_EXIT);
    xEventGroupSetBits(PageEventGroup, page_bit);
    if(Key_GetNum_fHandler != NULL) vTaskSuspend(Key_GetNum_fHandler);
    vTaskSuspend(NULL);
}
```

```c
static void Page_LeaveCommon(void)
{
    uint8_t dummy;

    /* 页面期间 Menu 处于挂起,积压的 1~4 会在回菜单后被当成新指令 → 自动跳页 */
    while(xQueueReceive(UsartMenuHandler, &dummy, 0) == pdTRUE) { }

    vTaskResume(Menu_handling_fHandler);
    vTaskResume(Key_GetNum_fHandler);
}
```

> 改造期间曾用 `Page_ExitSeen()` + `dbg_exit_seen[]` 做“4 个任务都收到”的一次性计数，验收完成后已按计划删除。

## 6. 页面任务的统一模板

以计步页为例（时间/温湿度/血氧三页结构相同，只换页面位与超时）：

```c
while(1)
{
    EventBits_t bits = Page_Wait(EVT_PAGE_STEP, active, pdMS_TO_TICKS(50));

    if(bits & EVT_EXIT)
    {
        if(bits & EVT_PAGE_STEP)          /* 只有前台页面执行退页 */
        {
            xEventGroupClearBits(PageEventGroup, EVT_PAGE_STEP | EVT_EXIT);
            global_var = 0;
            OLED_Clear();
            frame_drawn = 0;
            Page_LeaveCommon();
            vTaskSuspend(beep_Work_fHandler);   /* 仅计步页需要 */
            active = 0;
            continue;
        }
        vTaskDelay(1);                    /* 后台页：让出 1 tick，避免位未清时忙循环 */
        continue;
    }

    if(!(bits & EVT_PAGE_STEP)) { active = 0; continue; }

    active = 1;
    /* 本页的绘制/取数逻辑 */
}
```

各页参数：

| 页面 | 页面位 | 前台超时 | 附加动作 |
|---|---|---|---|
| 时间 | `EVT_PAGE_TIME` | 100ms | 调用 `SetTime_w` |
| 计步 | `EVT_PAGE_STEP` | 50ms | 挂起 beep、复位报警引脚 |
| 温湿度 | `EVT_PAGE_DHT` | 200ms | — |
| 血氧 | `EVT_PAGE_MAX` | 200ms | — |

## 7. 运行流程

### 7.1 进入页面

```
Menu 收到选择
  └─ ClearBits(EVT_PAGE_ALL | EVT_EXIT)      // 清掉上一轮残留
  └─ SetBits(目标页面位)                      // 点亮聚光灯
  └─ 挂起 Key_GetNum_f 与 Menu 自己
        └─ 清单里包含该页面位的 Show 任务被唤醒
              └─ active = 1，开始画屏/取数
```

### 7.2 页面内循环

- 前台：`Page_Wait` 只等 `EVT_EXIT`，超时值取本页刷新周期；
- 超时返回时位图里页面位仍亮，继续刷新；
- 后台：`Page_Wait` 等 `页面位 | EVT_EXIT`，超时为 `portMAX_DELAY`。

### 7.3 退出广播

```
按下退出键
  └─ EXTI9_5_IRQHandler：清挂起位 → xQueueSendFromISR(UsartRxHandler, 0x00)
        └─ Usart_Rx_f：取到 0x00
              └─ 若 (GetBits & EVT_PAGE_ALL) != 0 → SetBits(EVT_EXIT)
                    ├─ 前台页：清 页面位|EVT_EXIT → 清屏 → Page_LeaveCommon() → active=0
                    ├─ 后台页：vTaskDelay(1) → 继续睡（只被唤醒，不做退页）
                    └─ Menu：被恢复 → 重画菜单
```

蓝牙 `0x00` 走同一条 `Usart_Rx_f` 路径。

### 7.4 时间修改中退出

`SetTime_w` 的等待循环每 1ms 检查一次退出位：

```c
while((wait > 0) &&
      ((xEventGroupGetBits(PageEventGroup) & EVT_EXIT) == 0) &&
      (xQueueReceive(UsartTimeHandler,&rx_byte,pdMS_TO_TICKS(1)) != pdTRUE))
{ wait--; }

if((xEventGroupGetBits(PageEventGroup) & EVT_EXIT) != 0)
{
    return;   /* 交给 Time_Show_f 走统一退页流程 */
}
```

### 7.5 菜单态、重复按键与残留指令

- `Usart_Rx_f` 在置位前检查 `EVT_PAGE_ALL`：菜单态直接忽略，避免退出位残留；
- 第一次退出后页面位已被清，第二次按键时 `EVT_PAGE_ALL == 0`，同样被忽略；
- **页面态不再接收 `1~4`**：Menu 已挂起，此时入队只会变成“回菜单后自动跳页”的残留；
- **离页时清空菜单队列**：`Page_LeaveCommon()` 以 0 超时抽干 `UsartMenuHandler`，再恢复 Menu/Key。

## 8. 代码改动清单

| 文件 | 改动 |
|---|---|
| `LIbrary/head.h` | 新增 `event_groups.h`、5 个位宏、`PageEventGroup` 外部声明；删除 `g_exit_req` 声明 |
| `User/main.c` | 新增 `PageEventGroup`、`Page_Wait`、`Page_Enter`、`Page_LeaveCommon`；4 个 Show 任务改为常驻 + 事件组等待；Menu 8 处入页改调 `Page_Enter`；`Usart_Rx_f` 置退出位、页面态忽略 `1~4`；离页清空菜单队列；启动时不再挂起 4 个 Show 任务；`main` 创建事件组并判 NULL |
| `User/stm32f10x_it.c` | EXTI 中断只投 `0x00` 进队列，删除 OLED 操作与 `g_exit_req` |
| `System/MyRTC.c` | `SetTime_w` 的退出判据由 `g_exit_req` 改为 `EVT_EXIT` 位 |
| `FreeRTOS/FreeRTOSConfig.h` | 本步**无需改动**（此版本事件组随 `event_groups.c` 一起编译，没有独立开关） |

`g_exit_req` 已从工程中删除。

## 9. 关键设计点与易错点

1. **前台等待掩码不能含自己的页面位**
   页面位整页期间一直置位，若写进掩码，`xEventGroupWaitBits` 条件立即满足并返回，形成忙循环。前台只等 `EVT_EXIT`。

2. **广播必须 `xClearOnExit = pdFALSE`**
   若为 `pdTRUE`，第一个醒来的任务会清位，其余任务看不到，退化成“谁先醒谁得”。

3. **广播为什么不会漏**
   `xEventGroupSetBits` 把“置位那一刻的完整位图”锁存进每个被唤醒任务的 event item；即使前台随后清位，后台任务读到的仍是锁存值。

4. **清除时机选“进页面前”，而不是“退出时抢清”**
   `Page_Enter` 先清 `EVT_PAGE_ALL | EVT_EXIT`，菜单态残留不会感染下一次进页面。

5. **事件组不计数**
   连按两次退出只会留下“仍然亮着”的一个事件；第二次按下时页面位已清，`Usart_Rx_f` 直接忽略，不会重复退页。改造期的 `exit_counted` 计数只是验收手段，现已删除。

6. **后台页的 1 tick 让出**
   退出位尚未被前台清掉时，后台任务重新等待会立即返回；`vTaskDelay(1)` 避免忙循环并给前台让出 CPU。

7. **ISR 不碰 OLED**
   软 IIC 位带时序在中断中执行时间长且不可调度；改为投递一个字节，由任务上下文置位。

8. **`5` 只在时间页前台生效**
   `Usart_Rx_f` 检查 `EVT_PAGE_TIME`；菜单态按 `5` 不再留下“下次进页面突然进入修改态”的残留。

9. **页面态丢弃 `1~4` + 离页清空菜单队列**
   这两条共同消除“按 0 退出却自动跳回某页”：页面期间 Menu 挂起，任何入队的菜单指令都会在回菜单后被当成新输入；现在页面态不入队，离页时再抽干一次队列兜底。

## 10. 验收与观测

### 10.1 编译

`main.c`、`MyRTC.c`、`stm32f10x_it.c` 单文件 `armcc` 编译 0 错误。

### 10.2 上板预期

| 测试 | 预期 |
|---|---|
| 任意页面按退出 | 页面回到菜单（广播计数为临时诊断，已拆除） |
| 连按两次退出 | 只回菜单一次；第二次被忽略 |
| 四页分别进出 | 每页显示与退出正常，无互相干扰 |
| 时间页按 `0x05` | 第 3 行出现 `Edit:Year low`；8 字节走完后时间更新 |
| 时间修改中按退出 | 立即回菜单，不卡在编辑态 |
| 页面内发 `1~4` 再按 0 退出 | 页面内无反应；回菜单后**不再自动跳回某页** |
| 菜单态按退出 | 无动作（不再出现残留标志） |

## 11. 资源开销

- 事件组对象约 40~50B 堆；
- 无新增任务、无新增任务栈；
- 每个 Show 任务新增 `EventBits_t bits`、`active` 等少量局部变量；
- `configUSE_TIMERS` 保持 0，未引入定时器守护任务的约 1.3KB 开销。

## 12. 已知限制与后续

1. **后台页仍在等退出位（但不做退页）**
   后台页面没有业务需要响应退出；保留它等 `EVT_EXIT` 是为了维持“一次置位唤醒全部等待者”的广播语义。收到后只 `vTaskDelay(1)` 让出，不做任何退页动作。若后续确认没有任务需要在后台观察退出，可进一步简化为：

   ```c
   EventBits_t mask = active ? EVT_EXIT : page_bit;
   ```

   这样只有前台页会被退出位唤醒，后台只等自己的页面位。

   改造期用于验证广播的 `Page_ExitSeen()` / `dbg_exit_seen[]` 已删除。

2. **抢占（B1）尚未开启**
   本实现按协作式调度验证。开启抢占后需要复核 `global_var` 等跨任务共享变量的保护。

3. **三路软 IIC 仍未合并**
   本次只改页面/退出机制，IIC 全量合并属于后续阶段（3C）。

## 13. 回滚方法

1. 恢复 4 个 Show 任务为“启动时挂起 + Menu `vTaskResume` + 退出 `vTaskSuspend`”；
2. 恢复 `g_exit_req` 的声明、ISR 置位与页面轮询；
3. 删除 `PageEventGroup`、位宏与三个公共函数（`Page_Wait` / `Page_Enter` / `Page_LeaveCommon`）；
4. 恢复 `SetTime_w` 的 `g_exit_req` 判据；
5. 用 `git revert` 或回到改造前的提交即可整体回退。

## 附录 A：术语

| 术语 | 含义 |
|---|---|
| 事件组（Event Group） | 一组位（本工程可用 24 位），置位时广播唤醒所有满足条件的等待者 |
| 事件位 | 表示“某事发生了”，不计数，处理完需清除 |
| 状态位 | 表示“当前处于某个状态”，同一时刻通常只有一位有效 |
| 锁存（latch） | 置位时内核把当时位图写进等待者记录，后续清位不影响已唤醒任务读到的值 |
| 忙循环 | 任务在就绪态原地空转，不让出 CPU；与“阻塞”相反 |
