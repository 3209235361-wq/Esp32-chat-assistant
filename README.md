The chat assistant based on ESP32：talk to microphone，AI will recognize your voice and the loudspeaker will play the reply voice。

  **structure**：ESP32 can only recording、sending、playing；ASR、LLM、TTS are finished in PC terminal Python.
                 
  **hardware**：ESP32-S3-N16R8 , INMP441:microphone , MAX98357A:loudspeaker , SSD1306:OLED , Motor,led 

  **technology**：ESP-IDF (C) + FastAPI (Python) · DashScope ASR · DeepSeek LLM · Edge-TTS + UI Manager(These devices are managed by intrusive two way loop linked list)
  <img width="1468" height="1284" alt="chat_assistant" src="https://github.com/user-attachments/assets/f3b44d09-5725-4ab4-b08d-e4ee37cece99" />

