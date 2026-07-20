# STM32 项目实验室

基于 STM32F103 单片机的嵌入式开发项目集合，使用 **HAL 库** + **VSCode + CMake** 开发环境。

---

## 目录结构

```
stm32project/
├── vscode_stm32/          # 主工程：LED + OLED + 编码器 + PWM + 串口 + 按键中断
│   ├── Core/              # 应用层代码（main.c, 初始化函数）
│   ├── Drivers/           # HAL 库驱动 + CMSIS
│   ├── Hardware/          # 硬件模块驱动（OLED）
│   ├── cmake/             # CMake 构建脚本
│   ├── CMakeLists.txt     # 顶层 CMake 配置
│   ├── CMakePresets.json  # CMake 预设（编译/烧录）
│   ├── STM32F103XX_FLASH.ld  # 链接脚本
│   ├── startup_stm32f103xb.s # 启动文件
│   └── 烧录说明.md        # VSCode 烧录/调试指南
│
└── 1-3 Delay函数模块/     # 工具库：软件延时函数（标准外设库版本）
    ├── Delay.c
    └── Delay.h
```

---

## 主工程功能：vscode_stm32

### 硬件配置

| 外设 | 引脚 | 功能 |
|------|------|------|
| **TIM1** | PA8/PA9/PA10 | PWM 输出，3 路 LED 亮度控制 |
| **TIM2** | - | 定时中断（2秒上报） |
| **TIM3** | PA6/PA7 | 编码器接口模式 |
| **I2C1** | PB6/PB7 | OLED 显示屏 (SSD1306) |
| **USART1** | PA9/PA10 | 串口通信（与 PC 调试） |
| **EXTI** | PB5 | 按键中断（切换编码器模式） |
| **LED** | PC13 | 板载 LED（状态指示） |

### 软件功能

1. **LED PWM 控制**
   - 3 路独立 PWM 输出控制 LED 灯珠
   - 编码器调节亮度 / 切换显示模式

2. **OLED 显示**
   - 实时显示系统运行模式（IDLE/RUNNING/LOAD/SHARK）
   - 按需刷新，减少 I2C 通信开销
   - 显示当前编码器模式和 LED 亮度值

3. **编码器交互**
   - 3 种工作模式循环切换：
     - `default`：默认模式
     - `brights`：亮度调节模式
     - `turn`：旋转切换模式
   - 按键（PB5）按下切换模式，旋转编码器调节参数
   - 操作成功时板载 LED 快闪反馈

4. **串口通信**
   - 接收 PC 端指令切换系统模式
   - 定时上报当前运行状态
   - 编码器模式切换时同步通知

5. **系统中断管理**
   - 定时器中断（TIM2）
   - 外部中断（EXTI - 按键）
   - 串口中断（USART1）
   - 非阻塞式 LED 闪烁

---

## 开发环境

| 项目 | 配置 |
|------|------|
| 开发板 | STM32F103C8T6 (Blue Pill / 最小系统板) |
| 开发工具 | VSCode + STM32CubeIDE |
| 构建系统 | CMake + Ninja |
| 固件库 | STM32 HAL Driver |
| 调试器 | ST-Link V2 |
| 编译器 | ARM GNU GCC |

### 快速开始

1. **克隆仓库**
   ```bash
   git clone https://github.com/gujiu47-2KPJ/stm32-lab.git
   cd stm32-lab
   ```

2. **打开工程**
   ```bash
   cd vscode_stm32
   code .
   ```

3. **编译**
   - 按 `Ctrl+Shift+B` 或执行 CMake 构建

4. **烧录 & 调试**
   - 按 `F5` 启动调试模式（自动编译 + 烧录）
   - 详见 [vscode_stm32/烧录说明.md](vscode_stm32/烧录说明.md)

---

## 硬件连接示意

```
STM32F103C8T6
┌─────────────────────────────────────────┐
│                                         │
│  PA6/PA7 ──┐                          │
│            ├─> 编码器 (CLK/DT)         │
│  PB5 ──────┘                          │
│                                         │
│  PB6/PB7 ──┐                          │
│            ├─> OLED (I2C: SCL/SDA)    │
│  GND ──────┤                          │
│  3.3V ─────┘                          │
│                                         │
│  PA8/PA9/PA10 ──┐                     │
│                  ├─> 3 路 PWM LED     │
│  GND ────────────┘                     │
│                                         │
│  PA9/PA10 ──┐                          │
│             ├─> 串口 (USB-TTL)         │
│  GND ───────┘                          │
│                                         │
│  PA13/PA14 ──┐                         │
│              ├─> ST-Link (SWD)         │
│  GND/3.3V ───┘                         │
│                                         │
└─────────────────────────────────────────┘
```

---

## 分支管理

- **master** - 主分支，稳定可用代码
- （其他分支已清理）

---

## 许可证

本项目仅用于学习和研究，遵循 STMicroelectronics HAL 库原始许可证。