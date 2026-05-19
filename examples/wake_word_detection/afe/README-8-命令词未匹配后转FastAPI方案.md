# 命令词未匹配后转 FastAPI 处理，并回播语音的设计方案

> 适用工程：`examples/wake_word_detection/afe`
>
> 当前工程已经具备：
> - 本地 **WakeNet 唤醒词检测**
> - 本地 **MultiNet 命令词识别**
> - 本地 **GPIO 动作执行**
> - 可复用的音频播放能力：`esp_audio_play()` / `player`
>
> 当前工程还 **没有**：
> - Wi-Fi 联网
> - 语音上传到服务端
> - FastAPI 返回结果解析
> - 回复语音回播链路

---

## 1. 目标

在现有“本地优先”的方案上增加一个 **兜底回退链路**：

1. 先走本地唤醒 + 本地命令词识别  
2. 如果识别结果 **没有命中本地动作表**，或者 **命令词阶段超时但用户其实说了自然语言**
3. 则把这段语音上传到 **FastAPI**
4. 由服务端做 **ASR / LLM / TTS**
5. ESP32 收到服务端返回的音频后直接播放

这样可以得到一个混合模式：

- **固定命令**：本地秒回，延迟低
- **自由表达**：走云端理解，能力更强

---

## 2. 先看当前代码里最关键的限制

### 2.1 本地命令未匹配时，当前是“静默忽略”

当前 `main/cmd_handler.c` 中：

- `cmd_handler_execute()` 负责把 `mn_result->string` 映射到 `action_map[]`
- 若找不到映射，函数最后直接结束
- 注释也明确写了：**未匹配 → 静默忽略**

这意味着：

- MultiNet 识别出了某个拼音
- 但它不在 `action_map[]`
- 现在不会触发任何 fallback

### 2.2 当前没有保存“唤醒后用户说的整段音频”

现在 `detect_Task()` 里是边取帧边调用：

- `afe_handle->fetch()`
- `multinet->detect()`

但没有把唤醒后的 PCM 连续缓存下来。  
如果后面要发给 FastAPI，就必须把这段语音保存起来。

### 2.3 当前工程已有“播放能力”，但没有“网络返回音频后播放”的整合代码

可复用点：

- `components/hardware_driver/include/esp_board_init.h`
  - `esp_audio_play(const int16_t* data, int length, TickType_t ticks_to_wait)`
- `components/player/esp_skainet_player.c`
  - 说明板级播放链路是通的
- `examples/chinese_tts/main/main.c`
  - 说明本地 TTS 也能做，但需要 `voice_data` 分区

所以：

- **回播服务端返回的 PCM/WAV**：更适合当前工程
- **在 ESP32 本地做 TTS**：也可以，但改动更重、占 Flash/RAM 更多

---

## 3. 推荐的总体方案

我建议先做下面这个 **MVP 版本**：

### 方案：本地命令优先，未匹配则上传音频到 FastAPI，服务端直接返回 WAV

```text
麦克风
  -> AFE
  -> WakeNet 检测唤醒
  -> MultiNet 识别命令词
       -> 命中 action_map[]      -> 本地执行 GPIO
       -> 未命中 / 超时有语音     -> 上传 PCM 到 FastAPI
                                      -> ASR
                                      -> LLM
                                      -> TTS
                                      -> 返回 WAV
  -> ESP32 解码/播放返回音频
```

这样做的优点：

1. **本地固定命令不受影响**
2. **网络链路只作为兜底**
3. **ESP32 端最轻**，不必先把文本再做本地 TTS
4. 服务端以后可以独立替换 ASR / LLM / TTS

---

## 4. 建议把“回退触发条件”定义清楚

建议不要把所有情况都丢到云端，否则会导致误触发和体验混乱。

### 推荐触发条件

| 场景 | 是否回退到 FastAPI | 说明 |
|---|---:|---|
| 唤醒成功，`cmd_handler_execute()` 命中动作 | 否 | 本地已经处理完 |
| 唤醒成功，MultiNet 识别成功，但不在 `action_map[]` | 是 | 最典型的云端兜底场景 |
| 唤醒成功，MultiNet 超时，但期间检测到明显语音 | 是 | 用户可能说了自然语句，不一定是命令词 |
| 没有唤醒词 | 否 | 仍保持低功耗待机 |
| 网络不可用 | 否 | 直接播报“网络不可用”或打印日志 |

### 不建议的触发方式

- 只要 `ESP_MN_STATE_TIMEOUT` 就一律上传  
  因为用户可能什么都没说

更稳妥的做法是：

- 结合 `res->vad_state`
- 并把 `res->vad_cache` + `res->data` 这段真实语音缓存起来
- 只有缓存长度达到阈值时才发云端

---

## 5. ESP32 端应如何改

## 5.1 代码结构建议

建议新增 3 个模块，不直接把所有逻辑都塞进 `main.c`：

| 文件 | 作用 |
|---|---|
| `main/cloud_fallback.h/.c` | 管理录音缓存、上传 FastAPI、接收响应 |
| `main/audio_reply.h/.c` | 回播服务端返回的 PCM/WAV |
| `main/wifi_connect.h/.c` | Wi-Fi STA 初始化与重连 |

现有文件的改动点：

| 文件 | 需要调整 |
|---|---|
| `main/cmd_handler.h/.c` | `cmd_handler_execute()` 改成返回 `bool`，表示是否命中本地动作 |
| `main/main.c` | 在 `detect_Task()` 中接入 fallback 状态机 |
| `main/CMakeLists.txt` | 增加网络相关依赖 |

---

## 5.2 最关键的接口改动：`cmd_handler_execute()` 返回 bool

当前签名：

```c
void cmd_handler_execute(const char *pinyin, float confidence);
```

建议改为：

```c
bool cmd_handler_execute(const char *pinyin, float confidence);
```

含义：

- `true`：本地动作已处理
- `false`：识别到了词，但本地没有绑定动作，应考虑 fallback

这样 `detect_Task()` 才能知道是否要转网络。

---

## 5.3 唤醒后要缓存音频，而不是只做识别

建议在唤醒后进入“录音窗口”，把以下数据拼起来缓存：

1. `res->vad_cache`  
   防止语音开头被截断
2. `res->data`  
   每一帧 AFE 输出的单声道 PCM

缓存格式建议统一为：

- **16 kHz**
- **16-bit**
- **mono**
- 原始 PCM

这是当前工程最自然的格式，也最适合直接上传给服务端。

### 何时停止录音

建议条件：

1. MultiNet 成功识别且命中本地动作  
   - 立即停止缓存，丢弃这次录音
2. MultiNet 成功识别但未命中本地动作  
   - 停止缓存，上传
3. MultiNet 超时  
   - 如果缓存里已有足够语音，则上传
4. 连续静音达到阈值  
   - 也可提前结束上传，减少等待

---

## 5.4 `detect_Task()` 推荐的新逻辑

伪代码如下：

```c
bool wakeup_flag = false;
bool cloud_recording = false;

while (task_flag) {
    afe_fetch_result_t *res = afe_handle->fetch(afe_data);
    if (!res || res->ret_value == ESP_FAIL) {
        break;
    }

    if (res->wakeup_state == WAKENET_DETECTED) {
        wakeup_flag = true;
        cloud_fallback_begin();   // 清空并开始缓存
        multinet->clean(model_data);
        continue;
    }

    if (!wakeup_flag) {
        continue;
    }

    cloud_fallback_append(res);   // 保存 vad_cache + data

    esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

    if (mn_state == ESP_MN_STATE_DETECTED) {
        esp_mn_results_t *mn_result = multinet->get_results(model_data);
        bool handled = cmd_handler_execute(mn_result->string, mn_result->prob[0]);

        if (!handled) {
            cloud_fallback_commit_and_send();
            audio_reply_play_last_response();
        }

        wakeup_flag = false;
        afe_handle->enable_wakenet(afe_data);
        continue;
    }

    if (mn_state == ESP_MN_STATE_TIMEOUT) {
        if (cloud_fallback_has_valid_audio()) {
            cloud_fallback_commit_and_send();
            audio_reply_play_last_response();
        }

        wakeup_flag = false;
        afe_handle->enable_wakenet(afe_data);
        continue;
    }
}
```

---

## 6. FastAPI 接口怎么设计更合适

## 6.1 MVP 推荐：HTTP POST 上传整段音频

先不要一上来做 WebSocket 流式，MVP 用 **单次 HTTP POST** 就够了。

原因：

- 当前场景是“唤醒后一句话”
- 代码更简单
- 调试更方便
- 出问题更容易抓包

### 推荐接口

`POST /api/v1/voice/fallback`

请求头：

```http
Content-Type: application/octet-stream
X-Audio-Format: pcm_s16le
X-Sample-Rate: 16000
X-Channels: 1
X-Device-Id: esp32s3-001
X-Session-Id: 1712345678
```

请求体：

- 直接放原始 PCM 字节流

响应建议：

### 方式 A：直接回 WAV 二进制（推荐）

响应头：

```http
Content-Type: audio/wav
X-Asr-Text: 今天天气怎么样
X-Reply-Text: 今天天气晴，最高温度三十度
```

响应体：

- WAV 文件二进制

优点：

- ESP32 收到后直接播放
- 不需要本地 TTS

### 方式 B：返回 JSON + 文本

```json
{
  "asr_text": "今天天气怎么样",
  "reply_text": "今天天气晴，最高温度三十度",
  "reply_action": null
}
```

这个更适合后续扩展，但如果你最终要“语音播放”，那还得再多一步 TTS。

所以 **本工程首版更推荐方式 A**。

---

## 6.2 FastAPI 服务端职责

推荐服务端流程：

```text
收到 PCM
  -> 转 numpy / wav
  -> ASR 转文字
  -> 文字送 LLM
  -> 得到回复文本
  -> TTS 合成 WAV
  -> 返回 WAV 给 ESP32
```

### 推荐技术栈

| 能力 | 推荐 |
|---|---|
| Web 框架 | FastAPI |
| ASR | FunASR / Whisper |
| LLM | OpenAI / DeepSeek / 本地模型 |
| TTS | edge-tts / CosyVoice / GPT-SoVITS / 其他你现有方案 |

如果你只做中文：

- **ASR**：优先 FunASR
- **TTS**：优先一个能稳定输出 16k WAV 的方案

---

## 7. 回播怎么做最稳

## 7.1 推荐首版：服务端返回 WAV，ESP32 播放 PCM 数据

这是当前工程最稳的路线。

原因：

1. 当前板级已经有 `esp_audio_play()`
2. 你不需要在 ESP32 再引入完整 TTS
3. 语音音色、模型都放在服务端，后续更容易升级

### ESP32 端播放时需要注意

- WAV 头需要解析
- 最终送给 `esp_audio_play()` 的应该是 PCM 数据段
- 采样率最好统一成 **16k / mono / 16bit**

如果服务端直接返回：

- `pcm_s16le` 裸流  
那 ESP32 更省事，但调试不如 WAV 直观

所以：

- **开发期**：返回 WAV
- **后续优化**：可改成裸 PCM

---

## 7.2 备选：服务端只回文本，ESP32 本地 TTS

本仓库已有 `examples/chinese_tts`，说明本地 TTS 是可行的。

但它带来的成本也更高：

- 需要 `voice_data` 分区
- 需要更多 Flash/RAM
- 音色和自然度通常不如服务端灵活

因此除非你有 **离线播报** 的强需求，否则不建议作为第一版。

---

## 8. 网络协议选择建议

## 第一版建议：HTTP

适合：

- 一问一答
- 录完再发
- 易于调试

## 第二版再考虑：WebSocket

适合：

- 流式上传
- 流式 ASR
- 边生成边回播

对当前目标来说，**HTTP 足够**。

---

## 9. 你这个工程里最适合的落地顺序

建议分 4 步做，不要一次把所有内容都改进去。

### 第一步：先让本地命令未匹配时“能走到 fallback 分支”

只改：

- `cmd_handler_execute()` 返回 `bool`
- `main.c` 检测未命中时打印日志

先确认状态机正确。

### 第二步：补录音缓存

把唤醒后的：

- `vad_cache`
- `data`

缓存成一段完整 PCM，并统计长度。

先不联网，只把长度打印出来，确认没有截断。

### 第三步：接 FastAPI

增加：

- Wi-Fi 初始化
- HTTP 上传 PCM
- 收到服务端响应

先不播音，只打印：

- ASR 文本
- 回复文本

### 第四步：接语音回播

服务端返回 WAV 后：

- 解析 WAV
- 用 `esp_audio_play()` 播放

到这一步就完成完整闭环。

---

## 10. 风险与注意点

## 10.1 最大风险不是识别，而是“状态机打架”

当前本地逻辑是：

- 等待唤醒
- 唤醒后听命令
- 超时后回到等待

加云端后要避免：

- 还在上传/播放时又重新进入唤醒
- 本地识别和网络请求同时抢占音频资源

所以建议新增一个简单状态：

```text
IDLE
LISTENING_LOCAL
UPLOADING_CLOUD
PLAYING_REPLY
```

播放期间先禁用新的唤醒，播完再恢复。

## 10.2 要给上传长度加上限

否则异常情况下可能一直录。

建议首版限制：

- 单次上传最长 **5~8 秒**

## 10.3 网络失败必须可恢复

失败时不要卡死在云端流程里。

建议失败处理：

- 打印错误日志
- 可选播放一句固定提示音
- 重新 `enable_wakenet()`
- 回到 `IDLE`

## 10.4 云端回复音频大小要可控

建议服务端回复音频控制在：

- **3~8 秒**

太长会影响交互节奏，也更占内存。

---

## 11. 我对这个项目的具体建议

如果你决定做，我建议按下面这个版本推进：

### 推荐版本

1. **本地命令优先**
2. **未命中动作表时上传 PCM**
3. **FastAPI 返回 WAV**
4. **ESP32 直接播放**

### 不建议第一版就做的内容

1. WebSocket 流式双向通话
2. 设备端本地 TTS
3. 服务端返回超长音频
4. 本地命令与云端意图同时执行

---

## 12. 最终结论

**这条路线是可行的，而且和你当前工程非常匹配。**

关键点不是“能不能做”，而是要先把这 3 件事补上：

1. `cmd_handler_execute()` 改成可返回“是否本地命中”
2. 唤醒后缓存完整语音
3. 增加 Wi-Fi + HTTP 上传 + 音频回播状态机

如果你后面决定继续做，最稳的实施方案就是：

> **命令词命中则本地执行；命令词未命中或超时但有语音，则上传 PCM 到 FastAPI；服务端做 ASR/LLM/TTS；ESP32 收到 WAV 后直接播放。**

