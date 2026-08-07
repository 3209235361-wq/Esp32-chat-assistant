基于 ESP32-S3 的 AI 语音对话助手：对着麦克风说话，AI 识别并回答，通过喇叭播放语音回复。

  **架构**：ESP32 只负责录音、传输、播放；语音识别（ASR）、大模型对话（LLM）、语音合成（TTS）全部在 PC 端 Python
  服务完成。

  **硬件**：ESP32-S3 · INMP441 麦克风 · MAX98357A 功放 · SSD1306 OLED

  **技术栈**：ESP-IDF (C) + FastAPI (Python) · DashScope ASR · DeepSeek LLM · Edge-TTS
