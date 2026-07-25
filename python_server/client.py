"""
本地测试客户端 — 模拟 ESP32 发 PCM 正弦波，验证服务端流水线
"""

import asyncio
import struct
import math
import websockets


def gen_sine(freq=440, duration=2.0, amp=16000, sr=16000) -> bytes:
    """生成 PCM 正弦波 bytes（int16 LE），模拟 ESP32 录音数据"""
    n = int(sr * duration)
    samples = [int(amp * math.sin(2 * math.pi * freq * i / sr)) for i in range(n)]
    return struct.pack(f"<{n}h", *samples)


async def one_round(ws, duration=3.0):
    """模拟一轮对话：发正弦波 → 收结果"""
    total = int(16000 * duration)
    chunk = 3200

    await ws.send(f"START:{16000},16,1")

    for offset in range(0, total, chunk):
        n = min(chunk, total - offset)
        pcm = gen_sine(freq=440, duration=n / 16000)
        await ws.send(pcm)

    await ws.send("END")
    print(f"[发送] {duration}s 正弦波已发送，等待回复...")

    while True:
        msg = await ws.recv()
        if isinstance(msg, str):
            if msg == "SKIP":
                print("[跳过] 服务端认为音频无效")
                return
            if msg == "DONE":
                print("[完成] 一轮结束")
                return
            if msg.startswith("TEXT:"):
                parts = msg[5:].split("|", 1)
                print(f"  用户说: {parts[0]}")
                if len(parts) > 1:
                    print(f"  AI 回复: {parts[1]}")
        else:
            print(f"  收到 PCM: {len(msg)} bytes ({len(msg) / 32000:.1f}s)")


async def main():
    print("== ESP32 模拟客户端 ==")
    async with websockets.connect("ws://localhost:8006/chat") as ws:
        for r in range(3):
            print(f"\n--- 第 {r + 1} 轮 ---")
            await one_round(ws, duration=3.0)

        await ws.send("STOP")
        print("\n== 测试完成 ==")


if __name__ == "__main__":
    asyncio.run(main())
