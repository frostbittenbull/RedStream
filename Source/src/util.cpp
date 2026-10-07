#include "common.h"
#include <cstdarg>

float g_k = 1.5f;
HWND  g_mainWnd = nullptr;

// ═════════════════════════════ строки ═════════════════════════════════════════
std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string U8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string o(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n, nullptr, nullptr);
    return o;
}
std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) a++;
    while (b > a && iswspace(s[b - 1])) b--;
    return s.substr(a, b - a);
}
std::string TrimA(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
bool StartsWith(const std::wstring& s, const std::wstring& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool EndsWith(const std::wstring& s, const std::wstring& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

std::wstring Fmt(const wchar_t* f, ...) {
    va_list ap;
    va_start(ap, f);
    std::vector<wchar_t> buf(512);
    for (;;) {
        va_list cp;
        va_copy(cp, ap);
        int n = _vsnwprintf(buf.data(), buf.size(), f, cp);
        va_end(cp);
        if (n >= 0 && (size_t)n < buf.size()) break;
        buf.resize(buf.size() * 2);
    }
    va_end(ap);
    return buf.data();
}

// ═════════════════════════════ файлы ══════════════════════════════════════════
std::wstring ExePath() {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    return std::wstring(b, n);
}
std::wstring ExeDir() {
    std::wstring p = ExePath();
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : p.substr(0, i);
}
std::wstring ResPath(const std::wstring& name) {
    return name.empty() ? ExeDir() : ExeDir() + L"\\" + name;
}
bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
std::string ReadFileA(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return "";
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}
bool WriteFileA(const std::wstring& path, const std::string& data, bool append) {
    FILE* f = _wfopen(path.c_str(), append ? L"ab" : L"wb");
    if (!f) return false;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

// ═════════════════════════════ настройки ══════════════════════════════════════
std::wstring GetDownloadsFolder() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", 0,
                      KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH * 2];
        DWORD sz = sizeof buf, type = 0;
        LONG r = RegQueryValueExW(k, L"{374DE290-123F-4565-9164-39C4925E467B}", nullptr, &type, (BYTE*)buf, &sz);
        RegCloseKey(k);
        if (r == ERROR_SUCCESS) {
            wchar_t ex[MAX_PATH * 2];
            ExpandEnvironmentStringsW(buf, ex, MAX_PATH * 2);
            if (DirExists(ex)) return ex;
        }
    }
    wchar_t* p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p)) && p) {
        std::wstring s = p;
        CoTaskMemFree(p);
        if (DirExists(s)) return s;
    }
    wchar_t up[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
    return (n ? std::wstring(up, n) : std::wstring(L"C:")) + L"\\Downloads";
}

static std::wstring SettingsPath() { return ResPath(L"settings.txt"); }

void SaveDestFolder(const std::wstring& f) { WriteFileA(SettingsPath(), U8(f)); }

std::wstring LoadDestFolder() {
    if (FileExists(SettingsPath())) {
        std::string s = ReadFileA(SettingsPath());
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
            s.erase(0, 3);
        std::wstring folder = Trim(W(s));
        if (!folder.empty()) return folder;
    }
    std::wstring folder = GetDownloadsFolder() + L"\\RedStream Downloader";
    SaveDestFolder(folder);
    return folder;
}

static std::wstring ProxyPath() { return ResPath(L"proxy.txt"); }

static std::string JStr(const json& j, const char* k, const char* def = "") {
    if (!j.is_object() || !j.contains(k)) return def;
    const json& v = j[k];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_null()) return def;
    return v.dump();
}

ProxyCfg LoadProxy() {
    ProxyCfg p;
    try {
        json j = json::parse(ReadFileA(ProxyPath()));
        if (j.is_object()) {
            p.enabled  = j.value("enabled", false);
            p.type     = JStr(j, "type", "http");
            p.host     = JStr(j, "host");
            p.port     = JStr(j, "port");
            p.user     = JStr(j, "user");
            p.password = JStr(j, "password");
        }
    } catch (...) {}
    return p;
}

void SaveProxy(const ProxyCfg& p) {
    json j;
    j["enabled"]  = p.enabled;
    j["type"]     = p.type;
    j["host"]     = p.host;
    j["port"]     = p.port;
    j["user"]     = p.user;
    j["password"] = p.password;
    WriteFileA(ProxyPath(), j.dump(-1, ' ', false, json::error_handler_t::replace));
}

std::wstring GetProxyUrl() {
    ProxyCfg p = LoadProxy();
    if (!p.enabled || p.host.empty()) return L"";
    std::string addr = p.port.empty() ? p.host : p.host + ":" + p.port;
    if (!p.user.empty() && !p.password.empty()) return W(p.type + "://" + p.user + ":" + p.password + "@" + addr);
    return W(p.type + "://" + addr);
}

std::wstring GetLogPath()     { return ResPath(L"log.txt"); }
std::wstring GetHistoryPath() { return ResPath(L"history.txt"); }

std::vector<std::wstring> LoadHistory() {
    std::vector<std::wstring> out;
    std::string s = ReadFileA(GetHistoryPath());
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('\n', i);
        if (j == std::string::npos) j = s.size();
        std::wstring line = Trim(W(s.substr(i, j - i)));
        if (!line.empty()) out.push_back(line);
        i = j + 1;
    }
    return out;
}

void SaveHistory(const std::wstring& url) {
    auto h = LoadHistory();
    h.erase(std::remove(h.begin(), h.end(), url), h.end());
    h.insert(h.begin(), url);
    std::string data;
    for (size_t i = 0; i < h.size(); i++) {
        if (i) data += "\n";
        data += U8(h[i]);
    }
    WriteFileA(GetHistoryPath(), data);
}

std::vector<std::wstring> LoadHistoryCombo() {
    auto h = LoadHistory();
    if (h.size() > 5) h.resize(5);
    return h;
}

// ═════════════════════════════ процессы ═══════════════════════════════════════
static std::wstring QuoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (size_t i = 0;; i++) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') { bs++; i++; }
        if (i == a.size()) { r.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"') { r.append(bs * 2 + 1, L'\\'); r.push_back(L'"'); }
        else { r.append(bs, L'\\'); r.push_back(a[i]); }
    }
    r.push_back(L'"');
    return r;
}

std::wstring BuildCmdLine(const std::vector<std::wstring>& args) {
    std::wstring s;
    for (size_t i = 0; i < args.size(); i++) {
        if (i) s += L' ';
        s += QuoteArg(args[i]);
    }
    return s;
}

static std::mutex g_spawnMu;

bool Proc::Start(const std::vector<std::wstring>& args, bool capture, const std::wstring& cwd, bool separateErr) {
    std::lock_guard<std::mutex> lk(g_spawnMu);
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rdE = nullptr, wrE = nullptr;
    HANDLE rd = nullptr, wr = nullptr, nulIn = INVALID_HANDLE_VALUE, nulOut = INVALID_HANDLE_VALUE;
    if (capture) {
        if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        if (separateErr) {
            if (!CreatePipe(&rdE, &wrE, &sa, 0)) { CloseHandle(rd); CloseHandle(wr); return false; }
            SetHandleInformation(rdE, HANDLE_FLAG_INHERIT, 0);
        }
    } else {
        nulOut = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    }
    nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nulIn;
    si.hStdOutput = capture ? wr : nulOut;
    si.hStdError  = capture ? (wrE ? wrE : wr) : nulOut;

    std::wstring cmd = BuildCmdLine(args);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (wr) CloseHandle(wr);
    if (wrE) CloseHandle(wrE);
    if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
    if (nulOut != INVALID_HANDLE_VALUE) CloseHandle(nulOut);
    if (!ok) {
        if (rd) CloseHandle(rd);
        if (rdE) CloseHandle(rdE);
        return false;
    }
    hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &info, sizeof info);
        if (!AssignProcessToJobObject(hJob, pi.hProcess)) { CloseHandle(hJob); hJob = nullptr; }
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    hProc = pi.hProcess;
    pid = pi.dwProcessId;
    hRead = rd;
    hReadErr = rdE;
    return true;
}

DWORD Proc::Read(char* buf, DWORD n) {
    if (!hRead) return 0;
    DWORD got = 0;
    if (!ReadFile(hRead, buf, n, &got, nullptr)) return 0;
    return got;
}

DWORD Proc::ReadErr(char* buf, DWORD n) {
    if (!hReadErr) return 0;
    DWORD got = 0;
    if (!ReadFile(hReadErr, buf, n, &got, nullptr)) return 0;
    return got;
}

int Proc::Wait(DWORD ms) {
    if (!hProc) return -1;
    if (WaitForSingleObject(hProc, ms) != WAIT_OBJECT_0) return -1;
    DWORD code = 0;
    GetExitCodeProcess(hProc, &code);
    return (int)code;
}

void Proc::Kill() {
    if (hJob) TerminateJobObject(hJob, 1);
    else if (hProc) TerminateProcess(hProc, 1);
}

bool Proc::Running() const {
    return hProc && WaitForSingleObject(hProc, 0) == WAIT_TIMEOUT;
}

void Proc::Close() {
    if (hRead) { CloseHandle(hRead); hRead = nullptr; }
    if (hReadErr) { CloseHandle(hReadErr); hReadErr = nullptr; }
    if (hProc) { CloseHandle(hProc); hProc = nullptr; }
    if (hJob)  { CloseHandle(hJob);  hJob = nullptr; }
}

ProcResult RunCapture(const std::vector<std::wstring>& args, DWORD timeoutMs, const std::wstring& cwd,
                      std::string* errOut) {
    ProcResult r;
    Proc p;
    if (!p.Start(args, true, cwd, errOut != nullptr)) return r;
    r.started = true;
    std::string out, err;
    std::thread t([&]() {
        char buf[65536];
        DWORD n;
        while ((n = p.Read(buf, sizeof buf)) > 0) out.append(buf, n);
    });
    std::thread te;
    if (errOut) te = std::thread([&]() {
        char buf[65536];
        DWORD n;
        while ((n = p.ReadErr(buf, sizeof buf)) > 0) err.append(buf, n);
    });
    DWORD w = WaitForSingleObject(p.hProc, timeoutMs);
    if (w == WAIT_TIMEOUT) { r.timedOut = true; p.Kill(); }
    t.join();
    if (te.joinable()) te.join();
    DWORD code = 0;
    GetExitCodeProcess(p.hProc, &code);
    r.code = r.timedOut ? -1 : (int)code;
    r.out = std::move(out);
    if (errOut) *errOut = std::move(err);
    p.Close();
    return r;
}

// ═════════════════════════════ HTTP ═══════════════════════════════════════════
struct Inet {
    HINTERNET h = nullptr;
    ~Inet() { if (h) InternetCloseHandle(h); }
};

static HINTERNET OpenInet(DWORD timeoutMs) {
    HINTERNET h = InternetOpenW(L"RedStream", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!h) return nullptr;
    InternetSetOptionW(h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof timeoutMs);
    InternetSetOptionW(h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof timeoutMs);
    InternetSetOptionW(h, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof timeoutMs);
    return h;
}

static HINTERNET OpenUrl(HINTERNET h, const std::wstring& url) {
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI |
                  INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_COOKIES;
    HINTERNET u = InternetOpenUrlW(h, url.c_str(), L"Accept: */*\r\n", (DWORD)-1, flags, 0);
    if (!u) return nullptr;
    DWORD status = 0, sz = sizeof status;
    if (HttpQueryInfoW(u, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &sz, nullptr)) {
        if (status < 200 || status >= 300) { InternetCloseHandle(u); return nullptr; }
    }
    return u;
}

bool HttpGet(const std::wstring& url, std::string& out, DWORD timeoutMs) {
    Inet net;
    net.h = OpenInet(timeoutMs);
    if (!net.h) return false;
    HINTERNET u = OpenUrl(net.h, url);
    if (!u) return false;
    char buf[16384];
    DWORD n = 0;
    out.clear();
    while (InternetReadFile(u, buf, sizeof buf, &n) && n > 0) out.append(buf, n);
    InternetCloseHandle(u);
    return true;
}

bool HttpDownload(const std::wstring& url, const std::wstring& file,
                  const std::function<bool(uint64_t, uint64_t)>& progress, DWORD timeoutMs) {
    Inet net;
    net.h = OpenInet(timeoutMs);
    if (!net.h) return false;
    HINTERNET u = OpenUrl(net.h, url);
    if (!u) return false;
    DWORD len = 0, sz = sizeof len;
    uint64_t total = 0;
    if (HttpQueryInfoW(u, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &len, &sz, nullptr)) total = len;
    FILE* f = _wfopen(file.c_str(), L"wb");
    if (!f) { InternetCloseHandle(u); return false; }
    std::vector<char> buf(65536);
    uint64_t done = 0;
    bool ok = true;
    for (;;) {
        DWORD n = 0;
        if (!InternetReadFile(u, buf.data(), (DWORD)buf.size(), &n)) { ok = false; break; }
        if (n == 0) break;
        fwrite(buf.data(), 1, n, f);
        done += n;
        if (progress && !progress(done, total)) { ok = false; break; }
    }
    fclose(f);
    InternetCloseHandle(u);
    return ok;
}

// ═════════════════════════════ система ════════════════════════════════════════
void PlayKindSound(const char* kind) {
    bool success = std::string(kind) == "success";
    std::thread([success]() {
        std::wstring path = success ? L"C:\\Windows\\Media\\Windows Background.wav"
                                    : L"C:\\Windows\\Media\\Windows Foreground.wav";
        if (FileExists(path)) PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_NODEFAULT);
        else MessageBeep(success ? MB_ICONASTERISK : MB_ICONHAND);
    }).detach();
}

void ShellOpen(const std::wstring& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static const wchar_t* kRunKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunName = L"RedStream";

bool IsAutostart() {
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        HKEY k;
        if (RegOpenKeyExW(root, kRunKey, 0, KEY_READ, &k) == ERROR_SUCCESS) {
            LONG r = RegQueryValueExW(k, kRunName, nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(k);
            if (r == ERROR_SUCCESS) return true;
        }
    }
    return false;
}

void SetAutostart(bool enable) {
    if (enable) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
            HKEY k;
            if (RegOpenKeyExW(root, kRunKey, 0, KEY_WRITE, &k) == ERROR_SUCCESS) {
                LONG r = RegSetValueExW(k, kRunName, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2));
                RegCloseKey(k);
                if (r == ERROR_SUCCESS) return;
            }
        }
    } else {
        for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
            HKEY k;
            if (RegOpenKeyExW(root, kRunKey, 0, KEY_WRITE, &k) == ERROR_SUCCESS) {
                RegDeleteValueW(k, kRunName);
                RegCloseKey(k);
            }
        }
    }
}

void PostUI(std::function<void()> fn) {
    if (!g_mainWnd) return;
    auto* p = new std::function<void()>(std::move(fn));
    if (!PostMessageW(g_mainWnd, WM_APP_CALL, 0, (LPARAM)p)) delete p;
}
