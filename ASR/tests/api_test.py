"""Real DLL/Silero integration checks, including overflow and session reset."""
import argparse
import ctypes as C
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asr_client import Client, Config, Result


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def wait_drained(client, handle):
    events = []
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        e = client.poll_event(handle)
        if e:
            events.append(e)
            require(e["type"] != 4, e["text"])
            if e["type"] == 5:
                return events
        time.sleep(.005)
    raise AssertionError("drain timed out")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--dll", required=True); p.add_argument("--model", required=True); p.add_argument("--vad", required=True)
    a = p.parse_args()
    c = Client(a.dll)
    c.lib.plugin_asr_init.argtypes, c.lib.plugin_asr_init.restype = [C.c_char_p], C.c_int
    c.lib.plugin_asr_transcribe.argtypes = [C.POINTER(C.c_float), C.c_int, C.POINTER(C.c_int)]
    c.lib.plugin_asr_transcribe.restype = C.c_char_p
    c.lib.plugin_asr_free.argtypes, c.lib.plugin_asr_free.restype = [], None
    count = C.c_int(123)
    require(c.lib.plugin_asr_init(None) == -1, "legacy invalid model accepted")
    require(c.lib.plugin_asr_transcribe(None, 0, C.byref(count)) is None and count.value == 0, "legacy ABI changed")
    require(c.lib.plugin_asr_transcribe(None, 0, None) is None, "legacy NULL output crashed")
    c.lib.plugin_asr_free()
    config = c.config(a.model, a.vad, gpu=False)
    error, handle = C.create_string_buffer(2048), C.c_void_p()
    config.abi_version = 999
    require(c.create(C.byref(config), C.byref(handle), error, len(error)) == -1 and not handle, "ABI mismatch accepted")
    config.abi_version = 1
    original_model = config.model_path
    config.model_path = b"missing-model.bin"
    require(c.create(C.byref(config), C.byref(handle), error, len(error)) == -3 and not handle, "missing model accepted")
    config.model_path = original_model
    h = c.open(config)  # Paths contain Chinese characters in the actual workspace.
    try:
        require(c.push(h, None, 1) == -1, "NULL PCM accepted")
        nan = (C.c_float * 1)(float("nan"))
        require(c.push(h, nan, 1) == -1, "NaN PCM accepted")
        zeros = (C.c_float * 16000)()
        require(c.push(h, zeros, len(zeros)) == 0, "silence push failed")
        require(c.flush(h) == 0 and c.push(h, zeros, 1) == -4, "push after flush accepted")
        events = wait_drained(c, h)
        require([e["type"] for e in events] == [5], "silence produced subtitles")
        require(c.reset(h) == 0, "reset failed")
        overflow = (C.c_float * 48000)()
        require(c.push(h, overflow, len(overflow)) == 2, "overload not reported")
        result = Result(struct_size=C.sizeof(Result))
        require(c.poll(h, C.byref(result), None, 0) == -2 and result.type == 3, "missing sized GAP event")
        require(c.poll(h, C.byref(result), None, 0) == -2, "sizing call consumed event")
        event = c.poll_event(h)
        require(event["type"] == 3 and event["start_sample"] == 16000, "overflow timeline incorrect")
        # Repeated resets invalidate in-flight work, including queued flush events.
        for _ in range(20):
            require(c.reset(h) == 0, "reset failed")
            require(c.push(h, zeros, 320) == 0, "push after reset failed")
            c.flush(h)
        require(c.reset(h) == 0, "last reset failed")
        c.flush(h)
        require([e["type"] for e in wait_drained(c, h)] == [5], "old meeting leaked results")
    finally:
        c.destroy(h)
    print("C ABI integration: invalid input, UTF-8 paths, silence, drain, overflow, sized poll, reset passed")


if __name__ == "__main__":
    main()
