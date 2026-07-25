"""
WebSocket 服务端 — 收 ESP32 的 PCM 音频，跑流水线，返回 PCM 语音

启动:   python server.py
访问:   ws://localhost:8006/chat
"""

import io
import wave
import time
import uvicorn
from urllib.parse import quote
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.responses import Response

from pipeline import speech_to_text, llm_chat, mp3_to_pcm, text_to_speech, voice_pipeline

app = FastAPI()


# ============================================================
#  PCM bytes → WAV bytes（给 ASR 用，ASR 需要 WAV 头）
# ============================================================
def pcm_to_wav(pcm: bytes, sample_rate=16000, channels=1, bit_depth=2) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(bit_depth)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)
    return buf.getvalue()


# ============================================================
#  /chat — 一轮对话
# ============================================================
@app.websocket("/chat")
async def chat(ws: WebSocket):
    await ws.accept()
    print("[连接] ESP32 已连接")

    history = []

    try:
        while True:
            # ---- 收数据 ----
            audio_buf = bytearray()
            sample_rate = 16000
            channels = 1
            bit_depth = 2

            while True:
                data = await ws.receive()

                if "text" in data:
                    text = data["text"]
                    if text.startswith("START:"):
                        audio_buf = bytearray()
                        parts = text[6:].split(",")
                        sample_rate = int(parts[0])
                        bit_depth = int(parts[1]) // 8
                        channels = int(parts[2])
                        print(f"[接收] 开始录音 SR={sample_rate} BD={bit_depth * 8} CH={channels}")
                        continue
                    elif text == "END":
                        print(f"[接收] 录音结束 {len(audio_buf)} bytes")
                        break
                    elif text == "STOP":
                        print("[连接] 客户端关闭")
                        return
                    elif text.startswith("TEXT:"):
                        print(f"[接收] {text}")
                        continue

                elif "bytes" in data:
                    audio_buf.extend(data["bytes"])

            # ---- 录音太短，跳过 ----
            if len(audio_buf) < sample_rate * bit_depth:
                print("[跳过] 音频不足 1 秒")
                await ws.send_text("SKIP")
                continue

            # ---- PCM → WAV → ASR ----
            duration = len(audio_buf) / (sample_rate * bit_depth * channels)
            wav_bytes = pcm_to_wav(bytes(audio_buf), sample_rate, channels, bit_depth)
            print(f"[录音] {duration:.1f}s")

            # ---- ASR ----
            t0 = time.time()
            user_text = await speech_to_text(wav_bytes)
            print(f"[ASR] 耗时 {time.time() - t0:.1f}s")
            if not user_text:
                await ws.send_text("SKIP")
                continue

            # ---- LLM ----
            t0 = time.time()
            ai_text = await llm_chat(user_text, history)
            print(f"[LLM] 耗时 {time.time() - t0:.1f}s")

            # ---- TTS ----
            t0 = time.time()
            mp3 = await text_to_speech(ai_text)
            pcm = mp3_to_pcm(mp3, sample_rate)
            print(f"[TTS] 耗时 {time.time() - t0:.1f}s → {len(pcm)} bytes PCM")

            # ---- 更新历史 ----
            history.append({"role": "user", "content": user_text})
            history.append({"role": "assistant", "content": ai_text})

            # ---- 返回给 ESP32 ----
            await ws.send_text(f"TEXT:{user_text}|{ai_text}")
            await ws.send_bytes(pcm)
            await ws.send_text("DONE")

            print(f"[完成] 用户: {user_text}  |  AI: {ai_text}")
            print()

    except WebSocketDisconnect:
        print("[连接] ESP32 断开")


# ============================================================
#  POST /voice — HTTP 版本（ESP32 最简单的接入方式）
#  ESP32 把 PCM 二进制扔进 body，返回 PCM 二进制
#  查询参数: ?sr=16000&bits=16&ch=1（默认就是 16000/16/1）
# ============================================================
@app.post("/voice")
async def voice_http(request: Request):
    pcm_body = await request.body()  # raw PCM bytes from ESP32

    sample_rate = int(request.query_params.get("sr", 16000))
    bit_depth   = int(request.query_params.get("bits", 16)) // 8
    channels    = int(request.query_params.get("ch", 1))

    if len(pcm_body) < sample_rate * bit_depth:
        return Response(content=b"", status_code=400, media_type="application/octet-stream")

    # ---- 调试：保存收到的 PCM 到文件 ----
    with open("debug_recv.pcm", "wb") as f:
        f.write(pcm_body)
    print(f"[调试] 收到 {len(pcm_body)} bytes PCM, 已保存到 debug_recv.pcm")
    # ------------------------------------

    # PCM → WAV → pipeline
    wav_bytes  = pcm_to_wav(pcm_body, sample_rate, channels, bit_depth)
    pcm, user, ai = await voice_pipeline(wav_bytes)

    if not pcm:
        return Response(content=b"", status_code=204, media_type="application/octet-stream")

    # 返回 raw PCM，同时把识别结果放 header 里给 ESP32 显示用
    return Response(
        content=pcm,
        media_type="application/octet-stream",
        headers={
            "X-User-Text":  quote(user, safe=""),
            "X-AI-Text":    quote(ai, safe=""),
        },
    )


if __name__ == "__main__":
    print("=" * 40)
    print("ESP32 AI 语音助手 — 后端服务")
    print("HTTP:   POST http://localhost:8006/voice")
    print("WebSocket: ws://localhost:8006/chat")
    print("=" * 40)
    uvicorn.run(app, host="0.0.0.0", port=8006)
