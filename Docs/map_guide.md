# map 文件速查（`Listings/手环项目.map`）

> 用途：读懂 armlink 生成的链接映射，定位"符号在哪、占多大、谁引用了谁"。
> 数据源：本项目**最后一次链接**的 map（ARM Compiler 5.06u7 / armlink）。
> ⚠️ map 是**每次编译的快照**，地址会随优化等级/代码变化 —— 别把地址当永久结论。
> 配套：`architecture_draft.md`（架构）／`notes_rtos.md`（问题清单）

---

## 0. 一句话定位

**`.map` = 链接器留下的"内存分配账单 + 依赖清单"。**
回答三个问题：① 某个符号放在哪个地址、多大？② 谁引用了谁？③ 总共占了多少 Flash / RAM？

**打开方式**：Keil → `Options for Target` → `Listing` → 勾 `Linker Map`（本项目已勾，产物在 `Listings/手环项目.map`，330 KB）。

---

## 1. 五大段落（按文件顺序）

| # | 段落名 | 回答什么问题 | 用不用得上 |
|---|---|---|---|
| ① | `Section Cross References` | 谁调用了谁 / 谁引用了谁 | 追依赖、查死代码 |
| ② | `Memory Map of the image` | 每个符号的**地址 + 段 + .o** | ★★★ 最有用 |
| ③ | `Image Symbol Table` | 全局符号总表（Local/Global） | ★★ 查符号 |
| ④ | `Image component sizes` | **每个 .o 占多少** | ★★★ 找"谁最占空间" |
| ⑤ | `Grand Totals` | RO / RW / ROM 三个总数 | ★★★ 容量红线 |

---

## 2. 段落① `Section Cross References` · 逐字段

**样例（本项目真实行，map L133）：**
```
stm32f10x_it.o(i.EXTI9_5_IRQHandler) refers to main.o(.data) for KeyExitHandler
```

**读法 = `谁 → 引用了什么 → 为了拿到哪个符号`**

| 片段 | 含义 |
|---|---|
| `stm32f10x_it.o` | 目标文件（`stm32f10x_it.c` 编译产物） |
| `(i.EXTI9_5_IRQHandler)` | **该 .o 内部的这个函数**。`i.` = 代码段（instruction） |
| `refers to` | 引用了 / 用到了 |
| `main.o` | 被引用的目标文件 |
| `(.data)` | 被引用对象所在的**段**。`.data` = 有初值的全局变量 |
| `for KeyExitHandler` | **具体符号名** |

**→ 翻译**：`stm32f10x_it.c` 的 `EXTI9_5_IRQHandler` 里用到了 `main.c` 的全局变量 `KeyExitHandler`。

**对应源码**（`stm32f10x_it.c` L184）：
```c
extern QueueHandle_t KeyExitHandler;
xQueueSendFromISR(KeyExitHandler, &KeyExit, NULL);
```

> ★ 这一行是本项目 **P0-4（ISR 里调 FromISR，抢占优先级 0）** 的**入口证据**。

**常见段名对照**

| 段 | 内容 | 占哪 |
|---|---|---|
| `.text` / `i.` 前缀 | 代码 | Flash |
| `.constdata` / `.rodata` | const 变量、字符串 | Flash |
| `.data` | **有初值**的全局/静态变量 | Flash(初值) + RAM |
| `.bss` | **无初值**的全局/静态变量 | RAM |
| `ABSOLUTE` | 不可重定位的绝对段 | — |

---

## 3. 段落④ `Image component sizes` · 逐列

**样例（本项目真实行，map L3415）：**
```
Code (inc. data)   RO Data    RW Data    ZI Data      Debug   Object Name
894         72          0         28          0       8273   adxl345.o
```

| 列 | 值 | 含义 |
|---|---|---|
| **Code (inc. data)** | 894 (72) | 代码字节；**括号内 = 内联进指令的常量**，非独立 RO Data |
| **RO Data** | 0 | 只读数据（`const`、字符串） |
| **RW Data** | 28 | 有初值全局/静态 → 占 **RAM** |
| **ZI Data** | 0 | 无初值全局/静态（BSS）→ 占 **RAM**，不占 Flash |
| **Debug** | 8273 | **调试信息，不烧进芯片**，只给调试器用 |
| **Object Name** | `adxl345.o` | 哪个目标文件 |

**三条实用推论**
1. **烧进 Flash 的 = Code + RO Data**（894 + 0 = 894 B）。`Debug` 那 8273 B **不占芯片容量** —— 这就是 Debug/Release 烧录后大小没差多少的原因。
2. **占 RAM 的 = RW + ZI**（28 + 0 = **28 B**）。`adxl345.c` 里的 `temp_X/Y/Z`、`BUF[8]`、`ge/shi/bai/qian/wan` 就是这 28 字节。
3. **"inc. data" 的含义**：像 `float buf = 0.0f;` 这种小常量，编译器可能直接编码进指令，不单独占 RO Data。

### 本项目各模块实际占用（可直接引用）

| 模块 | Code | RO Data | RW | ZI | Debug | 备注 |
|---|---|---|---|---|---|---|
| `main.o` | 3050 | 870 | 60 | 92 | 260652 | **Debug 巨大是因为任务函数多**；RO 870 含字符串常量 |
| `oled.o` | 814 | **1520** | 0 | 0 | 7773 | ★ **RO 1520 = 字库**，Flash 大头 |
| `tasks.o` | 2936 | 386 | 0 | 64 | 32295 | FreeRTOS 内核 |
| `heap_4.o` | 568 | 72 | 0 | **10240** | 3795 | ★ **ZI 10240 = `ucHeap[]`**，堆本体 |
| `queue.o` | 1342 | 12 | 0 | 0 | 13761 | 队列实现 |
| `port.o` | 420 | 48 | 0 | 4 | 8744 | Cortex-M 移植层 |
| `blood.o` | 1400 | 138 | 0 | 20 | 4352 | FFT + 血氧算法 |
| `adxl345.o` | 894 | 0 | 28 | 0 | 8273 | 驱动很省 RAM |
| `dht11.o` | 502 | 100 | 0 | 0 | 5213 | — |
| `usart.o` | 372 | 42 | 0 | 0 | 2968 | 你自己的串口打印 |
| `stm32f10x_usart.o` | 472 | 6 | 0 | 0 | 12627 | 标准库串口 |

> ★ **`main.o` 的 Debug 高达 260 KB** —— 说明绝大部分调试信息来自任务函数。**别被 Debug 列吓到。**

---

## 4. 段落③ `Image Symbol Table` · 逐列

**样例（用户提供，来自 C 库）：**
```
Symbol Name                    Value     Ov Type        Size  Object(Section)
../clib/angel/boardlib.s    0x00000000   Number         0  boardinit2.o ABSOLUTE
```

| 列 | 值 | 含义 |
|---|---|---|
| **Symbol Name** | `../clib/angel/boardlib.s` | 符号名（此处是 C 库的路径式符号） |
| **Value** | `0x00000000` | 地址。**0 不一定表示"在地址 0"** |
| **Ov** | `Number` | **Overlay 重定位类型**。`Number` = 纯数值常量，非真实地址 |
| **Type** | （空） | 类型；空 = 纯数值 |
| **Size** | 0 | 占 0 字节 |
| **Object(Section)** | `boardinit2.o ABSOLUTE` | 来自 `boardinit2.o` 的 `ABSOLUTE` 段 |

**→ 翻译**：这是 **ARM C 库（angel 版）** 的一个**标记符号**，值为 0、大小 0、不对应任何真实内存。**不用管它。**

**★ 关键判读规则：`Value` 要结合 `Ov` 列看**

| `Ov` 值 | Value 的含义 |
|---|---|
| `Data` / `Code` / `Section` | **真实地址** |
| `Number` 或段名 `ABSOLUTE` | **常量 / 编号**，不是地址 |

> 面试延伸：如果 `&some_var` 打出 0，**正常情况下说明链接/分散加载配置有问题**；
> 但若该符号的 `Ov = Number`，那就只是个常量，不是"变量被放在地址 0"。

**本项目实例（map L2936）**
```
KeyExitHandler      0x2000004c   Data    4   main.o(.data)
```
→ 它是**真实数据**（`Ov = Data`），地址 `0x2000004c`（在 SRAM 区间），大小 **4 B**（`QueueHandle_t` = 指针）。

**本项目实例（map L2434）**
```
ucHeap      0x20001510   Data   10240   heap_4.o(.bss)
```
→ FreeRTOS 堆本体，**10240 B = 10 KB**，印证 `configTOTAL_HEAP_SIZE = 10*1024`。

---

## 5. 段落⑤ `Grand Totals` · 容量红线

**本项目真实数值：**

| 项 | 值 | 解释 |
|---|---|---|
| `Total RO Size (Code + RO Data)` | **34676 (33.86 kB)** | 占 **Flash** |
| `Total RW Size (RW Data + ZI Data)` | **17312 (16.91 kB)** | 占 **RAM** |
| `Total ROM Size (Code + RO Data + RW Data)` | **34720 (33.91 kB)** | 烧录文件大小 |

**STM32F103C8 = Flash 64 KB / RAM 20 KB**（工程 `IRAM 0x5000=20KB` / `IROM 0x10000=64KB`）

| 资源 | 已用 | 上限 | 占比 |
|---|---|---|---|
| Flash | 33.86 KB | 64 KB | **53%** |
| RAM | 16.91 KB | 20 KB | **84%** ⚠️ |

> ★ **RAM 只剩约 3 KB 余量** —— 印证 `notes_rtos.md` 优化方向 C3「堆仅 10KB，余量极小」。
> 注意：`Total RW` 里已含 10 KB 堆 + 7 KB 任务栈（静态分配的 TCB/栈在 `.bss`）。

---

## 6. 五个典型用法（照着搜）

| 想干什么 | 搜什么 | 看哪段 |
|---|---|---|
| 查变量/函数的**地址与大小** | 符号名 | ③ Symbol Table |
| 查**谁调用了它** | 符号名 | ① Cross References |
| 查**它调用了谁** | `函数名)` | ① Cross References |
| 查某模块**占多少空间** | `.o` 文件名 | ④ component sizes |
| 判断**会不会爆 Flash/RAM** | `Total` | ⑤ Grand Totals |
| 查符号**是否被链接进去**（死代码） | 符号名 → **搜不到 = 被丢弃** | ③ Symbol Table |

---

## 7. 三个坑

1. **只是快照**：每次编译重新生成，改优化等级地址全变。**别把地址写进代码或"永久结论"**。
2. **`Debug` 列不占芯片**：占了 map 文件绝大部分体积（本项目 main.o 就 260 KB），**和容量无关**。
3. **本工程有两个 map**：
   - `Listings/手环项目.map` ← **看这个**（Keil，与编译配置对应）
   - `build/Target 1/Target03.map`（EIDE 插件产出，可能不同步）
