# FlexMeet 流式 ASR

新版默认采用多语言 `base` + Silero VAD。客户端识别自己和远端的合并音频，显示可修订草稿和最终字幕。原来的三个 C ABI 导出保留，旧调用方仍使用完整缓冲转写语义。

## 已实现的行为

- 采集和网络仍使用 **16kHz / mono / float32**。miniaudio 负责设备原生格式的转换，不再重复降采样。
- 识别与播放分别混音：自己的麦克风直接进入识别，服务器回传的自己那份不重复加入，也不从扬声器播放。
- 每 20ms 推进音频时间线，没有数据时补零。共享关闭、静音时持续排空采集缓冲；捕获溢出、输入积压和推理队列过载都有日志及状态重置。
- Silero 使用连续的 512 样本帧；默认约 512ms 语音后开始草稿，每 400ms 更新，约 300ms 静音定稿。VAD 帧粒度会使实际门限向上取整。
- 连续讲话的窗口上限约 8秒，末尾 320ms 延后确认，下一窗口附带左侧 200ms 上下文。Whisper token 时间戳决定滚动边界两侧文本归属，不做全局重复词删除。时间戳是模型估计，实际边界准确率仍须用会议录音评估。
- PCM 入队与结果轮询不会执行推理。推理繁忙时合并过期草稿，排队上限默认 2秒；过载明确返回 `OVERFLOW` 并发出 `GAP`。
- 默认线程数为 `min(硬件线程数, 8)`，支持配置。零温度 Greedy、简体中文样式提示、最多 64 个提示 token；草稿上限 96 token，最终结果重新解码且不使用该上限。未缩小 `audio_ctx`。
- 退会先关闭连接，再等待线程，并取消旧 ASR 代次。DLL 缺失、导出不兼容或模型失败时显示字幕不可用，会议主体仍可使用。

## 重新构建

在仓库根目录的 PowerShell 中执行：

```powershell
# 官方源不通时可使用 -HostName hf-mirror.com；两者都校验固定 SHA256。
& .\ASR\tools\download-models.ps1 -HostName hf-mirror.com

# 完整 Release x64：解决方案、独立 Whisper/插件、测试、模型及运行库部署。
& .\ASR\tools\build-client.ps1 -Backend CPU

& .\x64\Release\ScreenTrans_Client.exe
```

本机已完成上述 CPU 构建与文件部署。解决方案的预编译 Whisper 库使用较新的 MSVC STL，构建脚本以 `/p:PlatformToolset=v145` 调用 VS 2026，保留项目文件中已有的工具集选择。客户端和服务端依赖库的构建顺序已补齐。

独立源码构建默认使用 VS 2022/v143，并由 CMake 获取固定提交、校验源码归档。可离线指定已有源码目录，例如本次下载的位置：

```powershell
& .\ASR\tools\build-client.ps1 -Backend CPU `
  -SourceDir "$PWD\ASR\.cache\source\whisper.cpp-6e4ab854f67f743900934a703d5603419384c961"
```

编译产物在 `ASR/build/CPU/Release/plugin`，脚本最后复制到 `x64/Release/plugin`。模型约 148MB 和 0.9MB；源码、模型、构建缓存均不加入 Git。锁定提交、模型版本及 SHA256 见 `dependencies.json`。

### CUDA

本机 RTX 4060 Laptop 8GB 可作为 CUDA 验证目标，但**当前未安装 CUDA Toolkit，尚未进行 GPU 编译和性能测试**。驱动 551.52 的 `nvidia-smi` 显示 CUDA 12.4，并不代表安装了 Toolkit。安装前需核对 NVIDIA 官方的驱动、Toolkit、MSVC 支持矩阵；不要直接假定 CUDA 支持 VS 2026 的 v145。

安装兼容组合并确保 `nvcc` 在 PATH 后，在相应开发终端执行：

```powershell
# Generator 和 Toolset 按实际已安装、受 Toolkit 支持的组合指定。
& .\ASR\tools\build-client.ps1 -Backend CUDA `
  -Generator 'Visual Studio 17 2022' -Toolset '<受支持的 MSVC 工具集>'
```

脚本将启用 `GGML_CUDA=ON`，通过 CMake 传递 CUDA 链接依赖。运行时还需可找到所用 CUDA 的运行库。部署到其他机器时同时验证驱动和运行库。没有 GPU 后端或初始化失败会明确记录 CPU 回退；仅设置 `use_gpu=1` 不作为启用 GPU 的证据。

如果之后从 VS 单独构建解决方案，会重新生成使用原有 CPU 库的插件；需要重新运行 CUDA 构建/部署步骤恢复 CUDA 插件。

## 配置和 C ABI

启动前可设置：

```powershell
$env:FLEXMEET_ASR_MODEL = "$PWD\ASR\models\ggml-base.bin"
$env:FLEXMEET_ASR_VAD = "$PWD\ASR\models\ggml-silero-v6.2.0.bin"
$env:FLEXMEET_ASR_THREADS = '8'
$env:FLEXMEET_ASR_GPU = '0' # 强制 CPU；默认请求 GPU，并允许 CPU 回退。
```

默认 DLL 和模型路径相对可执行文件定位，支持中文目录。接口声明见 `include/plugin/asr.h`，使用普通 C 结构布局、UTF-8 文本和 16kHz 单声道 PCM。所有 C 导出捕获内部 C++ 异常。

1. `default_config` 初始化结构，`create` 同步加载模型并预热；采集开始前调用。
2. `push` 复制新样本；`poll` 返回带语句 ID、修订号、类型和采样位置的事件。草稿按 ID 替换，最终事件只提交一次，包括清除草稿所需的空最终结果。
3. `BUFFER_SMALL` 返回所需字节数且不消费事件；重试时容量包括 NUL。
4. `flush` 异步排空，最终发出 `DRAINED`；之后不能 push，直到 reset。会议直接退出时使用 reset 取消，避免旧字幕进入下个房间。
5. `reset` 取消旧代次并清空输入/输出；`destroy` 等待工作线程退出。销毁前由调用者停止其他 API 调用。轮询只允许一个消费者。

`queue_ms` 是产生本次识别所用批次中最早样本入队至开始解码的时间，可能包括该批次内 VAD/前一段解码耗时；`inference_ms` 单独记录本次 Whisper 推理。它们不包含远端网络传输或硬件采集时间。

## 测试与结果

### 单机两个客户端没有字幕时

使用重新构建的 `x64/Release/ScreenTrans_Client.exe`。同机两个客户端只需一个开启
`share_audio`，另一个关闭，避免同一支麦克风被两路重复送入；两端都可以看到字幕。
展开 `settings -> plugin`：

- `Local microphone sharing` 显示自身发送开关；关闭仅影响自己的声音，仍可识别远端。
- `Capture: running` 且 samples 增长，表示采集回调有数据。说话时 `Microphone level` 应变化。
- `ASR input level` 是送到插件的本地与远端合并音量。自己的 sharing 关闭时，该条仍可显示远端音量。
- `ASR input` 应持续增长；`interruptions` 不应持续增长。输入有音量却没有字幕时保存控制台日志。
- `Audio samples lost` 表示真实缓冲溢出，`ASR overloaded` 表示推理队列过载；这些提示都不再声称字幕已恢复。

2026-10-20 修复了客户端将完整的 >200ms 到达批次裁短并反复重置 VAD 的问题。
本地、远端使用 1秒容量保留普通批次抖动，仅真实丢样才重置；代价是较大批次可能增加播放/字幕延迟。
同时停用未共享时的连续抓屏/编码，以及空视频队列的忙等，降低同机双客户端的无效负载。
插件还会在已有新输入积压时跳过当前过期草稿，先消费新 PCM；保留定稿，不通过扩大队列来掩盖积压。
回归覆盖 240ms 批次的完整消费和真实容量溢出，不能代替具体麦克风与真实界面的人工验证。

完整构建脚本运行 CTest：分段、混音、并发工作线程及指标计算。C 编译单元同时检查公开头文件可用于 C。额外运行真实 DLL/Silero 集成测试：

```powershell
python .\ASR\tests\api_test.py `
  --dll .\x64\Release\plugin\plugin_ASR.dll `
  --model .\x64\Release\plugin\ggml-base.bin `
  --vad .\x64\Release\plugin\ggml-silero-v6.2.0.bin
```

用人工标注会议录音测试：输入文件为 **16kHz mono PCM16 WAV**，文本参考为 UTF-8。脚本按 20ms 实时回放，记录完整事件和 P50/P95；CER 去除标点、空白并忽略字母大小写，不自动合并繁简体。

```powershell
python .\ASR\tools\benchmark.py `
  --dll .\x64\Release\plugin\plugin_ASR.dll `
  --model .\ASR\models\ggml-base.bin `
  --vad .\ASR\models\ggml-silero-v6.2.0.bin `
  --wav '<会议录音.wav>' --reference '<人工转录.txt>' `
  --threads 8 --cpu --output .\ASR\.cache\meeting-result.json
```

首个草稿时间使用波形振幅估计起声点；`audio_end_to_event_ms` 从事件所覆盖音频窗口末尾计时，**不等于从最后一个字说完到显示的完整延迟**，也未覆盖会议网络链路。需要精确端到端验收时应使用带采集时间标记、同步时钟和人工语音边界的多客户端实录。

本次离线合成音频测试、原始事件和限制见 `verification/RESULTS.md`。这段样本的 CPU 首次草稿已在 1秒以内，但尚不能声明真实会议端到端 0.5–1秒达标；真实会议准确率和 GPU 性能仍待验证。
