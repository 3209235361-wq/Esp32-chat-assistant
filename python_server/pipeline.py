# -*- coding: utf-8 -*-
"""
ASR → LLM → TTS → PCM 完整流水线

   WAV ──Paraformer──→ 文字 ──DeepSeek──→ 回复 ──EdgeTTS──→ MP3 ──ffmpeg──→ PCM

依赖: pip install dashscope openai edge-tts
"""

import os
import io
import asyncio
import subprocess
import tempfile
import wave

import dashscope
from dashscope.audio.asr import Recognition
from openai import OpenAI
import edge_tts

# ============ 配置 ============
DASHSCOPE_KEY = os.getenv("DASHSCOPE_API_KEY", "")
DEEPSEEK_KEY  = os.getenv("DEEPSEEK_API_KEY",  "")
DEEPSEEK_URL  = "https://api.deepseek.com"
SYSTEM_PROMPT = "你是一个友好的语音助手，回答简洁，不超过3句话。"


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
async def text_to_speech(text: str, voice: str = "zh-CN-XiaoyiNeural") -> bytes:
    communicate = edge_tts.Communicate(text, voice)
    mp3_buf = io.BytesIO()
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            mp3_buf.write(chunk["data"])
    mp3 = mp3_buf.getvalue()
    print(f"[TTS] → {len(mp3)} bytes MP3")
    return mp3


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
async def voice_pipeline(wav_bytes: bytes, history: list[dict] = None
                         ) -> tuple[bytes, str, str]:
    """一次调用走完四步，返回 (pcm_bytes, user_text, ai_text)"""
    print("\n" + "-" * 40)
    user_text = await speech_to_text(wav_bytes)
    if not user_text:
        return b"", "", ""
    ai_text   = await llm_chat(user_text, history)
    if not ai_text:
        return b"", user_text, ""
    mp3       = await text_to_speech(ai_text)
    pcm       = mp3_to_pcm(mp3, 16000)
    print(f"[Done] {len(wav_bytes)}B → '{user_text}' → '{ai_text}' → {len(pcm)}B PCM")
    return pcm, user_text, ai_text
