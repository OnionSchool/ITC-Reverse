# ITC 兼容控制台

原生 Win32/C++17 管理工具，发布目标为 Windows 7 SP1 及以上的 x86 系统；64 位 Windows 可直接运行该 32 位程序。它管理本地终端与分组配置，并提供已确认的最小 TCP 8000 控制面；TCP 15001 会返回未实现提示，不伪装媒体互通。

## 构建

```bash
nix build --print-out-paths nixpkgs#pkgsCross.mingw32.windows.mingw_w64.dev
nix build --print-out-paths nixpkgs#pkgsCross.mingw32.stdenv.cc
export MINGW_SYSROOT=/nix/store/<mingw-w64-dev>
export MINGW_GCC_TOOLCHAIN=/nix/store/<i686-w64-mingw32-gcc>
cmake -S native -B build-win32 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=native/toolchains/clang-mingw32.cmake
cmake --build build-win32
```

`clang++` 必须在 `PATH` 中；NixOS 可用 `nix shell nixpkgs#clang` 提供。以上路径以两条 `nix build --print-out-paths` 的输出替换，前者是 Windows 头文件和导入库，后者提供 C++ 标准库。CMake 使用 `clang++ --target=i686-w64-windows-gnu`，不使用 GCC 编译。

程序只使用 Windows 7 自带的 Win32、Common Controls 与 Winsock。默认绑定 `127.0.0.1`，管理员可在界面中启动或停止控制服务。配置保存到可执行文件同目录的 `itc-configuration.tsv`。

“导入旧 Access”通过 Windows 内置的 Jet 4.0 OLE DB 提供程序直接只读访问 MDB，不依赖 `mdb-export.exe`，也不会修改源数据库。为避免 64 位 Access 驱动差异，发布目标固定为 32 位；Windows 7 SP1 通常已包含所需的 Jet 组件。

`.github/workflows/windows-build.yml` 使用 Clang/Mingw-w64 构建静态运行时的 32 位 `ITCCompat.exe`，并作为 `ITCCompat-win32` 构建产物上传。该目标配置为 Windows 7；最终兼容性仍应在 Windows 7 SP1 x86 和 x64 虚拟机各执行一次冒烟测试确认。

控制面已实现静态确认的 `logon`、`quit` 与 `session new/list/terms/set/get/add_term/rm_term/source/playvol/rm` 命令，且仅绑定回环地址。其请求格式、响应文本与状态枚举尚未经过隔离抓包验证，不能视为已与旧客户端或硬件终端互通；TCP 15001、媒体播放、组播转发、实时采播与定时任务仍未实现。
