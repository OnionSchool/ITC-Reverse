#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {
constexpr int IdTerminalList = 100;
constexpr int IdAdd = 101;
constexpr int IdDelete = 102;
constexpr int IdImport = 103;
constexpr int IdService = 104;
constexpr int IdSave = 105;

struct Terminal { int id{}; std::wstring name; std::wstring address; int volume{80}; std::wstring forward; };
struct Group { int id{}; std::wstring name; std::vector<int> members; };
struct Session {
  int id{};
  std::string name;
  int priority{};
  int user_priority{};
  int type{};
  int status{};
  int play_time{};
  int total_time{};
  int program_id{};
  int play_volume{};
  std::string source;
  std::vector<int> terms;
};
struct State {
  std::vector<Terminal> terminals;
  std::vector<Group> groups;
  std::map<int, Session> sessions;
  int next_session_id{1};
  std::atomic<bool> serving{false};
  std::atomic<bool> stop{false};
  SOCKET control{INVALID_SOCKET};
  SOCKET data{INVALID_SOCKET};
  std::thread worker;
  std::vector<std::thread> clients;
  std::vector<SOCKET> client_sockets;
  std::mutex mutex;
};

State g_state;
HWND g_window;
HWND g_list;
std::wstring g_path;

std::wstring executable_path() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring value(path);
  return value.substr(0, value.find_last_of(L"\\/") + 1);
}

std::wstring trim(std::wstring value) {
  const auto first = value.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return L"";
  return value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}

std::vector<std::wstring> split(const std::wstring& value, wchar_t delimiter) {
  std::vector<std::wstring> result;
  std::wstringstream stream(value);
  std::wstring part;
  while (std::getline(stream, part, delimiter)) result.push_back(part);
  return result;
}

void refresh_list() {
  ListView_DeleteAllItems(g_list);
  for (size_t index = 0; index < g_state.terminals.size(); ++index) {
    const auto& terminal = g_state.terminals[index];
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = static_cast<int>(index);
    const std::wstring id = std::to_wstring(terminal.id);
    item.pszText = const_cast<wchar_t*>(id.c_str());
    item.lParam = static_cast<LPARAM>(index);
    ListView_InsertItem(g_list, &item);
    const std::wstring volume = std::to_wstring(terminal.volume);
    ListView_SetItemText(g_list, static_cast<int>(index), 1, const_cast<wchar_t*>(terminal.name.c_str()));
    ListView_SetItemText(g_list, static_cast<int>(index), 2, const_cast<wchar_t*>(terminal.address.c_str()));
    ListView_SetItemText(g_list, static_cast<int>(index), 3, const_cast<wchar_t*>(volume.c_str()));
    ListView_SetItemText(g_list, static_cast<int>(index), 4, const_cast<wchar_t*>(terminal.forward.c_str()));
  }
}

void save_configuration() {
  std::wofstream out(g_path.c_str(), std::ios::trunc);
  out.imbue(std::locale(""));
  for (const auto& terminal : g_state.terminals)
    out << L"T\t" << terminal.id << L"\t" << terminal.name << L"\t" << terminal.address << L"\t" << terminal.volume << L"\t" << terminal.forward << L"\n";
  for (const auto& group : g_state.groups) {
    out << L"G\t" << group.id << L"\t" << group.name << L"\t";
    for (size_t i = 0; i < group.members.size(); ++i) out << (i ? L"," : L"") << group.members[i];
    out << L"\n";
  }
}

void load_configuration() {
  std::wifstream in(g_path.c_str());
  in.imbue(std::locale(""));
  std::wstring line;
  while (std::getline(in, line)) {
    const auto fields = split(line, L'\t');
    try {
      if (fields.size() >= 6 && fields[0] == L"T") g_state.terminals.push_back({std::stoi(fields[1]), fields[2], fields[3], std::stoi(fields[4]), fields[5]});
      if (fields.size() >= 4 && fields[0] == L"G") {
        Group group{std::stoi(fields[1]), fields[2], {}};
        for (const auto& member : split(fields[3], L',')) if (!member.empty()) group.members.push_back(std::stoi(member));
        g_state.groups.push_back(group);
      }
    } catch (...) {}
  }
}

bool send_line(SOCKET socket, const std::string& text) {
  size_t sent = 0;
  while (sent < text.size()) {
    const int count = send(socket, text.data() + sent, static_cast<int>(text.size() - sent), 0);
    if (count <= 0) return false;
    sent += static_cast<size_t>(count);
  }
  return true;
}

std::vector<std::string> split_command(const std::string& line) {
  std::vector<std::string> fields;
  std::istringstream input(line);
  std::string field;
  while (input >> field) fields.push_back(field);
  return fields;
}

bool parse_number(const std::string& value, int& number) {
  try { size_t used{}; number = std::stoi(value, &used); return used == value.size(); } catch (...) { return false; }
}

bool session_and_value(const std::vector<std::string>& fields, size_t index, int& session_id, std::string& value) {
  if (fields.size() == index + 1) {
    const size_t comma = fields[index].find(',');
    if (comma == std::string::npos) return false;
    value = fields[index].substr(comma + 1);
    return parse_number(fields[index].substr(0, comma), session_id);
  }
  if (fields.size() == index + 2) {
    value = fields[index + 1];
    if (!value.empty() && value[0] == ',') value.erase(0, 1);
    return parse_number(fields[index], session_id);
  }
  return false;
}

Session* session_from(const std::vector<std::string>& fields, size_t index, SOCKET socket) {
  int id{};
  if (fields.size() <= index) { send_line(socket, "599 invalid argument\r\n"); return nullptr; }
  const std::string identifier = fields[index].substr(0, fields[index].find(','));
  if (!parse_number(identifier, id)) { send_line(socket, "599 invalid argument\r\n"); return nullptr; }
  const auto found = g_state.sessions.find(id);
  if (found == g_state.sessions.end()) { send_line(socket, "500 invalid session\r\n"); return nullptr; }
  return &found->second;
}

bool set_session_value(Session& session, const std::string& assignment) {
  const size_t separator = assignment.find('=');
  if (separator == std::string::npos) return false;
  std::string key = assignment.substr(0, separator);
  std::transform(key.begin(), key.end(), key.begin(), ::toupper);
  const std::string value = assignment.substr(separator + 1);
  int number{};
  if (key == "NAME") { session.name = value; return true; }
  int* target = nullptr;
  if (key == "STAT") target = &session.status;
  if (key == "PLAY_TIME") target = &session.play_time;
  if (key == "TOTAL_TIME") target = &session.total_time;
  if (key == "PROGRAM_ID") target = &session.program_id;
  if (key == "PLAYVOL") target = &session.play_volume;
  if (key == "TYPE") target = &session.type;
  if (!target || !parse_number(value, number)) return false;
  *target = number;
  return true;
}

void handle_session(const std::vector<std::string>& fields, SOCKET socket) {
  if (fields.size() < 2) { send_line(socket, "599 unknown sub command.\r\n"); return; }
  std::string action = fields[1]; std::transform(action.begin(), action.end(), action.begin(), ::tolower);
  std::lock_guard<std::mutex> lock(g_state.mutex);
  if (action == "new") {
    int priority{}, user_priority{}, type{};
    if (fields.size() != 6 || !parse_number(fields[3], priority) || !parse_number(fields[4], user_priority) || !parse_number(fields[5], type) || priority < 1 || priority > 1000) { send_line(socket, "599 invalid argument\r\n"); return; }
    const int id = g_state.next_session_id++;
    g_state.sessions[id] = {id, fields[2], priority, user_priority, type};
    send_line(socket, "000 " + std::to_string(id) + "\r\n"); return;
  }
  if (action == "list") {
    for (const auto& entry : g_state.sessions) send_line(socket, "000 " + std::to_string(entry.second.id) + "\t" + entry.second.name + "\r\n");
    send_line(socket, "000 end\r\n"); return;
  }
  Session* session = session_from(fields, 2, socket); if (!session) return;
  if (action == "rm") { g_state.sessions.erase(session->id); send_line(socket, "000 ok\r\n"); return; }
  if (action == "add_term" || action == "rm_term") {
    int terminal{};
    int requested_session{}; std::string terminal_text;
    if (!session_and_value(fields, 2, requested_session, terminal_text) || requested_session != session->id || !parse_number(terminal_text, terminal)) { send_line(socket, "599 invalid argument\r\n"); return; }
    auto found = std::find(session->terms.begin(), session->terms.end(), terminal);
    if (action == "add_term" && found == session->terms.end()) session->terms.push_back(terminal);
    if (action == "rm_term" && found != session->terms.end()) session->terms.erase(found);
    send_line(socket, "000 ok\r\n"); return;
  }
  if (action == "terms") {
    for (const int terminal : session->terms) send_line(socket, "000 " + std::to_string(terminal) + "\r\n");
    send_line(socket, "000 end\r\n"); return;
  }
  if (action == "source") {
    int requested_session{}; std::string source;
    if (!session_and_value(fields, 2, requested_session, source) || requested_session != session->id) { send_line(socket, "599 invalid argument\r\n"); return; }
    session->source = source; send_line(socket, "000 ok\r\n"); return;
  }
  if (action == "playvol" && fields.size() == 4) {
    int volume{};
    if (!parse_number(fields[3], volume)) { send_line(socket, "500 invalid parameter\r\n"); return; }
    session->play_volume = volume; send_line(socket, "000 ok\r\n"); return;
  }
  if (action == "set") {
    if (fields.size() < 4 || !std::all_of(fields.begin() + 3, fields.end(), [&](const std::string& item) { return set_session_value(*session, item); })) { send_line(socket, "500 invalid parameter\r\n"); return; }
    send_line(socket, "000 ok\r\n"); return;
  }
  if (action == "get" && fields.size() == 4) {
    std::string key = fields[3]; std::transform(key.begin(), key.end(), key.begin(), ::toupper);
    if (key == "NAME") { send_line(socket, "000 NAME=" + session->name + "\r\n"); return; }
    const std::map<std::string, int> values{{"STAT", session->status}, {"PLAY_TIME", session->play_time}, {"TOTAL_TIME", session->total_time}, {"PROGRAM_ID", session->program_id}, {"TYPE", session->type}, {"PLAYVOL", session->play_volume}};
    const auto found = values.find(key);
    if (found != values.end()) { send_line(socket, "000 " + key + "=" + std::to_string(found->second) + "\r\n"); return; }
    send_line(socket, "500 invalid parameter\r\n"); return;
  }
  send_line(socket, "599 unknown sub command.\r\n");
}

void handle_client(SOCKET socket) {
  bool authenticated = false;
  std::string buffered;
  char input[512];
  while (!g_state.stop) {
    const int count = recv(socket, input, sizeof(input), 0);
    if (count <= 0) break;
    buffered.append(input, count);
    if (buffered.size() > 4096) { send_line(socket, "599 line too long\r\n"); break; }
    size_t end;
    while ((end = buffered.find('\n')) != std::string::npos) {
      std::string line = buffered.substr(0, end);
      buffered.erase(0, end + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.size() > 4096) { send_line(socket, "599 line too long\r\n"); closesocket(socket); return; }
      const auto fields = split_command(line);
      if (fields.empty()) { send_line(socket, "599 unknown command.\r\n"); continue; }
      std::string verb = fields[0];
      std::transform(verb.begin(), verb.end(), verb.begin(), ::tolower);
      if (verb == "quit") { send_line(socket, "000 bye\r\n"); return; }
      if (verb == "logon") {
        if (fields.size() != 4) send_line(socket, "599 invalid argument\r\n");
        else if (fields[2] != "admin") send_line(socket, "511 invalid user\r\n");
        else if (fields[3] != "admin") send_line(socket, "512 invalid password\r\n");
        else { authenticated = true; send_line(socket, "000 ok\r\n"); }
      } else if (!authenticated) send_line(socket, "501 not logon\r\n");
      else if (verb == "session") handle_session(fields, socket);
      else send_line(socket, "599 unknown command.\r\n");
    }
  }
  closesocket(socket);
  std::lock_guard<std::mutex> lock(g_state.mutex);
  const auto found = std::find(g_state.client_sockets.begin(), g_state.client_sockets.end(), socket);
  if (found != g_state.client_sockets.end()) g_state.client_sockets.erase(found);
}

SOCKET listen_port(u_short port) {
  SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == INVALID_SOCKET) return socket;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR || listen(socket, SOMAXCONN) == SOCKET_ERROR) { closesocket(socket); return INVALID_SOCKET; }
  return socket;
}

void service_loop() {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { PostMessage(g_window, WM_APP + 1, 0, 0); return; }
  g_state.control = listen_port(8000);
  g_state.data = listen_port(15001);
  if (g_state.control == INVALID_SOCKET || g_state.data == INVALID_SOCKET) {
    if (g_state.control != INVALID_SOCKET) closesocket(g_state.control);
    if (g_state.data != INVALID_SOCKET) closesocket(g_state.data);
    g_state.control = g_state.data = INVALID_SOCKET;
    PostMessage(g_window, WM_APP + 1, 0, 0); WSACleanup(); return;
  }
  g_state.serving = true;
  PostMessage(g_window, WM_APP + 1, 1, 0);
  while (!g_state.stop) {
    fd_set sockets; FD_ZERO(&sockets); FD_SET(g_state.control, &sockets); FD_SET(g_state.data, &sockets);
    timeval timeout{0, 200000};
    if (select(0, &sockets, nullptr, nullptr, &timeout) <= 0) continue;
    for (SOCKET listener : {g_state.control, g_state.data}) if (FD_ISSET(listener, &sockets)) {
      SOCKET client = accept(listener, nullptr, nullptr);
      if (client == INVALID_SOCKET) continue;
       if (listener == g_state.data) { send_line(client, "505 data channel is not implemented\r\n"); closesocket(client); }
       else {
         std::lock_guard<std::mutex> lock(g_state.mutex);
         g_state.client_sockets.push_back(client);
         g_state.clients.emplace_back(handle_client, client);
       }
    }
  }
  closesocket(g_state.control); closesocket(g_state.data); g_state.control = g_state.data = INVALID_SOCKET; g_state.serving = false;
}

void stop_service() {
  g_state.stop = true;
  if (g_state.control != INVALID_SOCKET) shutdown(g_state.control, SD_BOTH);
  if (g_state.data != INVALID_SOCKET) shutdown(g_state.data, SD_BOTH);
  if (g_state.worker.joinable()) g_state.worker.join();
  {
    std::lock_guard<std::mutex> lock(g_state.mutex);
    for (const SOCKET socket : g_state.client_sockets) shutdown(socket, SD_BOTH);
  }
  for (auto& client : g_state.clients) if (client.joinable()) client.join();
  g_state.clients.clear();
  WSACleanup();
}

std::wstring open_file(const wchar_t* filter) {
  wchar_t file[MAX_PATH]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = g_window; dialog.lpstrFilter = filter; dialog.lpstrFile = file; dialog.nMaxFile = MAX_PATH; dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  return GetOpenFileNameW(&dialog) ? file : L"";
}

VARIANT text_variant(const std::wstring& text) {
  VARIANT value; VariantInit(&value); value.vt = VT_BSTR; value.bstrVal = SysAllocString(text.c_str()); return value;
}

VARIANT integer_variant(int number) {
  VARIANT value; VariantInit(&value); value.vt = VT_I4; value.lVal = number; return value;
}

void clear_arguments(std::vector<VARIANT>& arguments) {
  for (auto& argument : arguments) VariantClear(&argument);
}

HRESULT invoke(IDispatch* object, const wchar_t* name, WORD flags, std::vector<VARIANT> arguments, VARIANT* result = nullptr) {
  DISPID id{};
  LPOLESTR names[] = {const_cast<LPOLESTR>(name)};
  HRESULT status = object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &id);
  if (FAILED(status)) { clear_arguments(arguments); return status; }
  std::reverse(arguments.begin(), arguments.end());
  DISPPARAMS parameters{};
  parameters.cArgs = static_cast<UINT>(arguments.size());
  parameters.rgvarg = arguments.empty() ? nullptr : arguments.data();
  EXCEPINFO exception{};
  UINT error{};
  status = object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &parameters, result, &exception, &error);
  clear_arguments(arguments);
  return status;
}

IDispatch* dispatch_from_variant(VARIANT& value) {
  IDispatch* object = value.vt == VT_DISPATCH ? value.pdispVal : nullptr;
  if (object) object->AddRef();
  VariantClear(&value);
  return object;
}

std::wstring value_as_text(VARIANT& value) {
  if (value.vt == VT_NULL || value.vt == VT_EMPTY) { VariantClear(&value); return L""; }
  VARIANT converted; VariantInit(&converted);
  if (FAILED(VariantChangeType(&converted, &value, 0, VT_BSTR))) { VariantClear(&value); return L""; }
  std::wstring text = converted.bstrVal ? converted.bstrVal : L"";
  VariantClear(&converted); VariantClear(&value); return text;
}

IDispatch* create_ado(const wchar_t* program_id) {
  CLSID clsid{};
  if (FAILED(CLSIDFromProgID(program_id, &clsid))) return nullptr;
  IDispatch* object = nullptr;
  return SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, reinterpret_cast<void**>(&object))) ? object : nullptr;
}

std::wstring field_text(IDispatch* recordset, const wchar_t* field_name) {
  VARIANT fields; VariantInit(&fields);
  if (FAILED(invoke(recordset, L"Fields", DISPATCH_PROPERTYGET, {}, &fields))) return L"";
  IDispatch* collection = dispatch_from_variant(fields);
  if (!collection) return L"";
  VARIANT field; VariantInit(&field);
  const HRESULT status = invoke(collection, L"Item", DISPATCH_METHOD | DISPATCH_PROPERTYGET, {text_variant(field_name)}, &field);
  collection->Release();
  if (FAILED(status)) return L"";
  IDispatch* item = dispatch_from_variant(field);
  if (!item) return L"";
  VARIANT value; VariantInit(&value);
  const HRESULT value_status = invoke(item, L"Value", DISPATCH_PROPERTYGET, {}, &value);
  item->Release();
  return SUCCEEDED(value_status) ? value_as_text(value) : L"";
}

bool recordset_eof(IDispatch* recordset) {
  VARIANT value; VariantInit(&value);
  if (FAILED(invoke(recordset, L"EOF", DISPATCH_PROPERTYGET, {}, &value))) return true;
  const bool eof = value.boolVal == VARIANT_TRUE;
  VariantClear(&value); return eof;
}

std::vector<std::map<std::wstring, std::wstring>> query_mdb(IDispatch* connection, const std::wstring& query, const std::vector<std::wstring>& columns) {
  std::vector<std::map<std::wstring, std::wstring>> rows;
  IDispatch* recordset = create_ado(L"ADODB.Recordset");
  if (!recordset) return rows;
  VARIANT connection_value; VariantInit(&connection_value); connection_value.vt = VT_DISPATCH; connection_value.pdispVal = connection; connection->AddRef();
  const HRESULT opened = invoke(recordset, L"Open", DISPATCH_METHOD, {text_variant(query), connection_value, integer_variant(0), integer_variant(1), integer_variant(1)});
  if (SUCCEEDED(opened)) {
    while (!recordset_eof(recordset)) {
      std::map<std::wstring, std::wstring> row;
      for (const auto& column : columns) row[column] = field_text(recordset, column.c_str());
      rows.push_back(row);
      invoke(recordset, L"MoveNext", DISPATCH_METHOD, {});
    }
    invoke(recordset, L"Close", DISPATCH_METHOD, {});
  }
  recordset->Release();
  return rows;
}

void import_database() {
  const std::wstring database = open_file(L"Access 数据库 (*.mdb)\0*.mdb\0\0");
  if (database.empty()) return;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { MessageBoxW(g_window, L"无法初始化 Access 导入组件。", L"导入失败", MB_ICONERROR); return; }
  IDispatch* connection = create_ado(L"ADODB.Connection");
  if (!connection) { CoUninitialize(); MessageBoxW(g_window, L"系统缺少 Windows Jet 4.0 数据库组件。", L"导入失败", MB_ICONERROR); return; }
  const std::wstring source = L"Provider=Microsoft.Jet.OLEDB.4.0;Data Source=" + database + L";Mode=Read;";
  if (FAILED(invoke(connection, L"Open", DISPATCH_METHOD, {text_variant(source), text_variant(L""), text_variant(L""), integer_variant(0)}))) {
    connection->Release(); CoUninitialize(); MessageBoxW(g_window, L"无法以只读方式打开该 MDB。", L"导入失败", MB_ICONERROR); return;
  }
  const auto terminals = query_mdb(connection, L"SELECT ID, Name, Address, PlayVol, FwdAddr FROM DB_Term", {L"ID", L"Name", L"Address", L"PlayVol", L"FwdAddr"});
  const auto groups = query_mdb(connection, L"SELECT GroupId, Name FROM DB_Group", {L"GroupId", L"Name"});
  const auto members = query_mdb(connection, L"SELECT GroupId, TermId FROM DB_GroupMember", {L"GroupId", L"TermId"});
  invoke(connection, L"Close", DISPATCH_METHOD, {}); connection->Release(); CoUninitialize();
  int imported = 0;
  for (const auto& row : terminals) try { g_state.terminals.push_back({std::stoi(row.at(L"ID")), row.at(L"Name"), row.at(L"Address"), row.at(L"PlayVol").empty() ? 80 : std::stoi(row.at(L"PlayVol")), row.at(L"FwdAddr")}); ++imported; } catch (...) {}
  std::map<int, Group> imported_groups;
  for (const auto& row : groups) try { const int id = std::stoi(row.at(L"GroupId")); imported_groups[id] = {id, row.at(L"Name"), {}}; } catch (...) {}
  for (const auto& row : members) try { imported_groups[std::stoi(row.at(L"GroupId"))].members.push_back(std::stoi(row.at(L"TermId"))); } catch (...) {}
  for (auto& group : imported_groups) g_state.groups.push_back(group.second);
  refresh_list(); save_configuration();
  MessageBoxW(g_window, (L"已导入 " + std::to_wstring(imported) + L" 个终端和 " + std::to_wstring(imported_groups.size()) + L" 个分组。源数据库未被修改。").c_str(), L"导入完成", MB_ICONINFORMATION);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE: {
      g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 18, 78, 744, 340, window, reinterpret_cast<HMENU>(IdTerminalList), nullptr, nullptr);
      ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
      const wchar_t* columns[] = {L"编号", L"终端名称", L"地址", L"音量", L"中继地址"}; const int widths[] = {80, 180, 170, 70, 220};
      for (int i = 0; i < 5; ++i) { LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(columns[i]); column.cx = widths[i]; ListView_InsertColumn(g_list, i, &column); }
      CreateWindowW(L"BUTTON", L"添加终端", WS_CHILD | WS_VISIBLE, 18, 28, 108, 32, window, reinterpret_cast<HMENU>(IdAdd), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"删除所选", WS_CHILD | WS_VISIBLE, 136, 28, 108, 32, window, reinterpret_cast<HMENU>(IdDelete), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"导入旧 Access", WS_CHILD | WS_VISIBLE, 254, 28, 120, 32, window, reinterpret_cast<HMENU>(IdImport), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"保存配置", WS_CHILD | WS_VISIBLE, 384, 28, 100, 32, window, reinterpret_cast<HMENU>(IdSave), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"启动控制服务", WS_CHILD | WS_VISIBLE, 494, 28, 130, 32, window, reinterpret_cast<HMENU>(IdService), nullptr, nullptr);
      CreateWindowW(L"STATIC", L"本地控制面：127.0.0.1:8000；数据端口：15001（保留）", WS_CHILD | WS_VISIBLE, 18, 432, 700, 24, window, nullptr, nullptr, nullptr);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wparam) == IdAdd) { int id = 1; for (const auto& terminal : g_state.terminals) id = std::max(id, terminal.id + 1); g_state.terminals.push_back({id, L"新终端", L"0.0.0.0", 80, L""}); refresh_list(); }
      if (LOWORD(wparam) == IdDelete) { const int selected = ListView_GetNextItem(g_list, -1, LVNI_SELECTED); if (selected >= 0) { g_state.terminals.erase(g_state.terminals.begin() + selected); refresh_list(); } }
      if (LOWORD(wparam) == IdImport) import_database();
      if (LOWORD(wparam) == IdSave) { save_configuration(); MessageBoxW(window, L"配置已保存。", L"ITC 兼容控制台", MB_OK); }
      if (LOWORD(wparam) == IdService) { auto button = reinterpret_cast<HWND>(lparam); if (!g_state.serving) { if (g_state.worker.joinable()) g_state.worker.join(); g_state.stop = false; g_state.worker = std::thread(service_loop); SetWindowTextW(button, L"停止控制服务"); } else { stop_service(); SetWindowTextW(button, L"启动控制服务"); } }
      return 0;
    case WM_APP + 1:
      if (!wparam) {
        if (g_state.worker.joinable()) g_state.worker.join();
        SetWindowTextW(GetDlgItem(window, IdService), L"启动控制服务");
        MessageBoxW(window, L"无法绑定 8000 或 15001 端口。", L"控制服务", MB_ICONERROR);
      }
      return 0;
    case WM_CLOSE: save_configuration(); stop_service(); DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
  g_path = executable_path() + L"itc-configuration.tsv"; load_configuration();
  WNDCLASSW klass{}; klass.hInstance = instance; klass.hCursor = LoadCursor(nullptr, IDC_ARROW); klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); klass.lpszClassName = L"ITCCompat"; klass.lpfnWndProc = window_proc;
  RegisterClassW(&klass);
  g_window = CreateWindowW(klass.lpszClassName, L"ITC 兼容控制台", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 800, 520, nullptr, nullptr, instance, nullptr);
  ShowWindow(g_window, show); refresh_list();
  MSG message{}; while (GetMessageW(&message, nullptr, 0, 0)) { TranslateMessage(&message); DispatchMessageW(&message); }
  return 0;
}
