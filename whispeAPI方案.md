# whisper.cpp C API方案

## 一、封装层代码

### 1. 头文件 `FlexMeetASR.h`

```c
// FlexMeetASR.h — 纯 C ABI 导出接口
#ifndef FLEXMEET_ASR_H
#define FLEXMEET_ASR_H

#ifdef _WIN32
    #ifdef FLEXMEET_ASR_EXPORTS
        #define FLEXMEET_API __declspec(dllexport)
    #else
        #define FLEXMEET_API __declspec(dllimport)
    #endif
#else
    #define FLEXMEET_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 初始化模型，加载 ggml 模型文件
// model_path: ggml-base.bin 等模型文件的路径
// 返回 0 成功，-1 失败
FLEXMEET_API int flexmeet_asr_init(const char* model_path);

// 核心函数：输入 PCM float 数组（16kHz 单声道），返回识别文本（UTF-8）
// pcm_data: 32-bit float PCM 数组指针
// n_samples: 采样点数量
// 返回的字符串由 DLL 内部管理，调用方不要 free，下次调用或释放时会被覆盖
FLEXMEET_API const char* flexmeet_asr_transcribe(const float* pcm_data, int n_samples);

// 释放所有资源
FLEXMEET_API void flexmeet_asr_free();

#ifdef __cplusplus
}
#endif

#endif
```

### 2. 实现文件 `FlexMeetASR.cpp`

```cpp
// FlexMeetASR.cpp — 基于 whisper.cpp 的 C API 封装
#define FLEXMEET_ASR_EXPORTS
#include "FlexMeetASR.h"

// whisper.cpp 的 C API 头文件
#include "whisper.h"

#include <string>
#include <vector>
#include <mutex>

// ============================================================
// 内部状态
// ============================================================
static struct whisper_context* g_ctx = nullptr;
static std::string g_last_result;
static std::mutex g_mtx;  // 保护 g_ctx 和 g_last_result

// ============================================================
// 初始化
// ============================================================
int flexmeet_asr_init(const char* model_path) {
    std::lock_guard<std::mutex> lock(g_mtx);

    if (g_ctx) {
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }

    // 配置模型加载参数
    auto cparams = whisper_context_default_params();
    cparams.use_gpu = false;   // 纯 CPU 推理，不依赖 CUDA
    cparams.flash_attn = false;

    g_ctx = whisper_init_from_file_with_params(model_path, cparams);

    return g_ctx ? 0 : -1;
}

// ============================================================
// 核心转写函数
// ============================================================
const char* flexmeet_asr_transcribe(const float* pcm_data, int n_samples) {
    std::lock_guard<std::mutex> lock(g_mtx);

    if (!g_ctx || !pcm_data || n_samples <= 0) {
        return "";
    }

    // 配置推理参数
    auto wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress   = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    wparams.translate        = false;
    wparams.language         = "zh";   // 默认中文识别
    wparams.n_threads        = 4;      // 使用 4 个线程推理

    // 执行推理
    if (whisper_full(g_ctx, wparams, pcm_data, n_samples) != 0) {
        return "";
    }

    // 拼接所有片段
    g_last_result.clear();
    const int n_seg = whisper_full_n_segments(g_ctx);
    for (int i = 0; i < n_seg; ++i) {
        const char* text = whisper_full_get_segment_text(g_ctx, i);
        if (text) {
            g_last_result += text;
        }
    }

    return g_last_result.c_str();
}

// ============================================================
// 释放资源
// ============================================================
void flexmeet_asr_free() {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_ctx) {
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }
    g_last_result.clear();
}
```


## 二、需要安装的工具

| 工具                   | 用途                                          | 下载地址                            |
| ---------------------- | --------------------------------------------- | ----------------------------------- |
| **Git for Windows**    | 拉取 whisper.cpp 源码                         | https://git-scm.com/download/win    |
| **CMake**（3.20+）     | 构建 whisper.cpp 库                           | https://cmake.org/download/         |
| **Visual Studio 2022** | 编译项目，需安装“使用 C++ 的桌面开发”工作负载 | https://visualstudio.microsoft.com/ |

安装 CMake 时记得勾选 **“Add CMake to system PATH”**。

## 三、编译 whisper.cpp 静态库

打开 **PowerShell** 或 **命令提示符**，执行：

```bash
# 1. 克隆仓库
git clone https://github.com/ggml-org/whisper.cpp.git
cd whisper.cpp

# 2. 用 CMake 生成 VS 工程
cmake -B build -A x64 -DBUILD_SHARED_LIBS=OFF

# 3. 编译 Release 版本
cmake --build build --config Release -j
```

编译完成后，你需要的文件在以下位置：

| 文件          | 路径                                          |
| ------------- | --------------------------------------------- |
| `whisper.h`   | `whisper.cpp/include/whisper.h`               |
| `whisper.lib` | `whisper.cpp/build/src/Release/whisper.lib`   |
| `ggml.lib`    | `whisper.cpp/build/ggml/src/Release/ggml.lib` |

> **注意**：`BUILD_SHARED_LIBS=OFF` 生成静态库，链接到你的 DLL 后，最终发布时不需要额外携带 whisper.dll 和 ggml.dll，部署更简单。

## 四、下载模型文件

Windows 下推荐直接手动下载：

```bash
# 在 whisper.cpp/models/ 目录下执行
curl -L -o ggml-base.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin
```

或使用仓库自带的脚本：

```bash
cd whisper.cpp
.\models\download-ggml-model.cmd base
```

模型选择参考：`ggml-tiny.bin`（约39MB，最快），`ggml-base.bin`（约74MB，推荐，兼顾速度与精度），`ggml-small.bin`（约244MB，更准确）。

## 五、集成到项目

### 第1步：创建 FlexMeetASR 项目

1. 在 Visual Studio 2026 中打开你的解决方案，**右键解决方案 → 添加 → 新建项目**。
2. 选择 **“动态链接库(DLL)”** 模板，项目名命名为 `FlexMeetASR`。
3. 将上面提供的 `FlexMeetASR.h` 和 `FlexMeetASR.cpp` 添加到该项目中。

### 第2步：配置包含目录和库目录

右键 `FlexMeetASR` 项目 → **属性**：

- **C/C++ → 常规 → 附加包含目录**：添加 `whisper.cpp\include` 和 `whisper.cpp\ggml\include` 的路径。
- **链接器 → 常规 → 附加库目录**：添加 `whisper.cpp\build\src\Release` 和 `whisper.cpp\build\ggml\src\Release`。
- **链接器 → 输入 → 附加依赖项**：添加 `whisper.lib` 和 `ggml.lib`。
- **C/C++ → 预处理器 → 预处理器定义**：确保有 `FLEXMEET_ASR_EXPORTS` 和 `WHISPER_SHARED` 或 `WHISPER_STATIC`（取决于你的 whisper 构建方式）。

### 第3步：集成到客户端

在 `App.hpp` 中引入头文件并添加成员：

```cpp
// App.hpp 顶部
#include "FlexMeetASR.h"

// App 类中新增成员变量
class App {
    // ... 已有代码 ...
public:
    // ASR 相关
    bool asr_ready = false;
    std::vector<float> asr_audio_buffer;  // 累积的音频数据
    // ...
};
```

在 `App.cpp` 的 `App::Send()` 中，采集到音频后，将音频数据累积到 `asr_audio_buffer`，然后可以：

```cpp
// 在 Send() 函数中，采集到 ad_cpt.Frames() 后
// ad_cpt.Frames() 返回的是 int16_t 格式的音频
// 需要转换为 float 并重采样到 16kHz 单声道

// 初始化时（例如在 run() 或 Show() 中）：
// flexmeet_asr_init("models/ggml-base.bin");

// 转写：
// const char* text = flexmeet_asr_transcribe(asr_audio_buffer.data(), 
//                                             (int)asr_audio_buffer.size());
```

### 第4步：音频格式转换

你的项目使用 **48kHz 双声道** 音频（见 `App.hpp` 中的 `audio::sampleRate = 48000` 和 `audio::channels = 2`），而 whisper.cpp 要求 **16kHz 单声道 float** 格式。因此你需要做两步转换：

1. **重采样**：48kHz → 16kHz（采样率降到 1/3）
2. **声道混合**：双声道 → 单声道（左右声道取平均）
3. **格式转换**：`int16_t` → `float`（归一化到 [-1.0, 1.0]）

建议在 `FlexMeetASR.cpp` 中增加一个辅助函数来处理这个转换，或者在你的项目中新增一个 `AudioResampler` 工具类。由于你的 `ad_cpt.Frames()` 返回的是 `std::vector<int16_t>`，转换逻辑大致如下：

```cpp
// 将 48kHz 双声道 int16 转为 16kHz 单声道 float
std::vector<float> convert_to_whisper_format(const std::vector<int16_t>& input) {
    const int input_rate = 48000;
    const int output_rate = 16000;
    const int channels = 2;
    const int decimation = input_rate / output_rate;  // 3

    int n_frames = (int)input.size() / channels;
    std::vector<float> output;
    output.reserve(n_frames / decimation);

    for (int i = 0; i < n_frames; i += decimation) {
        // 取左右声道平均
        float left  = input[i * channels + 0] / 32768.0f;
        float right = input[i * channels + 1] / 32768.0f;
        output.push_back((left + right) * 0.5f);
    }
    return output;
}
```

> **更精确的做法**：使用 libswresample（FFmpeg 的一部分）进行高质量重采样。项目已经依赖 FFmpeg，可以直接调用 `swr_convert` 实现 48kHz 双声道 → 16kHz 单声道的转换。

## 六、注意事项

1. **音频格式**：whisper.cpp 要求 16kHz 单声道 32-bit float PCM，这是硬性要求。
2. **线程安全**：当前封装使用全局互斥锁，同一时刻只能处理一个转写请求。如果会议中有多人同时需要识别，需要为每个音频流创建独立的 `whisper_context`。
3. **流式识别**：如果希望“边说边出字”，需要将音频分成小片段（如每 2-3 秒），分片调用 `flexmeet_asr_transcribe`，而不是等一整段音频结束后再转写。
4. **模型路径**：建议将模型文件放在客户端可执行文件同目录下的 `models/` 文件夹中，初始化时使用相对路径。
5. **首次调用延迟**：`whisper_init_from_file_with_params` 加载模型需要一定时间（base 模型约 1-3 秒），建议在程序启动时提前初始化，或使用单独的线程异步初始化。

## 七、测试建议

建议先创建一个独立的控制台测试程序，加载一段 16kHz 单声道 WAV 文件，验证 `flexmeet_asr_transcribe` 能正确输出文字。验证通过后，再把封装好的 DLL 链接到客户端项目中。