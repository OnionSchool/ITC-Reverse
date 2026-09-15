#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
constexpr int ErrorBufferSize = 256;

struct Pcap;
struct BpfInstruction;
struct BpfProgram { unsigned int length; BpfInstruction* instructions; };
struct PcapAddress;
struct PcapInterface { PcapInterface* next; char* name; char* description; PcapAddress* addresses; unsigned int flags; };
struct TimeValue { long seconds; long microseconds; };
struct PcapHeader { TimeValue timestamp; unsigned int captured_length; unsigned int length; };
using DumpHandle = unsigned char;
using PacketHandler = void(__cdecl*)(unsigned char*, const PcapHeader*, const unsigned char*);
using FindAllDevices = int(__cdecl*)(PcapInterface**, char*);
using FreeAllDevices = void(__cdecl*)(PcapInterface*);
using OpenLive = Pcap*(__cdecl*)(const char*, int, int, int, char*);
using CompileFilter = int(__cdecl*)(Pcap*, BpfProgram*, const char*, int, unsigned int);
using SetFilter = int(__cdecl*)(Pcap*, BpfProgram*);
using FreeFilter = void(__cdecl*)(BpfProgram*);
using OpenDump = DumpHandle*(__cdecl*)(Pcap*, const char*);
using WriteDump = void(__cdecl*)(unsigned char*, const PcapHeader*, const unsigned char*);
using CloseDump = void(__cdecl*)(DumpHandle*);
using Loop = int(__cdecl*)(Pcap*, int, PacketHandler, unsigned char*);
using BreakLoop = void(__cdecl*)(Pcap*);
using Close = void(__cdecl*)(Pcap*);
using ErrorText = char*(__cdecl*)(Pcap*);

struct Api {
  HMODULE module{};
  FindAllDevices find_all_devices{};
  FreeAllDevices free_all_devices{};
  OpenLive open_live{};
  CompileFilter compile_filter{};
  SetFilter set_filter{};
  FreeFilter free_filter{};
  OpenDump open_dump{};
  WriteDump write_dump{};
  CloseDump close_dump{};
  Loop loop{};
  BreakLoop break_loop{};
  Close close{};
  ErrorText error_text{};
};

Api g_api;
Pcap* g_capture{};

template <typename T>
bool load(T& function, const char* name) {
  function = reinterpret_cast<T>(GetProcAddress(g_api.module, name));
  return function != nullptr;
}

bool initialize() {
  g_api.module = LoadLibraryA("wpcap.dll");
  return g_api.module && load(g_api.find_all_devices, "pcap_findalldevs") && load(g_api.free_all_devices, "pcap_freealldevs") && load(g_api.open_live, "pcap_open_live") && load(g_api.compile_filter, "pcap_compile") && load(g_api.set_filter, "pcap_setfilter") && load(g_api.free_filter, "pcap_freecode") && load(g_api.open_dump, "pcap_dump_open") && load(g_api.write_dump, "pcap_dump") && load(g_api.close_dump, "pcap_dump_close") && load(g_api.loop, "pcap_loop") && load(g_api.break_loop, "pcap_breakloop") && load(g_api.close, "pcap_close") && load(g_api.error_text, "pcap_geterr");
}

BOOL WINAPI stop_capture(DWORD event) {
  if ((event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) && g_capture) {
    g_api.break_loop(g_capture);
    return TRUE;
  }
  return FALSE;
}

void __cdecl write_packet(unsigned char* context, const PcapHeader* header, const unsigned char* bytes) {
  g_api.write_dump(context, header, bytes);
}

void usage() {
  std::puts("ITCCapture.exe --list");
  std::puts("ITCCapture.exe --interface <number> --output <file.pcap>");
}

int list_interfaces() {
  char error[ErrorBufferSize]{};
  PcapInterface* devices{};
  if (g_api.find_all_devices(&devices, error) == -1) { std::fprintf(stderr, "无法列出 Npcap 接口: %s\n", error); return 1; }
  int index = 1;
  for (PcapInterface* device = devices; device; device = device->next, ++index) std::printf("%d\t%s\t%s\n", index, device->name ? device->name : "", device->description ? device->description : "");
  g_api.free_all_devices(devices);
  return 0;
}

int capture(int requested, const char* output) {
  char error[ErrorBufferSize]{};
  PcapInterface* devices{};
  if (g_api.find_all_devices(&devices, error) == -1) { std::fprintf(stderr, "无法列出 Npcap 接口: %s\n", error); return 1; }
  PcapInterface* device = devices;
  for (int index = 1; device && index < requested; ++index) device = device->next;
  if (!device) { g_api.free_all_devices(devices); std::fprintf(stderr, "接口编号无效: %d\n", requested); return 1; }
  g_capture = g_api.open_live(device->name, 65535, 1, 1000, error);
  g_api.free_all_devices(devices);
  if (!g_capture) { std::fprintf(stderr, "无法打开接口: %s\n", error); return 1; }
  const char* filter = "tcp port 8000 or tcp port 15001 or (udp and dst net 225.101.1.0/24)";
  BpfProgram program{};
  if (g_api.compile_filter(g_capture, &program, filter, 1, 0) == -1 || g_api.set_filter(g_capture, &program) == -1) { std::fprintf(stderr, "无法设置过滤器: %s\n", g_api.error_text(g_capture)); g_api.free_filter(&program); g_api.close(g_capture); g_capture = nullptr; return 1; }
  g_api.free_filter(&program);
  DumpHandle* dump = g_api.open_dump(g_capture, output);
  if (!dump) { std::fprintf(stderr, "无法创建抓包文件: %s\n", g_api.error_text(g_capture)); g_api.close(g_capture); g_capture = nullptr; return 1; }
  SetConsoleCtrlHandler(stop_capture, TRUE);
  std::printf("只读抓包已启动，按 Ctrl+C 停止。\n过滤器: %s\n", filter);
  const int result = g_api.loop(g_capture, -1, write_packet, reinterpret_cast<unsigned char*>(dump));
  if (result == -1) std::fprintf(stderr, "抓包失败: %s\n", g_api.error_text(g_capture));
  g_api.close_dump(dump);
  g_api.close(g_capture); g_capture = nullptr;
  return result == -1 ? 1 : 0;
}
}

int main(int argc, char** argv) {
  if (!initialize()) { std::fputs("未检测到 Npcap。请安装 Npcap 后重试。\n", stderr); return 1; }
  if (argc == 2 && std::strcmp(argv[1], "--list") == 0) return list_interfaces();
  if (argc == 5 && std::strcmp(argv[1], "--interface") == 0 && std::strcmp(argv[3], "--output") == 0) return capture(std::atoi(argv[2]), argv[4]);
  usage();
  return 1;
}
