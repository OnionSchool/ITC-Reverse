# ITC 广播系统复现

这是对样本中 IP 广播 2.0（V2.3.44）控制面的一份干净室重写，不包含或调用原始二进制、原始媒体及厂商协议实现。

## 已确认的原始结构

- `C_Proxy` 监听控制命令，`ConfigTerm.ini` 显示控制端口为 TCP 8000、数据端口为 TCP 15001。
- `C_Media` 持有终端和播放会话；会话具有名称、类型、优先级、源地址/端口、播放状态与播放时间。
- `C_Task` 从任务库调度定时铃声、课程和节目任务；`C_RTime` 通过控制面创建实时采播会话。
- `C_Forwd` 将媒体转发到终端侧，支持 `225.101.1.1` 到 `225.101.1.253` 的组播地址池。
- 服务守护器每 5000 ms 检查媒体、代理、中继、实时、任务等服务。

静态字符串已确认控制命令族包括 `logon`、`quit`、`session new`、`session set`、`session get`、`session add_term`、`session rm_term` 与 `session rm`。原协议的完整二进制帧格式、文件传输通道和终端媒体协议尚未经过隔离抓包验证，因此本项目将它们明确留为兼容层扩展点。

## 运行

```bash
python3 -m itc_reimplementation.server --username admin --password admin
```

服务只绑定回环地址 `127.0.0.1`；除非显式传入 `--host`，不会对校园网暴露。控制协议为 UTF-8、每行一条命令。示例：

```text
logon 0 admin admin
session new Demo 400 10 1
session add_term 1 ,1001
session set 1 STAT=1 PLAY_TIME=12
session get 1 STAT
quit
```

响应均以原样本中可见的 `000` 成功码和 `5xx` 错误码开头。数据端口目前保留并返回明确错误，避免伪装为已实现的媒体或文件传输能力。

## 导入旧终端配置

导入器仅读取 `DB_Server.mdb` 的 `DB_Term`、`DB_Group` 和 `DB_GroupMember`，输出独立 JSON；不会修改源数据库，也不导出用户密码、日志和媒体库。

```bash
nix shell nixpkgs#mdbtools --command python3 -m itc_reimplementation.importer \
  ITC/ITCCAST.2349/Server/CTNB_DATA/DB_Server.mdb legacy-configuration.json
```

如果 `mdb-export` 不在 `PATH` 中，可用 `--mdb-export /nix/store/.../bin/mdb-export` 指定其绝对路径。

## 验证与下一步

```bash
python3 -m unittest discover -s tests -v
```

要实现与硬件终端的真实互通，应在无默认路由的 Windows 虚拟机中抓取授权测试设备流量，首先验证登录响应、命令分隔符、数据端口握手、会话状态值与媒体 RTP/私有帧格式。
