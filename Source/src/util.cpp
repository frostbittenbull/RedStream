#include "common.h"
#include <cstdarg>
#include <atomic>
#include <regex>

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

// ═════════════════════════════ недокачанные файлы ═════════════════════════════
unsigned long long NowFileTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static bool IsPartialLeftover(const std::wstring& n) {
    auto ends = [&](const wchar_t* e) { size_t l = wcslen(e); return n.size() >= l && n.compare(n.size() - l, l, e) == 0; };
    if (ends(L".part") || ends(L".ytdl") || n.find(L".part-frag") != std::wstring::npos) return true;
    static const std::wregex rf(LR"(\.f[0-9]+(-[0-9]+)?\.[a-z0-9]{2,5}$)");            // «Название.f251.webm» — поток до слияния
    static const std::wregex rt(LR"(\.temp\.[a-z0-9]{2,5}$)");                         // временный файл слияния
    static const std::wregex rs(LR"(\.[a-z]{2,3}(-[a-z0-9]+)*\.(vtt|srt|ass|ttml|srv1|srv2|srv3|json3)$)");   // «Название.ru.vtt»
    return std::regex_search(n, rf) || std::regex_search(n, rt) || std::regex_search(n, rs);
}

void CleanupPartialFiles(const std::wstring& folder, unsigned long long since, bool wait) {
    if (folder.empty() || since == 0) return;
    auto work = [folder, since]() {
        for (int attempt = 0; attempt < 12; attempt++) {         // процесс только что завершён — файлы могут ещё быть заняты
            bool left = false;
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    unsigned long long lw = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
                    if (lw + 20000000ULL < since) continue;                  // старше начала загрузки (минус 2 с) — не наш файл
                    std::wstring name = fd.cFileName;
                    CharLowerBuffW(&name[0], (DWORD)name.size());
                    if (!IsPartialLeftover(name)) continue;
                    if (!DeleteFileW((folder + L"\\" + fd.cFileName).c_str())) left = true;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (!left) break;
            Sleep(250);
        }
    };
    if (wait) work(); else std::thread(work).detach();
}

// Формат строки: «ссылка» либо «ссылка<TAB>название<TAB>дата» (старые файлы читаются как есть)
static std::vector<std::wstring> SplitTabs(const std::wstring& s) {
    std::vector<std::wstring> r;
    size_t i = 0;
    while (true) {
        size_t j = s.find(L'\t', i);
        if (j == std::wstring::npos) { r.push_back(s.substr(i)); break; }
        r.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return r;
}

std::vector<HistItem> LoadHistoryItems() {
    std::vector<HistItem> out;
    std::string s = ReadFileA(GetHistoryPath());
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('\n', i);
        if (j == std::string::npos) j = s.size();
        std::wstring line = W(s.substr(i, j - i));
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        i = j + 1;
        if (line.empty()) continue;
        auto f = SplitTabs(line);
        HistItem h;
        h.url = Trim(f[0]);
        if (h.url.empty()) continue;
        if (f.size() > 1) h.title = f[1];
        if (f.size() > 2) h.when = f[2];
        out.push_back(h);
    }
    return out;
}

static void WriteHistory(const std::vector<HistItem>& h) {
    std::string data;
    for (size_t i = 0; i < h.size(); i++) {
        if (i) data += "\n";
        std::wstring t = h[i].title;
        for (auto& c : t) if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
        data += U8(h[i].url);
        if (!t.empty() || !h[i].when.empty()) data += "\t" + U8(t) + "\t" + U8(h[i].when);
    }
    WriteFileA(GetHistoryPath(), data);
}

std::vector<std::wstring> LoadHistory() {
    std::vector<std::wstring> out;
    for (auto& h : LoadHistoryItems()) out.push_back(h.url);
    return out;
}

void SaveHistory(const std::wstring& url, const std::wstring& title) {
    auto h = LoadHistoryItems();
    h.erase(std::remove_if(h.begin(), h.end(), [&](const HistItem& x) { return x.url == url; }), h.end());
    HistItem n;
    n.url = url;
    n.title = title;
    time_t t = time(nullptr);
    tm lt;
    localtime_s(&lt, &t);
    wchar_t buf[32];
    wcsftime(buf, 32, L"%d.%m.%Y %H:%M", &lt);
    n.when = buf;
    h.insert(h.begin(), n);
    if (h.size() > 1000) h.resize(1000);
    WriteHistory(h);
}

void DeleteHistory(const std::wstring& url) {
    auto h = LoadHistoryItems();
    h.erase(std::remove_if(h.begin(), h.end(), [&](const HistItem& x) { return x.url == url; }), h.end());
    WriteHistory(h);
}

void ClearHistory() { WriteFileA(GetHistoryPath(), ""); }

std::vector<std::wstring> LoadHistoryCombo() {
    auto h = LoadHistory();
    if (h.size() > 5) h.resize(5);
    return h;
}

// ═════════════════════════════ настройки ══════════════════════════════════════
std::wstring GetConfigPath() { return ResPath(L"config.json"); }

static json LoadCfg() {
    try {
        std::string s = ReadFileA(GetConfigPath());
        if (s.empty()) return json::object();
        json j = json::parse(s, nullptr, false);
        return j.is_object() ? j : json::object();
    } catch (...) { return json::object(); }
}

std::wstring CfgGet(const char* key, const std::wstring& def) {
    json j = LoadCfg();
    if (j.contains(key) && j[key].is_string()) return W(j[key].get<std::string>());
    return def;
}

void CfgSet(const char* key, const std::wstring& val) {
    json j = LoadCfg();
    j[key] = U8(val);
    WriteFileA(GetConfigPath(), j.dump(2));
}

int CfgGetInt(const char* key, int def) {
    json j = LoadCfg();
    if (j.contains(key) && j[key].is_number_integer()) return j[key].get<int>();
    return def;
}

void CfgSetInt(const char* key, int val) {
    json j = LoadCfg();
    j[key] = val;
    WriteFileA(GetConfigPath(), j.dump(2));
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

bool Proc::Start(const std::vector<std::wstring>& args, bool capture, const std::wstring& cwd) {
    std::lock_guard<std::mutex> lk(g_spawnMu);
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr, nulIn = INVALID_HANDLE_VALUE, nulOut = INVALID_HANDLE_VALUE;
    if (capture) {
        if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
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
    si.hStdError  = capture ? wr : nulOut;

    std::wstring cmd = BuildCmdLine(args);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (wr) CloseHandle(wr);
    if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
    if (nulOut != INVALID_HANDLE_VALUE) CloseHandle(nulOut);
    if (!ok) {
        if (rd) CloseHandle(rd);
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
    return true;
}

DWORD Proc::Read(char* buf, DWORD n) {
    if (!hRead) return 0;
    DWORD got = 0;
    if (!ReadFile(hRead, buf, n, &got, nullptr)) return 0;
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
    if (hProc) { CloseHandle(hProc); hProc = nullptr; }
    if (hJob)  { CloseHandle(hJob);  hJob = nullptr; }
}

ProcResult RunCapture(const std::vector<std::wstring>& args, DWORD timeoutMs, const std::wstring& cwd) {
    ProcResult r;
    Proc p;
    if (!p.Start(args, true, cwd)) return r;
    r.started = true;
    std::string out;
    std::thread t([&]() {
        char buf[65536];
        DWORD n;
        while ((n = p.Read(buf, sizeof buf)) > 0) out.append(buf, n);
    });
    DWORD w = WaitForSingleObject(p.hProc, timeoutMs);
    if (w == WAIT_TIMEOUT) { r.timedOut = true; p.Kill(); }
    t.join();
    DWORD code = 0;
    GetExitCodeProcess(p.hProc, &code);
    r.code = r.timedOut ? -1 : (int)code;
    r.out = std::move(out);
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

static HINTERNET OpenUrl(HINTERNET h, const std::wstring& url, const std::wstring& extraHeaders = L"") {
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI |
                  INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_COOKIES;
    std::wstring hdr = L"Accept: */*\r\n" + extraHeaders;
    HINTERNET u = InternetOpenUrlW(h, url.c_str(), hdr.c_str(), (DWORD)-1, flags, 0);
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

static bool HttpDownloadSingle(const std::wstring& url, const std::wstring& file,
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

// ── Многопоточная загрузка (Range-запросы) ───────────────────────────────────
// Узнаёт размер файла и итоговый адрес (после редиректов); true — сервер поддерживает Range.
static bool ProbeRange(const std::wstring& url, uint64_t& total, std::wstring& finalUrl, DWORD timeoutMs) {
    Inet net;
    net.h = OpenInet(timeoutMs);
    if (!net.h) return false;
    HINTERNET u = OpenUrl(net.h, url, L"Range: bytes=0-0\r\n");
    if (!u) return false;
    bool ok = false;
    DWORD status = 0, sz = sizeof status;
    if (HttpQueryInfoW(u, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &sz, nullptr) && status == 206) {
        wchar_t cr[128] = {};
        DWORD cl = sizeof cr;
        if (HttpQueryInfoW(u, HTTP_QUERY_CONTENT_RANGE, cr, &cl, nullptr)) {      // «bytes 0-0/35827712»
            const wchar_t* slash = wcsrchr(cr, L'/');
            if (slash) { total = _wcstoui64(slash + 1, nullptr, 10); ok = total > 0; }
        }
    }
    wchar_t fu[4096] = {};
    DWORD fl = sizeof fu;
    if (InternetQueryOptionW(u, INTERNET_OPTION_URL, fu, &fl) && fu[0]) finalUrl = fu;
    else finalUrl = url;
    InternetCloseHandle(u);
    return ok;
}

static bool HttpDownloadParallel(const std::wstring& url, const std::wstring& file,
                                 const std::function<bool(uint64_t, uint64_t)>& progress, DWORD timeoutMs, int threads) {
    uint64_t total = 0;
    std::wstring finalUrl;
    if (!ProbeRange(url, total, finalUrl, timeoutMs) || total < (2u << 20)) return false;

    // файл нужного размера создаём заранее, каждый поток пишет в свой участок
    {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)total;
        bool okSize = SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && SetEndOfFile(h);
        CloseHandle(h);
        if (!okSize) { DeleteFileW(file.c_str()); return false; }
    }

    DWORD mc = 32;                                           // по умолчанию WinINet пускает к серверу всего 2-6 соединений
    InternetSetOptionW(nullptr, INTERNET_OPTION_MAX_CONNS_PER_SERVER, &mc, sizeof mc);
    InternetSetOptionW(nullptr, INTERNET_OPTION_MAX_CONNS_PER_1_0_SERVER, &mc, sizeof mc);

    struct Seg { uint64_t pos = 0, end = 0; bool done = false; };
    std::vector<Seg> segs((size_t)threads);
    uint64_t chunk = (total + threads - 1) / threads;
    for (int i = 0; i < threads; i++) {
        segs[i].pos = (uint64_t)i * chunk;
        segs[i].end = std::min<uint64_t>(total, (uint64_t)(i + 1) * chunk) - 1;      // включительно
        if (segs[i].pos > segs[i].end) segs[i].done = true;
    }

    std::atomic<uint64_t> got{0};
    std::atomic<bool> stop{false}, rangeFail{false};
    std::atomic<int> running{0};

    auto worker = [&](int idx) {
        Seg& sg = segs[idx];
        HANDLE fh = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fh == INVALID_HANDLE_VALUE) { rangeFail = true; running--; return; }
        std::vector<char> buf(65536);
        for (int attempt = 0; attempt < 5 && !sg.done && !stop && !rangeFail; attempt++) {
            if (attempt) Sleep(500 * attempt);
            Inet net;
            net.h = OpenInet(timeoutMs);
            if (!net.h) continue;
            std::wstring rng = L"Range: bytes=" + std::to_wstring(sg.pos) + L"-" + std::to_wstring(sg.end) + L"\r\n";
            HINTERNET u = OpenUrl(net.h, finalUrl, rng);
            if (!u) continue;
            DWORD status = 0, sz = sizeof status;
            if (!HttpQueryInfoW(u, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &sz, nullptr) || status != 206) {
                InternetCloseHandle(u);
                rangeFail = true;                             // сервер проигнорировал Range — параллельно не выйдет
                break;
            }
            while (!stop && sg.pos <= sg.end) {
                DWORD want = (DWORD)std::min<uint64_t>(buf.size(), sg.end - sg.pos + 1), n = 0;
                if (!InternetReadFile(u, buf.data(), want, &n) || n == 0) break;
                LARGE_INTEGER li; li.QuadPart = (LONGLONG)sg.pos;
                DWORD wr = 0;
                if (!SetFilePointerEx(fh, li, nullptr, FILE_BEGIN) || !WriteFile(fh, buf.data(), n, &wr, nullptr) || wr != n) { rangeFail = true; break; }
                sg.pos += n;
                got += n;
            }
            InternetCloseHandle(u);
            if (sg.pos > sg.end) sg.done = true;
        }
        CloseHandle(fh);
        running--;
    };

    std::vector<std::thread> pool;
    running = threads;
    for (int i = 0; i < threads; i++) pool.emplace_back(worker, i);

    bool cancelled = false;
    while (running > 0) {
        Sleep(100);
        if (progress && !progress(got.load(), total)) { cancelled = true; stop = true; }
        if (rangeFail) stop = true;
    }
    for (auto& t : pool) t.join();

    bool allDone = true;
    for (auto& sg : segs) allDone = allDone && sg.done;
    if (progress && allDone) progress(total, total);
    if (cancelled) { DeleteFileW(file.c_str()); return false; }
    if (!allDone) { DeleteFileW(file.c_str()); return false; }
    return true;
}

// threads > 1 — пробуем качать в несколько потоков; если сервер не поддерживает Range
// или что-то пошло не так, молча откатываемся на обычную загрузку одним потоком.
bool HttpDownload(const std::wstring& url, const std::wstring& file,
                  const std::function<bool(uint64_t, uint64_t)>& progress, DWORD timeoutMs, int threads) {
    if (threads > 1) {
        bool cancelled = false;
        auto wrapped = [&](uint64_t d, uint64_t t) { bool r = !progress || progress(d, t); if (!r) cancelled = true; return r; };
        if (HttpDownloadParallel(url, file, wrapped, timeoutMs, std::min(threads, 16))) return true;
        if (cancelled) return false;
    }
    return HttpDownloadSingle(url, file, progress, timeoutMs);
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
