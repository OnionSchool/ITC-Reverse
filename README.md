# ITC-Reverse

对 ITC IP 广播 2.0 V2.3.44 样本进行静态分析，并以干净室方式重写可验证控制面的项目。原始安装样本仅用于离线研究；重写程序不调用、分发或修改原始二进制。

## 项目状态

- 已确认：控制服务配置为回环地址 `127.0.0.1`、控制端口 `8000`、数据端口 `15001`；服务包含媒体、代理、任务、实时采播及中继模块。
- 已确认：中继配置使用 `225.101.1.1` 至 `225.101.1.253` 的组播地址池，TTL 为 `1`；媒体组件支持 MP3 Layer III 与 WAV/PCM。
- 已实现：Windows 7 SP1+ x86 原生配置管理界面、Jet 4.0 只读 MDB 导入、回环最小控制服务和 Npcap 离线抓包工具。
- 未验证：旧客户端认证报文、命令帧边界与响应格式、`15001` 数据通道、媒体封装、组播端口和终端互通。它们不会被猜测实现。

## 目录

| 路径 | 用途 |
| --- | --- |
| `ITC/` | 原始安装样本，只读保留。 |
| `native/` | Windows 7 兼容的 C++17 重写程序与 CMake 配置。 |
| `tools/` | Npcap 离线抓包命令脚本与操作流程。 |
| `itc_reimplementation/` | 早期 Python 协议参考实现，不作为发布版本。 |
| `tests/` | 早期参考实现的单元测试。 |

## 原生工具

构建产物包含以下两个 x86 程序：

- `ITCCompat.exe`：管理本地终端与分组；可用 Windows Jet 4.0 以只读方式导入 `DB_Server.mdb` 的 `DB_Term`、`DB_Group` 和 `DB_GroupMember`。本地配置写入程序同目录的 `itc-configuration.tsv`。
- `ITCCapture.exe`：从 Npcap 动态加载 `wpcap.dll`，枚举接口或将 TCP `8000`、TCP `15001` 与目标为 `225.101.1.0/24` 的 UDP 报文写入经典 PCAP；不依赖 Wireshark、PowerShell 或 `mdb-export.exe`。

`ITCCompat.exe` 的控制服务仅绑定回环地址，实现了静态字符串证据支持的最小 `logon`、`quit` 和 `session` 命令状态模型。`15001` 明确返回未实现，不应视为旧终端兼容服务。

原生构建、依赖和兼容性说明见 `native/README.md`。GitHub Actions 会用 Clang 为 i686 Windows 构建并上传两个程序；最终仍须在 Windows 7 SP1 的 x86 和 x64 虚拟机中冒烟测试。

## 离线抓包

在获得授权且没有默认网关、DNS、NAT 或公网连接的测试网络中，可使用 Npcap 和 `ITCCapture.exe` 获取旧系统的行为证据。抓包工具不发送登录、探测、ARP 或组播加入请求。

完整的网络隔离、接口编号、单机旧服务端抓取、哈希校验、脱敏和离线分析流程见 `tools/README.md`。不要在生产网络运行旧服务，也不要对 `8000`、`15001` 或组播地址手工发送猜测协议。

## 开发与验证

```bash
python3 -m unittest discover -s tests -v
```

该命令仅验证早期 Python 参考实现。Windows 程序的构建由 `.github/workflows/windows-build.yml` 执行；其当前 Clang/MSYS2 配置应以 CI 结果和 Windows 7 真机测试为准。

## 安全与证据边界

- 不要执行样本中的 `StartSvc.exe`、`StopSvc.exe`、`ServiceDog.exe`、`backup.bat` 或任何原服务二进制。
- 不要提交或上传抓包、`DB_Log.mdb`、Access 锁文件、明文配置、凭据、真实终端信息或媒体文件。
- 仅在已授权的隔离环境中操作，并保留原始 PCAP 的 SHA-256；分析与脱敏应始终在副本上完成。
