# -*- coding: utf-8 -*-
"""
ASR → LLM → TTS → PCM 完整流水线

   WAV ──Paraformer──→ 文字 ──DeepSeek──→ 回复 ──EdgeTTS──→ MP3 ──ffmpeg──→ PCM

依赖: pip install dashscope openai edge-tts
"""

import os
import io
import socket
import asyncio
import subprocess
import tempfile
import wave
import time

import aiohttp
import dashscope
from dashscope.audio.asr import Recognition
from openai import OpenAI
import edge_tts

# ============ config ============
# API keys
DASHSCOPE_KEY = os.getenv("DASHSCOPE_API_KEY", "")
DEEPSEEK_KEY  = os.getenv("DEEPSEEK_API_KEY",  "")
DEEPSEEK_URL  = "https://api.deepseek.com"
SYSTEM_PROMPT = "你是一个友好的语音助手,回答简洁,回复一到两句,40字以内。"
SPECIAL_REPLIES = {
    "开灯" : "好的，灯已打开",
    "关灯" : "好的，灯已关闭",
    "亮灯" : "好的，灯已亮",
    "灭灯" : "好的，灯已灭",
    "中档灯" : "好的，灯已设置为中档",
    "高档灯" : "好的，灯已设置为高档",
    
    "开电机" : "好的，电机已打开",
    "关电机" : "好的，电机已关闭",
    "电机转" : "好的，电机已转",
    "电机停" : "好的，电机已停止",
    "中档电机" : "好的，电机已设置为中档",
    "高档电机" : "好的，电机已设置为高档",
}
VOICE_LIST={
    0: "zh-CN-XiaoyiNeural",
    1: "zh-CN-YunyangNeural",
    2: "zh-HK-HiuMaanNeural",
    3: "en-US-AvaMultilingualNeural",
}
def get_special_reply(user_text: str) -> str | None:
    # 检查是否有特殊回复
    for key, reply in SPECIAL_REPLIES.items():
        if key in user_text:
            return reply
    return None


dashscope.api_key = DASHSCOPE_KEY


# ========== ffmpeg ==========
def find_ffmpeg() -> str:
    try:
        subprocess.run(["ffmpeg", "-version"], capture_output=True, check=True)
        return "ffmpeg"
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        raise RuntimeError("请安装 ffmpeg: winget install ffmpeg  或  pip install imageio-ffmpeg")


# ============================================================
#  第1步: ASR — WAV → 文字
# ============================================================
async def speech_to_text(wav_bytes: bytes) -> str:
    if not DASHSCOPE_KEY:
        return "[未设置 DASHSCOPE_API_KEY]"

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        tmp.write(wav_bytes)
        tmp_path = tmp.name

    try:
        recognition = Recognition(
            model="paraformer-realtime-v2",
            format="wav",
            sample_rate=16000,
            callback=None,
        )
        result = recognition.call(tmp_path)

        if result.status_code != 200:
            print(f"[ASR] 失败: {result.message}")
            return ""

        sentences = result.get_sentence()
        if not sentences:
            return ""

        text = "".join(s.get("text", "") for s in sentences)
        print(f"[ASR] → {text}")
        return text
    except Exception as e:
        print(f"[ASR] 异常: {e}")
        return ""
    finally:
        os.unlink(tmp_path)


# ============================================================
#  第2步: LLM — 文字 → 回复
# ============================================================
async def llm_chat(user_text: str, history: list[dict] = None) -> str:
    if not user_text.strip():
        return ""

    client = OpenAI(api_key=DEEPSEEK_KEY, base_url=DEEPSEEK_URL)
    if history is None:
        history = []

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT}
    ] + history + [
        {"role": "user", "content": user_text}
    ]

    response = client.chat.completions.create(
        model="deepseek-v4-pro",
        messages=messages,
        temperature=0.7,
        max_tokens=400,
        stream=False,
    )
    reply = response.choices[0].message.content
    print(f"[LLM] → {reply}")
    return reply


# ============================================================
#  第3步: TTS — 文字 → MP3
# ============================================================
TTS_RETRIES = 2

async def text_to_speech(text: str, voice: str = "zh-CN-XiaoyiNeural") -> bytes:
    # 强制 IPv4：手机热点/部分校园网的 IPv6 路由不可用，
    # aiohttp 先连 IPv6 失败后不会回退到 IPv4，直接抛 WinError 64
    last_err: Exception | None = None
    for attempt in range(TTS_RETRIES + 1):
        connector = aiohttp.TCPConnector(family=socket.AF_INET)
        try:
            communicate = edge_tts.Communicate(text, voice, connector=connector)
            mp3_buf = io.BytesIO()
            async for chunk in communicate.stream():
                if chunk["type"] == "audio":
                    mp3_buf.write(chunk["data"])
            mp3 = mp3_buf.getvalue()
            print(f"[TTS] → {len(mp3)} bytes MP3")
            return mp3
        except Exception as e:
            last_err = e
            print(f"[TTS] 连接失败(第{attempt + 1}次): {e}")
            if attempt < TTS_RETRIES:
                await asyncio.sleep(0.5)
        finally:
            await connector.close()
    raise last_err


# ============================================================
#  第4步: MP3 → PCM（给 ESP32 播放用）
# ============================================================
def mp3_to_pcm(mp3_bytes: bytes, sample_rate: int = 16000) -> bytes:
    """ffmpeg 把 MP3 转成 16-bit mono PCM 原始字节"""
    ffmpeg = find_ffmpeg()

    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f:
        f.write(mp3_bytes)
        mp3_path = f.name
    wav_path = mp3_path + ".wav"

    try:
        subprocess.run([
            ffmpeg, "-y", "-i", mp3_path,
            "-ar", str(sample_rate), "-ac", "1",
            "-sample_fmt", "s16", "-loglevel", "error",
            "-f", "wav", wav_path,
        ], check=True, capture_output=True)

        with wave.open(wav_path, "rb") as wf:
            pcm = wf.readframes(wf.getnframes())
        return pcm  # raw int16_t LE bytes, ESP32 可直接用
    finally:
        os.unlink(mp3_path)
        os.unlink(wav_path)


# ============================================================
#  串联: WAV → 文字 → LLM → TTS → PCM
# ============================================================
async def voice_pipeline(wav_bytes: bytes, voice:int = 0,history: list[dict] = None) -> tuple[bytes, str, str]:
    """一次调用走完四步，返回 (pcm_bytes, user_text, ai_text)"""
    print("\n" + "-" * 40)
    t_all=time.perf_counter()

    t=time.perf_counter()
    user_text = await speech_to_text(wav_bytes)
    t_asr=time.perf_counter()-t

    if not user_text:
        return b"", "", ""
    t=time.perf_counter()
    reply = get_special_reply(user_text)
    if reply:
        ai_text = reply
        print(f"[Special Reply] → {ai_text}")
    else:
        ai_text = await llm_chat(user_text, history)
    
    if not ai_text:
        return b"", user_text, ""
    t_llm=time.perf_counter()-t

    t=time.perf_counter()
    mp3       = await text_to_speech(ai_text,voice=VOICE_LIST[voice])
    t_tts=time.perf_counter()-t

    t=time.perf_counter()
    pcm       = mp3_to_pcm(mp3, 16000)
    t_pcm=time.perf_counter()-t

    t_end=time.perf_counter()-t_all
    print(f"[Done] {len(wav_bytes)}B → '{user_text}' → '{ai_text}' → {len(pcm)}B PCM "
          f"[Time] ASR={t_asr:.2f} LLM={t_llm:.2f} TTS={t_tts:.2f} PCM={t_pcm:.2f} Total={t_end:.2f}")
    return pcm, user_text, ai_text
