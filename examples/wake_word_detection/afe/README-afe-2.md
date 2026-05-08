# ESP-Skainet 语音方案对比与后续扩展

## ESP-Skainet 示例项目对比

| 项目路径 | 用途 | 是否需要麦克风 | 识别内容 |
|:---|:---|:---:|:---|
| `wake_word_detection/afe` | 唤醒词检测（当前项目） | ✅ 需要 | 固定唤醒词（如"Hi 乐鑫"） |
| `wake_word_detection/wakenet` | WakeNet 算法验证 | ❌ 用内置音频 | 同上，但纯软件测试 |
| `cn_speech_commands_recognition` | 离线中文命令词识别 | ✅ 需要 | 预定义命令词（如"打开空调"） |

---

### wake_word_detection/afe（当前项目）

- 使用 **AFE（Audio Front-End）** 音频前端引擎 + **WakeNet** 唤醒词模型
- AFE 负责降噪、回声消除、波束成形等预处理
- WakeNet 负责在处理后的音频流中检测唤醒词
- 适用于真实硬件场景，接真实麦克风（如 INMP441）

### wake_word_detection/wakenet

- **纯算法单元测试**，不使用真实麦克风
- 将预录制的音频数据（`.h` 头文件形式）直接喂入 WakeNet 模型
- 用于验证模型是否能正确检测唤醒词
- 开发调试用，不适合部署

### cn_speech_commands_recognition

- 完整的 **唤醒 + 命令词识别** 流程
- 唤醒后进入 **MultiNet** 命令词识别阶段（`multinet->detect()`）
- 可识别 sdkconfig 中预定义的中文命令（如"打开空调""关灯""增大音量"等）
- 识别超时后自动回到等待唤醒状态
- 全程离线，无需网络

---

## 后续语音识别方案选择

### 方案 A：离线命令词识别（不需要服务器）

如果只需识别 **固定的几十条中文命令**，直接在 `cn_speech_commands_recognition` 基础上开发即可。

**架构：**

```
┌──────────────────────────────────────────┐
│            ESP32-S3 (本地全离线)           │
│                                           │
│  麦克风 → AFE → WakeNet (唤醒词检测)      │
│                    │                      │
│                    ▼ 唤醒成功              │
│              MultiNet (命令词识别)         │
│                    │                      │
│                    ▼ 识别结果              │
│              执行本地动作                  │
│         (控制 GPIO / LED / 继电器等)       │
└──────────────────────────────────────────┘
```

**优点：**
- 无需网络，响应快（< 500ms）
- 隐私性好，数据不出设备
- 无服务器成本

**限制：**
- 只能识别预定义的命令词（通常 < 200 条）
- 不支持自由对话

---

### 方案 B：云端语音识别 + 大模型对话（需要 FastAPI 服务）

如果需要 **任意语句转文字（ASR）** 或 **接入大语言模型做智能对话**，ESP32-S3 算力不足以完成，需要搭建服务端。

**架构：**

```
┌─────────────────────┐          ┌─────────────────────────────────┐
│     ESP32-S3        │          │        FastAPI 服务端            │
│                     │          │                                  │
│ 麦克风 → AFE        │          │  ┌─────────────────────────┐    │
│   → WakeNet        │          │  │  ASR 语音识别            │    │
│      │ 唤醒成功     │  WiFi    │  │  (Whisper / FunASR /    │    │
│      ▼             │ ──────►  │  │   讯飞 / 百度等)         │    │
│ 录制音频 PCM/Opus  │  音频流   │  └──────────┬──────────────┘    │
│   → WiFi 发送      │          │             │ 文本               │
│                     │          │             ▼                    │
│ ◄─── 接收回复 ──── │  文本/   │  ┌─────────────────────────┐    │
│      │             │  音频流   │  │  LLM 大语言模型          │    │
│      ▼             │ ◄──────  │  │  (ChatGPT / DeepSeek /  │    │
│ TTS 播放(可选)     │          │  │   本地 Llama 等)         │    │
│ 或 执行动作        │          │  └──────────┬──────────────┘    │
└─────────────────────┘          │             │ 回复文本           │
                                 │             ▼                    │
                                 │  ┌─────────────────────────┐    │
                                 │  │  TTS 语音合成 (可选)     │    │
                                 │  │  (edge-tts / VITS 等)   │    │
                                 │  └─────────────────────────┘    │
                                 └─────────────────────────────────┘
```

**ESP32-S3 端职责：**
- 本地唤醒词检测（低功耗待机）
- 唤醒后录音，通过 WiFi 发送 PCM 音频流
- 接收服务端返回的文本或音频并执行/播放

**FastAPI 服务端职责：**
- 接收音频流，调用 ASR 引擎转文字
- 将文字送入 LLM 获取回复
- （可选）将回复文本转语音返回设备

**优点：**
- 支持任意自然语言对话
- 可接入各种 AI 能力（翻译、问答、控制等）
- 模型可随时升级，不影响设备端

**限制：**
- 需要 WiFi 网络
- 需要部署和维护服务器
- 延迟较高（1-3 秒）

---

## 方案选择建议

| 需求场景 | 推荐方案 | 说明 |
|:---|:---:|:---|
| 固定命令控制（开灯/关灯/播放音乐） | A | 纯离线，响应快 |
| 自由语音对话 / 问答 | B | 需要云端 ASR + LLM |
| 语音转文字记录 | B | 需要云端 ASR |
| 多语言支持 | B | 本地模型仅支持中/英文命令 |
| 无网络环境 | A | 唯一选择 |
| 低延迟要求 (< 500ms) | A | 云端方案延迟不可控 |

---

## 从当前项目扩展到方案 B 的关键步骤

如果你决定做云端语音识别，从当前 `afe` 项目出发需要：

### ESP32-S3 端改造

1. **添加 WiFi 连接** — 初始化 WiFi STA 模式，连接路由器
2. **音频流上传** — 唤醒后将 `bsp_get_feed_data` 读取的 PCM 数据通过 WebSocket 发送
3. **VAD 静音检测** — 利用 AFE 的 `vad_state` 检测用户是否说完话，说完后停止录音
4. **接收处理结果** — 解析服务端返回的文本/音频，执行对应动作或播放

### FastAPI 服务端搭建（示意）

```python
from fastapi import FastAPI, WebSocket
import whisper  # 或 funasr

app = FastAPI()
model = whisper.load_model("base")

@app.websocket("/asr")
async def asr_endpoint(websocket: WebSocket):
    await websocket.accept()

    # 接收 ESP32-S3 发送的 PCM 音频
    audio_data = await websocket.receive_bytes()

    # ASR：语音转文字
    result = model.transcribe(audio_data)

    # LLM：生成智能回复
    reply = call_llm(result["text"])

    # 返回结果给 ESP32-S3
    await websocket.send_text(reply)
```

### 推荐技术栈

| 组件 | 推荐方案 | 备注 |
|:---|:---|:---|
| ASR | FunASR / Whisper | FunASR 中文效果好，Whisper 多语言 |
| LLM | DeepSeek / ChatGPT API | DeepSeek 性价比高 |
| TTS | edge-tts / ChatTTS | edge-tts 免费，ChatTTS 自然度高 |
| 通信协议 | WebSocket | 适合音频流式传输 |
| 音频编码 | Opus / 原始 PCM | Opus 省带宽，PCM 简单直接 |

---

## 总结

```
当前已完成：
  ✅ ESP32-S3 + INMP441 唤醒词检测 (wake_word_detection/afe)

下一步可选方向：
  📌 方案 A — 在设备端加入 MultiNet 做离线命令词识别
  📌 方案 B — 搭建 FastAPI 服务，实现自由语音对话
```
