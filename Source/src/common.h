#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  RedStream: Video Downloader — C++ port of main.py
//  Common declarations
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <algorithm>
using std::min;
using std::max;

#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wininet.h>
#include <mmsystem.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "../third_party/json.hpp"

namespace G = Gdiplus;
using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Версия ───────────────────────────────────────────────────────────────────
#define APP_VERSION      L"3.0"
#define GITHUB_RELEASES  L"https://github.com/frostbittenbull/RedStream/releases/latest"
#define GITHUB_API_URL   L"https://api.github.com/repos/frostbittenbull/RedStream/releases/latest"
#define GITHUB_REPO_URL  L"https://github.com/frostbittenbull/RedStream"

// ── Палитра (взята 1-в-1 из CSS макета .mockup-*) ────────────────────────────
namespace col {
    constexpr uint32_t Bar          = 0x151515;  // .mockup-titlebar / .mockup-toolbar
    constexpr uint32_t BarBorder    = 0x222222;  // toolbar border-top
    constexpr uint32_t Body         = 0x1e1e1e;  // .mockup-body
    constexpr uint32_t WinBorder    = 0x2a2a2a;  // rgba(255,255,255,.07) на тёмном
    constexpr uint32_t Red          = 0xff0000;
    constexpr uint32_t RedHover     = 0xcc0000;
    constexpr uint32_t RedSoft      = 0xff6b6b;  // .mockup-combo span
    constexpr uint32_t Orange       = 0xff6600;  // конец градиента прогресса
    constexpr uint32_t Green        = 0x00bf00;
    constexpr uint32_t Amber        = 0xffaa00;
    constexpr uint32_t TitleText    = 0x888888;
    constexpr uint32_t WinBtn       = 0x666666;
    constexpr uint32_t MenuText     = 0x999999;
    constexpr uint32_t InputBg      = 0x2a2a2a;
    constexpr uint32_t InputBorder  = 0x444444;
    constexpr uint32_t InputText    = 0xbdbdbd;
    constexpr uint32_t GrayBg       = 0x333333;
    constexpr uint32_t GrayHover    = 0x3c3c3c;
    constexpr uint32_t GrayFg       = 0x999999;
    constexpr uint32_t GrayBorder   = 0x444444;
    constexpr uint32_t ComboBg      = 0x2e2e2e;
    constexpr uint32_t ComboBorder  = 0x484848;
    constexpr uint32_t ComboLabel   = 0xbbbbbb;
    constexpr uint32_t PrevBg       = 0x252525;
    constexpr uint32_t PrevBorder   = 0x333333;
    constexpr uint32_t PrevTitle    = 0xeeeeee;
    constexpr uint32_t PrevMeta     = 0x777777;
    constexpr uint32_t BarTrack     = 0x2a2a2a;
    constexpr uint32_t BarBorderC   = 0x444444;
    constexpr uint32_t Dim          = 0x555555;
    constexpr uint32_t Popup        = 0x1e1e1e;
    constexpr uint32_t PopupBorder  = 0x444444;
    constexpr uint32_t Sep          = 0x333333;
    constexpr uint32_t Label        = 0xaaaaaa;
    constexpr uint32_t Text         = 0xdddddd;
    constexpr uint32_t Err          = 0xff4444;
}

inline G::Color C(uint32_t rgb, int a = 255) {
    return G::Color((BYTE)a, (BYTE)((rgb >> 16) & 0xff), (BYTE)((rgb >> 8) & 0xff), (BYTE)(rgb & 0xff));
}
inline COLORREF CR(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

// ── Масштаб: 1 CSS-пиксель макета = 1.5 px при 96 DPI ─────────────────────────
extern float g_k;
inline int   U(float v)  { return (int)std::lround(v * g_k); }
inline float UF(float v) { return v * g_k; }

// ── Пользовательские сообщения ───────────────────────────────────────────────
#define WM_APP_CALL  (WM_APP + 1)   // lParam = std::function<void()>*
#define WM_TRAYICON  (WM_APP + 2)

// ── Строки ───────────────────────────────────────────────────────────────────
std::wstring W(const std::string& s);
std::string  U8(const std::wstring& s);
std::wstring Lower(std::wstring s);
std::wstring Trim(const std::wstring& s);
std::string  TrimA(const std::string& s);
bool         StartsWith(const std::wstring& s, const std::wstring& p);
bool         EndsWith(const std::wstring& s, const std::wstring& p);
std::wstring Fmt(const wchar_t* f, ...);

// ── Файлы / настройки (всё лежит рядом с exe) ───────────────────────────────
std::wstring ExePath();
std::wstring ExeDir();
std::wstring ResPath(const std::wstring& name);
bool         FileExists(const std::wstring& p);
bool         DirExists(const std::wstring& p);
std::string  ReadFileA(const std::wstring& path);
bool         WriteFileA(const std::wstring& path, const std::string& data, bool append = false);

std::wstring GetDownloadsFolder();
std::wstring LoadDestFolder();
void         SaveDestFolder(const std::wstring& f);

struct ProxyCfg {
    bool enabled = false;
    std::string type = "http", host, port, user, password;
};
ProxyCfg     LoadProxy();
void         SaveProxy(const ProxyCfg& p);
std::wstring GetProxyUrl();                  // пусто, если прокси выключен

std::wstring GetLogPath();
std::wstring GetHistoryPath();
std::vector<std::wstring> LoadHistory();
void         SaveHistory(const std::wstring& url);
std::vector<std::wstring> LoadHistoryCombo();  // последние 5

// ── Процессы ─────────────────────────────────────────────────────────────────
std::wstring BuildCmdLine(const std::vector<std::wstring>& args);

struct Proc {
    HANDLE hProc = nullptr, hRead = nullptr, hReadErr = nullptr, hJob = nullptr;
    DWORD  pid = 0;
    bool Start(const std::vector<std::wstring>& args, bool capture, const std::wstring& cwd = L"", bool separateErr = false);
    // читает до n байт; 0 = конец потока
    DWORD Read(char* buf, DWORD n);
    DWORD ReadErr(char* buf, DWORD n);
    int   Wait(DWORD ms = INFINITE);       // код возврата, -1 при таймауте/ошибке
    void  Kill();                          // убивает всё дерево процессов (job object)
    void  Close();
    bool  Running() const;
};

struct ProcResult { int code = -1; std::string out; bool started = false, timedOut = false; };
ProcResult RunCapture(const std::vector<std::wstring>& args, DWORD timeoutMs, const std::wstring& cwd = L"",
                      std::string* errOut = nullptr);   // errOut != nullptr → stderr отдельно от stdout

// ── Сеть (WinINet) ───────────────────────────────────────────────────────────
bool HttpGet(const std::wstring& url, std::string& out, DWORD timeoutMs = 8000);
// progress(done,total) -> false = прервать
bool HttpDownload(const std::wstring& url, const std::wstring& file,
                  const std::function<bool(uint64_t, uint64_t)>& progress, DWORD timeoutMs = 60000);

// ── Система ──────────────────────────────────────────────────────────────────
void PlayKindSound(const char* kind);                 // "success" | "error"
void ShellOpen(const std::wstring& path);
void ShowToast(const std::wstring& title, const std::wstring& msg);
bool IsAutostart();
void SetAutostart(bool enable);

// ── UI-поток ─────────────────────────────────────────────────────────────────
extern HWND g_mainWnd;
void PostUI(std::function<void()> fn);   // выполнить в UI-потоке (из любого потока)
