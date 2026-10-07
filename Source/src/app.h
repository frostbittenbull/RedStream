#pragma once
#include "ui.h"

// ── Геометрия окна в «CSS-пикселях» макета (умножается на g_k) ───────────────
namespace lay {
constexpr float W        = 546;   // ширина колонки макета (1200 − 48 − 60) / 2
constexpr float TitleH   = 26;
constexpr float ToolbarH = 25;
constexpr float BannerH  = 26;
constexpr float Pad      = 14;
constexpr float BaseH    = 394;
}

struct Preview {
    bool valid = false;
    std::wstring title, thumbUrl, extractor, bestRes;
    double duration = -1;
};

struct Banner {
    int kind;                     // 0 = app, 1 = yt-dlp, 2 = ffmpeg
    std::wstring pre, link, post;
    Ctl* linkCtl = nullptr;
};

struct App {
    HINSTANCE hinst = nullptr;
    HWND hwnd = nullptr;
    Panel* main = nullptr;

    // главные контролы
    Ctl *urlEdit = nullptr, *gear = nullptr, *prevCard = nullptr;
    Ctl *pFormat = nullptr, *pVc = nullptr, *pAc = nullptr, *pRes = nullptr, *pFps = nullptr;
    Ctl *pBrowser = nullptr, *pFolder = nullptr;
    Ctl *lblWarn = nullptr, *bar = nullptr, *lblLeft = nullptr, *lblRight = nullptr;
    Ctl *btnDl = nullptr, *btnOpen = nullptr;
    Ctl *btnSettings = nullptr, *btnExtra = nullptr, *btnHelp = nullptr, *btnTray = nullptr;

    // состояние
    std::wstring destFolder;
    Preview prev;
    json rawFormats = json::array();
    bool hasFormats = false;
    std::vector<std::wstring> savedVcs, savedAcs, savedRess, savedFpss;
    std::wstring trimStart, trimEnd;          // "ЧЧ:ММ:СС" или пусто
    std::wstring sizeText;                    // «~604,80 МБ»
    std::unique_ptr<G::Bitmap> thumb;
    std::wstring prevUrl;
    std::atomic<int> prevGen{0};

    // спиннер на кнопке ⚙
    bool spinning = false;
    int  spinPhase = 0;

    // загрузка
    std::shared_ptr<Proc> dlProc;
    std::atomic<int> dlGen{0};
    bool  downloading = false;
    bool  videoStream = true;

    // баннеры
    std::vector<Banner> banners;

    // трей
    bool inTray = false;
    bool tempToastIcon = false;

    // окна-попапы / меню
    Panel* menu = nullptr;                    // открытое выпадающее меню
    Ctl*   menuAnchor = nullptr;
    Panel* dlSettings = nullptr;
    Panel* proxyPopup = nullptr;
    Panel* aboutPopup = nullptr;
    Panel* updaterPopup = nullptr;
};
extern App A;

// ── app.cpp ──────────────────────────────────────────────────────────────────
RECT  CtlScreenRect(Panel* p, const Ctl* c);
void  CenterPopup(Panel* pop, int dy = 0);
void  PillDropdown(Panel* panel, Ctl* c, const std::vector<std::wstring>& items, int sel,
                   std::function<void(int)> onPick, bool darkList = true);
void  SetGearEnabled(bool en);
void  ClearFields();
void  MinimizeToTray();
void  ShowToast(const std::wstring& title, const std::wstring& msg);
void  OnAnyMouseDown(HWND hw, POINT pt);
void  CloseMenu();
void  ReportError(const std::wstring& msg);       // модальное окно «⚠ Внимание!»
bool  IsAudioExt(const std::wstring& ext);
std::wstring FormatExt();                          // mkv/mp4/opus/m4a/mp3

// ── popups.cpp ───────────────────────────────────────────────────────────────
Panel* NewPopup(float wCss, float hCss, const std::wstring& title, std::function<void()> onClose);
void   ShowAbout();
void   ShowProxy();
void   ShowScheduler();
void   ShowUpdater();
void   ShowDlSettings();
void   ToggleDlSettings();
void   OpenMenu(int which);        // 0 = Настройки, 1 = Дополнительно, 2 = Помощь
bool   SchedulerVisible();
void   HideScheduler();
void   SchedTick();
void   UpdaterPulse();
bool   UpdaterBusy();
void   CloseDlSettings();
