# FlexMeet v2 双机联测与诊断（2026-10-22）

本版每位说话者只识别自己的麦克风；服务器统一广播字幕，发送者也显示服务器确认的定稿。接收者关闭 share_audio 仍能收到字幕，但不会自行再次识别远端音频。

## 发给组员什么

运行仓库根目录的 `& .\ASR\tools\package-test.ps1`。生成的 ZIP 已包含客户端、服务器、三个播放运行库、plugin_ASR.dll、base 和 Silero 模型、文件哈希及本文；直接完整解压到独立文件夹。

不要只替换 DLL：本次会议协议为 `0x464d0002`，必须同时更新服务器与全部客户端。插件 C ABI 仍为 v1，旧导出及公开结构布局未改变。不要复制个人的 FlexMeet.ini/imgui.ini，也不需要 PDB、LIB、源码、VS 或候选 small 模型。若缺少 MSVCP/VCRUNTIME，安装 Microsoft Visual C++ 2015–2022 x64 Redistributable。

## 两个人的测试步骤

1. 两台电脑处于同一局域网，各自戴耳机。在 Windows 设置允许桌面程序访问麦克风，选对默认录音设备。先关闭视频共享。
2. 服务端电脑启动 `ScreenTrans_Server.exe`，输入一个空闲端口，例如 `33100`；在 `ipconfig` 查看该电脑的局域网 IPv4。首次启动按系统提示允许专用网络访问；无法连接时检查端口、防火墙与 AP 客户端隔离。
3. A、B 均启动包里的 `ScreenTrans_Client.exe`，填写服务端 IPv4、相同端口和不同名字。A 创建带非零密码的房间，B 输入房间号与密码加入。
4. A 开 share_audio，B 关闭。A 读三句正常中文；两边都应先看到可替换草稿，停顿后看到同一来源、同一文字、同一顺序的定稿。B 的本机 ASR 输入计数不应因听到远端而持续增加；这在 v2 是正常行为。
5. A 关闭 share_audio：其未定稿应清除，之后不再输出自己的识别。角色互换，B 开麦读同样三句，检查 A 仍接收字幕。不要要求接收者开麦才能看到字幕。
6. 各自测试“好、对、嗯、对对对、哈哈”、轻声、项目术语、短停顿和连续讲话超过 8 秒。记录漏字或被误删的原话；短回答允许通过严格质量门控，不做单字黑名单。
7. 双方关闭共享静置一分钟，然后单端开麦但不讲话，分别测试底噪、键盘声、风扇声。检查有无单字刷屏。高通只能减弱低频，不能消除所有键盘声。
8. 测试静音恢复、退会重进、房主移交和断线重连；检查草稿取消、最近 32 条非空定稿同步、没有重复定稿或旧草稿复活。
9. 最后加入视频共享和正常会议负载，连续运行至少 10 分钟。观察本机 capture/ASR samples、interruptions，记录字幕延迟是否越来越大。多人同时说话按服务器提交顺序显示，不提供说话重叠分离。

模型不可用时，该说话来源显示字幕不可用；接收端不会另起识别产生不同文本。仅接收字幕时不依赖接收者识别成功。实际麦克风/界面、跨机网络和长时间视频负载需要以上现场验收；自动化测试不能替代。

## 构建与自动化回归

在仓库根目录 PowerShell 执行（需要已安装的 VS、C++ 工具链和 Windows SDK）：

```powershell
& .\ASR\tools\build-client.ps1 -Backend CPU
python .\ASR\tests\api_test.py --dll .\x64\Release\plugin\plugin_ASR.dll --model .\ASR\models\ggml-base.bin --vad .\ASR\models\ggml-silero-v6.2.0.bin
python .\ASR\tests\meeting_protocol_test.py --server .\x64\Release\ScreenTrans_Server.exe --dll .\x64\Release\plugin\plugin_ASR.dll --model .\ASR\models\ggml-base.bin --vad .\ASR\models\ggml-silero-v6.2.0.bin --wav .\ASR\.cache\synthetic\zh.wav --output .\ASR\.cache\protocol.json
python .\ASR\tests\noise_test.py --dll .\x64\Release\plugin\plugin_ASR.dll --model .\ASR\models\ggml-base.bin --vad .\ASR\models\ggml-silero-v6.2.0.bin --output .\ASR\.cache\noise.json
```

若没有合成样本，先运行 `& .\ASR\tools\make-synthetic-fixture.ps1`；需要 Windows 已安装的中文离线语音。构建脚本将固定源码生成的 CPU 插件最后部署到 Release，避免被解决方案中的旧预编译后端覆盖。

## 录音、哈希与逐样本对比

录音默认关闭。两台机器分别在仓库根目录启动：

```powershell
& .\ASR\tools\start-diagnostic.ps1 -OutputDirectory .\ASR\.cache\trace-A
# B 机器把目录改为 trace-B；服务端也可传 -Executable .\x64\Release\ScreenTrans_Server.exe
```

也可直接设置 `$env:FLEXMEET_ASR_TRACE_DIR = 'C:\ASR-traces\A'` 后启动 EXE。环境变量支持中文路径。结束测试后正常退出客户端；关闭录音用 `Remove-Item Env:FLEXMEET_ASR_TRACE_DIR`，然后重启。日志可能包含语音和文字，应只录制双方同意的测试内容。

输出为 `.jsonl` 索引和对应 `.f32`，原始音频为 16kHz 单声道 float32 little endian。索引包含来源、代次、序号、起始样本、RMS、峰值、削波采样数、丢记录数。插件记录高通前后 PCM、每帧 VAD 概率、质量拒绝原因、窗口、文字与推理耗时；创建流时记录实际参数和模型/VAD/DLL SHA256。启动脚本另写程序文件哈希和环境配置。

```powershell
python .\ASR\tools\trace-audio.py --left '<A的client-send索引.jsonl>' --right '<B的client-receive索引.jsonl>' --source 1 --wav-dir .\ASR\.cache\wav-preview
```

`source` 取日志实际值。比较相同测试区间：先核对缺失/多余块与 `dropped_records`，再判断字节是否一致；停止录音时最后几个在途块缺失不能直接称为网络失真。工具对不完整或有丢记录的诊断明确报错，不用补零掩盖断点。试听 WAV 为 PCM16 派生预览，逐位比较使用原始 float32。字幕文本和显示提交编号见 `client-captions` 索引。

后台写盘队列上限 4MiB/2048 条；只在内存队列短暂加锁，不在音频线程进行磁盘 I/O。满队列会记录丢记录数量，不能用这样的采集做严格比较。远端播放的补零/抖动现在完全不影响本机 ASR；诊断输入是本地分支，不再人为补播放静音。

## 调优与模型

所有参数在创建流时读取，修改后重启客户端：

| 环境变量 | 默认 | 用途 |
|---|---:|---|
| FLEXMEET_ASR_MODEL | plugin/ggml-base.bin | 多语言模型完整路径 |
| FLEXMEET_ASR_THREADS | 8 或更少硬件线程 | CPU 线程数 |
| FLEXMEET_ASR_GPU | 1 | 仅在已编译且发现 GPU 后端时启用，否则 CPU |
| FLEXMEET_ASR_HIGHPASS_HZ | 80 | 0 关闭；允许 0–200Hz |
| FLEXMEET_ASR_VAD_START | 0.65 | 起声门限，最近三帧至少两帧满足 |
| FLEXMEET_ASR_VAD_HOLD | 0.35 | 保持门限，不得大于起声门限 |
| FLEXMEET_ASR_QUALITY | 1 | 0 关闭新增解码质量门控，仅用于对照 |
| FLEXMEET_ASR_HYSTERESIS | 1 | 0 使用单门限 0.5，仅用于对照 |

首稿 512ms、草稿间隔 400ms、句尾约 300ms、前置约 200ms、窗口 8秒；分段按 32ms VAD 帧量化。约 96–300ms 回答要求更强 VAD、相对能量及 token 置信度。纯标点丢弃，低质量重复拒绝，高置信度真实重复保留。拒绝的定稿清除草稿但不污染提示历史。

下载可选模型（固定版本和 SHA256 在 `ASR/dependencies.json`，构建不强制依赖候选）：

```powershell
& .\ASR\tools\download-models.ps1 -HostName hf-mirror.com -Candidates ggml-base-q5_1.bin,ggml-small-q5_1.bin,ggml-small.bin
$env:FLEXMEET_ASR_MODEL = (Resolve-Path .\ASR\models\ggml-small-q5_1.bin).Path
.\x64\Release\ScreenTrans_Client.exe
```

本机结果不支持把 small 设为实时默认；base-q5_1 主要减小内存，不能当作准确率升级。新的实测摘要在 `RESULTS-v2.md`。

同录音回放对照，输入必须是 16kHz mono PCM16 WAV，参考文本为 UTF-8：

```powershell
python .\ASR\tools\compare-models.py --dll .\x64\Release\plugin\plugin_ASR.dll --models .\ASR\models --wav '<录音.wav>' --reference '<人工转录.txt>' --output-dir .\ASR\.cache\meeting-comparison
```

此脚本顺序测试各模型 4/8 线程，保留溢出失败。不要把合成语音的 CER 当作实际会议准确率。人工数据还需覆盖轻声、键盘噪声、术语、短回答和真实重复；参考稿不能事先注入识别提示。
