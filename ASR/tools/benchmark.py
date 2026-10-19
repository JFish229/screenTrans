"""Replay PCM16/16kHz/mono WAV in real time; report measured ASR events/CER.

Token/speech timing is not ground truth. Onset here is an amplitude estimate;
CER requires a supplied transcript. Use genuine meeting recordings for acceptance.
"""
import argparse
import array
import ctypes as C
import json
import math
import statistics
import time
import unicodedata
import wave
from pathlib import Path
from asr_client import Client


def normalize(s):
    return "".join(c.lower() for c in s if unicodedata.category(c)[0] in "LN")


def cer(reference, hypothesis):
    reference, hypothesis = normalize(reference), normalize(hypothesis)
    if not reference:
        raise ValueError("Empty reference")
    # Keep S/D/I separately as well as total edit distance.
    previous = [(i, 0, 0, i) for i in range(len(hypothesis) + 1)]
    for i, left in enumerate(reference, 1):
        row = [(i, 0, i, 0)]
        for j, right in enumerate(hypothesis, 1):
            if left == right:
                row.append(previous[j - 1])
            else:
                d, s, delete, insert = previous[j - 1]
                substitution = (d + 1, s + 1, delete, insert)
                d, s, delete, insert = previous[j]
                deletion = (d + 1, s, delete + 1, insert)
                d, s, delete, insert = row[j - 1]
                insertion = (d + 1, s, delete, insert + 1)
                row.append(min((substitution, deletion, insertion), key=lambda x: x[0]))
        previous = row
    distance, substitutions, deletions, insertions = previous[-1]
    return {"cer": distance / len(reference), "reference_characters": len(reference),
            "substitutions": substitutions, "deletions": deletions, "insertions": insertions}


def distribution(values):
    if not values:
        return None
    values = sorted(values)
    return {"p50": statistics.median(values), "p95": values[max(0, math.ceil(.95 * len(values)) - 1)], "count": len(values)}


def main():
    p = argparse.ArgumentParser()
    for option in ("dll", "model", "vad", "wav", "output"):
        p.add_argument("--" + option, required=True)
    p.add_argument("--reference"); p.add_argument("--threads", type=int, default=8)
    p.add_argument("--cpu", action="store_true")
    p.add_argument("--timeout", type=float, default=120)
    p.add_argument("--window-ms", type=int, default=8000)
    p.add_argument("--first-partial-ms", type=int, default=512)
    a = p.parse_args()
    with wave.open(a.wav, "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000, 1, 2):
            raise ValueError("Expected 16 kHz mono PCM16 WAV")
        audio = array.array("h", wav.readframes(wav.getnframes()))
    onset = next((i for i, v in enumerate(audio) if abs(v) > 330), 0) / 16000
    audio.extend([0] * 8000)
    c = Client(a.dll)
    loading = time.monotonic()
    config = c.config(a.model, a.vad, a.threads, not a.cpu)
    config.max_window_ms = a.window_ms
    config.first_partial_ms = a.first_partial_ms
    h = c.open(config)
    startup_ms = (time.monotonic() - loading) * 1000
    events, failures = [], []
    begin = time.monotonic()

    def poll():
        while True:
            e = c.poll_event(h)
            if e is None:
                break
            e["wall_ms"] = (time.monotonic() - begin) * 1000
            e["audio_end_to_event_ms"] = e["wall_ms"] - e["end_sample"] / 16
            events.append(e)
            if e["type"] in (3, 4):
                failures.append(e["text"])
            print(json.dumps(e, ensure_ascii=False), flush=True)
    try:
        for offset in range(0, len(audio), 320):
            due = begin + offset / 16000
            while time.monotonic() < due:
                poll(); time.sleep(min(.005, max(0, due - time.monotonic())))
            frame = audio[offset:offset + 320]
            pcm = (C.c_float * len(frame))(*(v / 32768 for v in frame))
            status = c.push(h, pcm, len(pcm))
            if status != 0:
                failures.append(f"push status {status}")
            poll()
        c.flush(h)
        deadline = time.monotonic() + a.timeout
        while not events or events[-1]["type"] != 5:
            poll()
            if time.monotonic() >= deadline:
                raise TimeoutError("ASR drain timed out")
            time.sleep(.005)
    finally:
        c.destroy(h)
    subtitles = [e for e in events if e["type"] in (1, 2)]
    drafts = [e for e in subtitles if e["type"] == 1 and e["text"]]
    transcript = "".join(e["text"] for e in events if e["type"] == 2)
    report = {"model": str(Path(a.model).resolve()), "wav": str(Path(a.wav).resolve()),
              "threads": a.threads, "window_ms": a.window_ms, "first_partial_ms": a.first_partial_ms,
              "gpu_requested": not a.cpu, "startup_and_warmup_ms": startup_ms,
              "first_draft_from_estimated_onset_ms": drafts[0]["wall_ms"] - onset * 1000 if drafts else None,
              "inference_ms": distribution([e["inference_ms"] for e in subtitles]),
              "queue_ms": distribution([e["queue_ms"] for e in subtitles]),
              "audio_end_to_event_ms": distribution([e["audio_end_to_event_ms"] for e in subtitles]),
              "transcript": transcript, "failures": failures, "events": events}
    if a.reference:
        report["accuracy"] = cer(Path(a.reference).read_text(encoding="utf-8-sig"), transcript)
    Path(a.output).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "events"}, ensure_ascii=False, indent=2))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
