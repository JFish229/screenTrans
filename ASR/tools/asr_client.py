"""ctypes client for the public C ABI; no third-party Python dependencies."""
import ctypes as C
import os
from pathlib import Path


class Config(C.Structure):
    _fields_ = [("struct_size", C.c_uint32), ("abi_version", C.c_uint32),
                ("model_path", C.c_char_p), ("vad_model_path", C.c_char_p),
                *[(k, C.c_int32) for k in ("n_threads", "use_gpu", "first_partial_ms",
                  "partial_interval_ms", "silence_ms", "pre_roll_ms", "max_window_ms", "queue_ms")]]


class Result(C.Structure):
    _fields_ = [("struct_size", C.c_uint32), ("type", C.c_uint32),
                ("utterance_id", C.c_uint64), ("revision", C.c_uint64),
                ("start_sample", C.c_int64), ("end_sample", C.c_int64),
                ("text_bytes", C.c_uint32), ("reserved", C.c_uint32),
                ("inference_ms", C.c_double), ("queue_ms", C.c_double)]


class Client:
    def __init__(self, dll):
        dll = Path(dll).resolve()
        self.directory = os.add_dll_directory(str(dll.parent)) if os.name == "nt" else None
        self.lib = C.CDLL(str(dll))
        signatures = {
            "default_config": ([C.POINTER(Config)], None),
            "create": ([C.POINTER(Config), C.POINTER(C.c_void_p), C.c_char_p, C.c_uint32], C.c_int),
            "push": ([C.c_void_p, C.POINTER(C.c_float), C.c_uint32], C.c_int),
            "poll": ([C.c_void_p, C.POINTER(Result), C.c_char_p, C.c_uint32], C.c_int),
            "flush": ([C.c_void_p], C.c_int), "reset": ([C.c_void_p], C.c_int),
            "destroy": ([C.c_void_p], None),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.lib, "plugin_asr_stream_" + name)
            fn.argtypes, fn.restype = args, result
            setattr(self, name, fn)

    def config(self, model, vad, threads=4, gpu=True):
        config = Config()
        self.default_config(C.byref(config))
        config.model_path = str(Path(model).resolve()).encode("utf-8")
        config.vad_model_path = str(Path(vad).resolve()).encode("utf-8")
        config.n_threads, config.use_gpu = threads, int(gpu)
        return config

    def open(self, config):
        handle, error = C.c_void_p(), C.create_string_buffer(2048)
        status = self.create(C.byref(config), C.byref(handle), error, len(error))
        if status != 0:
            raise RuntimeError(f"ASR create ({status}): {error.value.decode('utf-8')}")
        return handle

    def poll_event(self, handle):
        result = Result(struct_size=C.sizeof(Result))
        text = C.create_string_buffer(4096)
        status = self.poll(handle, C.byref(result), text, len(text))
        if status == -2:
            text = C.create_string_buffer(result.text_bytes)
            status = self.poll(handle, C.byref(result), text, len(text))
        if status < 0:
            raise RuntimeError(f"ASR poll: {status}")
        if status == 0:
            return None
        return {k: getattr(result, k) for k, _ in Result._fields_} | {"text": text.value.decode("utf-8")}
