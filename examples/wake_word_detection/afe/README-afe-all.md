# 完整构建流程（从零开始）

> ⚠️ 命令执行顺序非常重要！`set-target` 会重置 sdkconfig，之后必须重新 menuconfig 选板。

---

## 前置条件

- 已安装 ESP-IDF v5.5.x 并 `source export.sh`
- 工作目录：`esp-skainet/examples/wake_word_detection/afe`
- ESP32-S3-DevKitC-1 + INMP441 已通过 USB 连接

---

## 执行步骤

### 第 1 步：完全清除构建缓存

```bash
idf.py fullclean
```

**作用：** 删除 `build/` 目录中所有编译产物、缓存文件和旧的 sdkconfig 生成物。确保后续构建从干净状态开始，避免旧配置残留。

---

### 第 2 步：设置目标芯片为 ESP32-S3

```bash
idf.py set-target esp32s3
```

**作用：**
- 将 `IDF_TARGET` 设为 `esp32s3`
- **重新生成 sdkconfig**（合并 `sdkconfig.defaults` + `sdkconfig.defaults.esp32s3`）
- 清除并重建 `build/` 目录的 CMake 缓存

**关键点：** 这一步会读取 `sdkconfig.defaults.esp32s3` 中的配置，包括：
```
CONFIG_SR_WN_WN9_HILEXIN=y          ← 唤醒词模型
CONFIG_SR_MN_CN_MULTINET7_QUANT=y   ← 命令词模型（MultiNet）
CONFIG_SPIRAM=y                      ← 启用 PSRAM（模型需要）
```

> ⚠️ **set-target 执行后 sdkconfig 会被重置！** 之前在 menuconfig 中做的所有选择（如选板）都会丢失，必须重新配置。

---

### 第 3 步：通过 menuconfig 选择硬件板级

```bash
idf.py menuconfig
```

**导航路径：**
```
(Top)
  → Audio Media HAL
    → Audio hardware board
      → ( ) ESP32-S3-Korvo-1          ← 默认选中（但我们不用这个！）
      → (*) ESP32-S3-DevKitC-1 (INMP441)  ← 选择这个
```

**操作：**
1. 用 ↑↓ 键移动到 `ESP32-S3-DevKitC-1 (INMP441)`
2. 按 `Enter` 选中
3. 按 `S` 保存
4. 按 `Q` 退出

**为什么这步必须在 set-target 之后：**
- `set-target` 重置 sdkconfig，默认板是 `ESP32-S3-Korvo-1`
- 如果不重新选板，编译出来的固件会试图初始化 Korvo 板上的 ES7210/ES8311 编解码器芯片
- 你的板子上没有这些芯片，会出现大量 `I2C_If: Fail to write to dev` 错误

---

### 第 4 步：编译项目

```bash
idf.py build
```

**作用：** 编译所有组件，打包固件和模型文件。

**编译成功标志：**
```
wake_word_detection.bin binary size 0xXXXXXX bytes. Smallest app partition is 0x271000 bytes.
```

**如果编译报错：**
- `implicit declaration of function` → 检查是否缺少 `#include`
- `undefined reference` → 检查 `CMakeLists.txt` 和 `idf_component.yml` 的依赖
- 模型相关错误 → 确认 `set-target` 后重新 build 过

---

### 第 5 步：烧录固件并打开串口监视器

```bash
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

**作用：**
- `flash` — 将 bootloader、app、分区表、SR 模型全部写入 flash
- `monitor` — 烧录完成后自动打开串口监视器查看输出

**烧录内容：**
| 地址 | 内容 | 说明 |
|:---|:---|:---|
| 0x0 | bootloader.bin | 引导程序 |
| 0x8000 | partition-table.bin | 分区表 |
| 0x10000 | wake_word_detection.bin | 应用程序 |
| 0x281000 | srmodels.bin | 唤醒词 + 命令词模型 |

**退出 monitor：** 按 `Ctrl+]`

---

## 完整命令汇总（可直接复制执行）

```bash
# 进入项目目录
cd esp-skainet/examples/wake_word_detection/afe

# 1. 清除所有构建缓存
idf.py fullclean

# 2. 设置目标芯片（会重置 sdkconfig）
idf.py set-target esp32s3

# 3. 选择 INMP441 板级配置（交互式菜单）
idf.py menuconfig
# → Audio Media HAL → Audio hardware board → ESP32-S3-DevKitC-1 (INMP441)
# 按 S 保存，Q 退出

# 4. 编译
idf.py build

# 5. 烧录 + 监视器（替换为你的串口路径）
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

---

## 正确运行时的串口输出

```
I (xxx) devkitc1_board: 初始化 ESP32-S3-DevKitC-1 + INMP441
I (xxx) devkitc1_board: I2S 引脚 — SCK: 4, WS: 5, SD: 6
I (xxx) devkitc1_board: 采样率: 16000 Hz, 位移: 16
I (xxx) MODEL_LOADER: Successfully load srmodels
命令词模型: mn7_cn
唤醒词模型: wn9_hilexin
I (xxx) AFE: Input PCM Config: total 2 channels(1 microphone, 0 playback)  ← 正确！
I (xxx) AFE: AFE Pipeline: [input] -> |VAD| -> |WakeNet| -> [output]
multinet 模型: mn7_cn
XXX active speech commands:
...
------------detect start------------
说出唤醒词以激活命令词识别...
-----------等待唤醒词...-----------
```

**关键验证点：**
- ✅ 看到 `devkitc1_board` 而不是 I2C 错误
- ✅ `total 2 channels(1 microphone, 0 playback)` — 单麦克风无回放
- ✅ 没有 `Fail to write to dev 80/30` 错误
- ✅ `multinet 模型: mn7_cn` — MultiNet 成功加载

---

## 错误排查

### 情况 1：只看到 "ESP32-Korvo" 选项

```
原因：IDF_TARGET 不是 esp32s3
解决：idf.py set-target esp32s3，然后重新 menuconfig
```

### 情况 2：大量 I2C 错误 (dev 80 / dev 30)

```
原因：选错了板级配置，当前用的是 Korvo 板驱动
解决：idf.py menuconfig → 选择 ESP32-S3-DevKitC-1 (INMP441) → rebuild
```

### 情况 3：Guru Meditation / LoadProhibited 崩溃

```
原因：通常是 MultiNet 模型未加载（set-target 前配置不含模型）
解决：确认 sdkconfig.defaults.esp32s3 包含 CONFIG_SR_MN_CN_MULTINET7_QUANT=y
      然后重新 set-target → menuconfig → build → flash
```

### 情况 4：编译成功但唤醒词不响应

```
排查：
1. 确认 INMP441 接线正确（SCK=4, WS=5, SD=6, L/R→GND）
2. 调整 INMP441_BIT_SHIFT（头文件中 16→14 提升灵敏度）
3. 靠近麦克风 10cm 内说 "Hi 乐鑫"
```

---

## 命令执行顺序重要性总结

```
fullclean → set-target → menuconfig → build → flash monitor
    ↓           ↓            ↓          ↓         ↓
  清除缓存   重置sdkconfig  选板/配模型  编译     烧录运行

       ⚠️ set-target 会重置 menuconfig 的选择！
       ⚠️ menuconfig 必须在 set-target 之后执行！
       ⚠️ build 必须在 menuconfig 保存之后执行！
```
