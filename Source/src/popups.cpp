#include "app.h"
#include <ctime>

bool MenuWasJustClosed(int which);

enum { T_SCHED = 3, T_PULSE = 5 };

// Источники компонентов (можно переопределить при сборке: -DURL_YTDLP=L"...")
#ifndef URL_7ZA
#define URL_7ZA    L"https://github.com/martinrotter/7za/raw/master/7za.exe"
#endif
#ifndef URL_YTDLP
#define URL_YTDLP  L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
#endif
#ifndef URL_FFMPEG
#define URL_FFMPEG L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-git-essentials.7z"
#endif

// Распаковка архива ffmpeg через 7za.exe и установка ffmpeg.exe рядом с программой.
// Общая для «Обновить ffmpeg» и установщика недостающих компонентов. Вызывается из рабочего потока.
static bool InstallFfmpegFromArchive(const std::wstring& base, const std::wstring& archive, std::wstring& err) {
    if (!FileExists(base + L"\\7za.exe")) { err = L"не найден 7za.exe"; return false; }
    ProcResult r = RunCapture({base + L"\\7za.exe", L"x", archive, L"-o" + base, L"-y"}, 600000, base);
    if (!r.started || r.code != 0) { err = L"не удалось распаковать архив"; return false; }
    bool moved = false;
    try {
        for (auto& e : fs::directory_iterator(fs::path(base))) {
            std::wstring name = e.path().filename().wstring();
            if (e.is_directory() && StartsWith(name, L"ffmpeg-") && name.find(L"-git-") != std::wstring::npos) {
                fs::path src = e.path() / L"bin" / L"ffmpeg.exe";
                fs::path dst = fs::path(base) / L"ffmpeg.exe";
                if (fs::exists(src)) {
                    std::error_code ec;
                    fs::remove(dst, ec);
                    fs::rename(src, dst, ec);
                    if (ec) { ec.clear(); fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec); }
                    moved = !ec;
                }
                std::error_code ec2;
                fs::remove_all(e.path(), ec2);
                break;
            }
        }
    } catch (...) {}
    if (!moved || !FileExists(base + L"\\ffmpeg.exe")) { err = L"в архиве не найден ffmpeg.exe"; return false; }
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Каркас всплывающего окна (рамка, заголовок, ✕)
// ═════════════════════════════════════════════════════════════════════════════
Panel* NewPopup(float wCss, float hCss, const std::wstring& title, std::function<void()> onClose) {
    auto* p = new Panel();
    int w = (int)std::lround(UF(wCss)), h = (int)std::lround(UF(hCss));
    p->Create(A.hwnd, WS_POPUP, WS_EX_TOOLWINDOW, 0, 0, w, h);
    p->pre = [p](UINT m, WPARAM, LPARAM l, LRESULT& out) {
        if (m == WM_NCHITTEST) {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(p->hwnd, &pt);
            RECT rc;
            GetClientRect(p->hwnd, &rc);
            if (pt.y >= 0 && pt.y < (int)UF(36) && pt.x < rc.right - (int)UF(32)) { out = HTCAPTION; return true; }
        }
        return false;
    };
    p->paintBg = [title](G::Graphics& g, const RECT& cr) {
        float W = (float)cr.right, H = (float)cr.bottom;
        G::SolidBrush bg(C(col::Popup));
        g.FillRectangle(&bg, 0.f, 0.f, W, H);
        G::Pen pen(C(col::PopupBorder), std::max(1.f, UF(1)));
        g.DrawRectangle(&pen, 0.5f, 0.5f, W - 1, H - 1);
        if (!title.empty()) {
            gfx::Text(g, title, G::RectF(UF(14), UF(8), W - UF(60), UF(22)), gfx::GetFont(UF(11.5f), true), C(col::Red),
                      gfx::CENTER, true);
            G::SolidBrush sep(C(col::Sep));
            g.FillRectangle(&sep, UF(16), UF(36), W - UF(32), std::max(1.f, UF(1)));
        }
    };
    Ctl* x = p->AddButton(G::RectF((float)w - UF(30), UF(2), UF(28), UF(20)), L"", [p, onClose]() {
        if (onClose) onClose();
        else Panel::Dispose(p);
    });
    x->icon = gfx::I_CLOSE; x->iconPx = UF(9); x->fg = 0x888888; x->fgH = 0xffffff; x->bgH = 0xc42b1c; x->rad = 0;
    x->body = false;
    return p;
}

static Ctl* HLine(Panel* p, float yCss, float wCss, float padCss = 16) {
    Ctl* c = p->Add(K::Custom, G::RectF(UF(padCss), UF(yCss), UF(wCss - padCss * 2), std::max(1.f, UF(1))));
    c->draw = [](G::Graphics& g, Ctl& r) { G::SolidBrush b(C(col::Sep)); g.FillRectangle(&b, r.r); };
    return c;
}

static Ctl* Lbl(Panel* p, float x, float y, float w, float h, const std::wstring& t, float px, uint32_t color,
                int ha = gfx::LEFT, bool bold = false) {
    return p->AddLabel(G::RectF(UF(x), UF(y), UF(w), UF(h)), t, UF(px), color, ha, bold);
}

static Ctl* Btn(Panel* p, float x, float y, float w, float h, const std::wstring& t, float px, std::function<void()> fn) {
    Ctl* b = p->AddButton(G::RectF(UF(x), UF(y), UF(w), UF(h)), t, std::move(fn));
    b->px = UF(px);
    b->bg = col::GrayBg; b->bgH = col::GrayHover; b->bgD = 0x2a2a2a;
    b->bd = col::GrayBorder; b->bdH = 0x555555; b->bdD = 0x333333;
    b->fg = 0xcccccc; b->fgH = 0xffffff; b->fgD = 0x555555;
    b->rad = UF(5);
    return b;
}

static Ctl* RedBtn(Panel* p, float x, float y, float w, float h, const std::wstring& t, float px, std::function<void()> fn) {
    Ctl* b = Btn(p, x, y, w, h, t, px, std::move(fn));
    b->bg = col::Red; b->bgH = col::RedHover; b->bd = col::Red; b->bdH = col::RedHover;
    b->fg = 0xffffff; b->fgH = 0xffffff; b->bold = true;
    return b;
}

static Ctl* Edt(Panel* p, float x, float y, float w, float h, float px, bool mono, std::function<void()> ch = nullptr,
                bool pwd = false, const wchar_t* cue = nullptr) {
    Ctl* e = p->AddEdit(G::RectF(UF(x), UF(y), UF(w), UF(h)), UF(px), mono, std::move(ch), pwd, cue);
    e->padX = UF(7);
    p->LayoutEdit(e);
    return e;
}

static void SetEnabled(Panel* p, Ctl* c, bool en) {
    c->en = en;
    if (c->h) EnableWindow(c->h, en);
    p->Inval(c);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Выпадающие меню (Настройки / Дополнительно / Помощь)
// ═════════════════════════════════════════════════════════════════════════════
struct MenuItem {
    std::wstring text;
    std::function<void()> fn;
    bool enabled = true;
    bool check = false, checked = false;
    uint32_t color = 0xffffff;
};

static void BuildMenu(Ctl* anchor, int which, std::vector<MenuItem> items) {
    CloseMenu();
    HDC dc = GetDC(A.hwnd);
    G::Graphics g(dc);
    float maxw = 0;
    for (auto& it : items) maxw = std::max(maxw, gfx::TextWidth(g, it.text, gfx::GetFont(UF(10))) + (it.check ? UF(18) : 0));
    ReleaseDC(A.hwnd, dc);
    float rowH = UF(25);
    int W = (int)std::lround(maxw + UF(34)), H = (int)std::lround(rowH * items.size() + UF(8));
    auto* p = new Panel();
    int x = (int)anchor->r.X, y = (int)(anchor->r.Y + anchor->r.Height);
    p->Create(A.hwnd, WS_CHILD | WS_VISIBLE, 0, x, y, W, H);
    p->paintBg = [](G::Graphics& g, const RECT& cr) {
        G::SolidBrush bg(C(0x232323));
        g.FillRectangle(&bg, 0, 0, cr.right, cr.bottom);
        G::Pen pen(C(0x484848), 1.f);
        g.DrawRectangle(&pen, 0.5f, 0.5f, (float)cr.right - 1, (float)cr.bottom - 1);
    };
    float yy = UF(4);
    for (auto& it : items) {
        Ctl* b;
        if (it.check) {
            b = p->AddCheck(G::RectF(UF(10), yy, (float)W - UF(20), rowH), it.text, UF(10), it.checked, nullptr);
            auto fn = it.fn;
            Ctl* self = b;
            b->onClick = [self, fn]() { self->checked = !self->checked; if (fn) fn(); };
            b->hand = true;
            // подсветка строки при наведении
            Ctl* hl = p->Add(K::Custom, G::RectF(UF(3), yy, (float)W - UF(6), rowH));
            hl->draw = [self](G::Graphics& gg, Ctl& c) { if (self->hover) { G::SolidBrush hb(C(0x3a3a3a)); gg.FillRectangle(&hb, c.r); } };
            // custom должен рисоваться ДО чекбокса
            std::swap(p->ctls[p->ctls.size() - 1], p->ctls[p->ctls.size() - 2]);
        } else {
            b = p->AddButton(G::RectF(UF(3), yy, (float)W - UF(6), rowH), it.text, nullptr);
            b->px = UF(10); b->ha = gfx::LEFT; b->bgH = 0x3a3a3a; b->rad = 0;
            b->fg = it.color; b->fgH = it.color; b->fgD = 0x555555;
            b->en = it.enabled;
            auto fn = it.fn;
            b->onClick = [fn]() { CloseMenu(); if (fn) fn(); };
        }
        yy += rowH;
    }
    A.menu = p;
    A.menuAnchor = anchor;
    SetWindowPos(p->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void OpenMenu(int which) {
    if (MenuWasJustClosed(which)) return;     // повторный клик по той же кнопке закрывает меню
    CloseList();
    Ctl* anchor = which == 0 ? A.btnSettings : which == 1 ? A.btnExtra : A.btnHelp;
    std::vector<MenuItem> items;
    if (which == 0) {
        MenuItem a; a.text = L"Запускать RedStream при запуске компьютера"; a.check = true; a.checked = IsAutostart();
        a.fn = []() { SetAutostart(!IsAutostart()); };
        // чекбокс уже переключил своё состояние; применяем к реестру по его значению
        items.push_back(a);
        bool logEx = FileExists(GetLogPath()), hisEx = FileExists(GetHistoryPath());
        MenuItem l; l.text = L"Открыть лог-файл"; l.enabled = logEx; l.fn = []() { ShellOpen(GetLogPath()); };
        MenuItem h; h.text = L"Открыть файл истории"; h.enabled = hisEx; h.fn = []() { ShellOpen(GetHistoryPath()); };
        MenuItem c; c.text = L"Очистить поля"; c.fn = []() { ClearFields(); };
        MenuItem e; e.text = L"Выход"; e.color = col::Err; e.fn = []() { DestroyWindow(A.hwnd); };
        items.push_back(l); items.push_back(h); items.push_back(c); items.push_back(e);
    } else if (which == 1) {
        MenuItem a; a.text = L"Планировщик"; a.fn = []() { ShowScheduler(); };
        MenuItem b; b.text = L"Настроить прокси"; b.fn = []() { ShowProxy(); };
        MenuItem c; c.text = L"Обновить компоненты"; c.fn = []() { ShowUpdater(); };
        items = {a, b, c};
    } else {
        MenuItem a; a.text = L"Справка"; a.fn = []() {
            std::wstring g = ResPath(L"guide.txt");
            if (!FileExists(g)) WriteFileA(g, "");
            ShellOpen(g);
        };
        MenuItem b; b.text = L"О программе…"; b.fn = []() { ShowAbout(); };
        items = {a, b};
    }
    BuildMenu(anchor, which, items);
}

// ═════════════════════════════════════════════════════════════════════════════
//  О программе
// ═════════════════════════════════════════════════════════════════════════════
void ShowAbout() {
    if (A.aboutPopup) { Panel::Dispose(A.aboutPopup); A.aboutPopup = nullptr; }
    CloseMenu();
    auto logo = gfx::LoadBitmapRes(3, L"logo.png");
    float logoW = 130, logoH = 0;
    if (logo) logoH = logoW * logo->GetHeight() / logo->GetWidth();
    if (logoH > 90) { logoW = logoW * 90 / logoH; logoH = 90; }

    float W = 270, y = 16;
    float H = 16 + (logo ? logoH + 6 : 0) + 22 + 10 + 74 + 10 + 5 * 20 + 24;
    Panel* p = NewPopup(W, H, L"", []() { Panel::Dispose(A.aboutPopup); A.aboutPopup = nullptr; });
    A.aboutPopup = p;
    auto* lg = new std::shared_ptr<G::Bitmap>(logo.release());
    if (*lg) {
        float lw = logoW, lh = logoH, ly = y;
        Ctl* c = p->Add(K::Custom, G::RectF(UF((W - lw) / 2), UF(ly), UF(lw), UF(lh)));
        auto bmp = *lg;
        c->draw = [bmp](G::Graphics& g, Ctl& r) { g.DrawImage(bmp.get(), r.r); };
        y += logoH + 6;
    }
    delete lg;
    Lbl(p, 0, y, W, 22, L"RedStream: Video Downloader", 12, col::Red, gfx::CENTER, true);
    y += 22 + 4;
    HLine(p, y, W, 20);
    y += 8;
    Ctl* d = Lbl(p, 10, y, W - 20, 74,
                 L"С помощью этой утилиты\nВы можете скачивать видео\nс популярных сайтов, таких как:\nYouTube, Instagram, Tik-Tok,\nи многих других.",
                 10.5f, 0xcccccc, gfx::CENTER);
    d->wrap = true;
    y += 74 + 2;
    HLine(p, y, W, 20);
    y += 8;
    struct Row { const wchar_t* l; const wchar_t* v; uint32_t c; bool link; };
    Row rows[] = {{L"Автор:", L"#frostbittenbull", 0xffffff, false}, {L"Сайт:", L"github.com", 0x4ea8de, true},
                  {L"Версия:", APP_VERSION, 0xaaaaaa, false}, {L"Сборка:", L"06.10.2026", 0xaaaaaa, false},
                  {L"Платформа:", L"Windows 10/11", 0xaaaaaa, false}};
    for (auto& r : rows) {
        Lbl(p, 36, y, 80, 20, r.l, 10.5f, 0x777777);
        if (r.link) {
            Ctl* b = p->AddButton(G::RectF(UF(118), UF(y), UF(120), UF(20)), r.v, []() { ShellOpen(GITHUB_REPO_URL); });
            b->px = UF(10.5f); b->ul = true; b->fg = r.c; b->fgH = 0x8ccaf0; b->ha = gfx::LEFT; b->rad = 0;
        } else {
            Lbl(p, 118, y, 140, 20, r.v, 10.5f, r.c);
        }
        y += 20;
    }
    CenterPopup(p);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Прокси
// ═════════════════════════════════════════════════════════════════════════════
void ShowProxy() {
    if (A.proxyPopup) { SetWindowPos(A.proxyPopup->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); return; }
    CloseMenu();
    ProxyCfg cfg = LoadProxy();

    struct UI {
        Ctl *enabled, *host, *port, *user, *pass, *authCb;
        Ctl *lServer, *lProto, *lAuth, *lAddr, *lPort, *lUser, *lPass;
        std::vector<Ctl*> radios;
        std::vector<std::string> vals;
        std::string proto;
    };
    auto ui = std::make_shared<UI>();
    ui->proto = cfg.type.empty() ? "http" : cfg.type;

    float W = 300, H = 330;
    Panel* p = nullptr;
    auto save = [ui, &p]() {};
    (void)save;

    p = NewPopup(W, H, L"Настройки прокси", nullptr);
    A.proxyPopup = p;
    auto doClose = [ui, p]() {
        ProxyCfg c;
        c.enabled = ui->enabled->checked;
        c.type = ui->proto;
        c.host = TrimA(U8(A.proxyPopup ? p->GetText(ui->host) : L""));
        c.port = TrimA(U8(p->GetText(ui->port)));
        bool auth = ui->authCb->checked;
        c.user = auth ? TrimA(U8(p->GetText(ui->user))) : "";
        c.password = auth ? TrimA(U8(p->GetText(ui->pass))) : "";
        SaveProxy(c);
        A.proxyPopup = nullptr;
        Panel::Dispose(p);
    };
    // ✕ → сохранить и закрыть
    for (auto& c : p->ctls) if (c->icon == gfx::I_CLOSE) c->onClick = doClose;

    float y = 44;
    ui->enabled = p->AddCheck(G::RectF(UF(20), UF(y), UF(240), UF(18)), L"Использовать прокси", UF(10.5f), cfg.enabled, nullptr);
    y += 24;
    HLine(p, y, W, 20);
    y += 8;
    ui->lServer = Lbl(p, 20, y, 120, 16, L"Сервер:", 10.5f, 0xaaaaaa, gfx::LEFT, true);
    y += 20;
    ui->lAddr = Lbl(p, 20, y, 48, 24, L"Адрес:", 10.5f, 0xcccccc);
    ui->host = Edt(p, 68, y, 128, 24, 10.5f, false);
    ui->lPort = Lbl(p, 206, y, 36, 24, L"Порт:", 10.5f, 0xcccccc);
    ui->port = Edt(p, 242, y, 38, 24, 10.5f, false);
    p->SetText(ui->host, ::W(cfg.host)); p->SetText(ui->port, ::W(cfg.port));
    y += 32;
    HLine(p, y, W, 20);
    y += 8;
    ui->lProto = Lbl(p, 20, y, 120, 16, L"Протокол:", 10.5f, 0xaaaaaa, gfx::LEFT, true);
    y += 20;
    struct R { const wchar_t* t; const char* v; } rs[] = {{L"SOCKS Version 5", "socks5"}, {L"SOCKS Version 4", "socks4"}, {L"HTTP / HTTPS", "http"}};
    for (auto& r : rs) {
        Ctl* rb = p->Add(K::Radio, G::RectF(UF(20), UF(y), UF(240), UF(18)));
        rb->t1 = r.t; rb->px = UF(10.5f); rb->fg = 0xffffff; rb->checked = (ui->proto == r.v);
        ui->radios.push_back(rb);
        ui->vals.push_back(r.v);
        std::string v = r.v;
        rb->onClick = [ui, rb, v, p]() {
            for (auto* o : ui->radios) o->checked = false;
            rb->checked = true;
            ui->proto = v;
            p->InvalAll();
        };
        y += 20;
    }
    y += 4;
    HLine(p, y, W, 20);
    y += 8;
    ui->lAuth = Lbl(p, 20, y, 120, 16, L"Авторизация:", 10.5f, 0xaaaaaa, gfx::LEFT, true);
    y += 20;
    ui->authCb = p->AddCheck(G::RectF(UF(20), UF(y), UF(200), UF(18)), L"Включить", UF(10.5f),
                             !cfg.user.empty() || !cfg.password.empty(), nullptr);
    y += 24;
    ui->lUser = Lbl(p, 20, y, 90, 24, L"Пользователь:", 10.5f, 0xcccccc);
    ui->user = Edt(p, 110, y, 170, 24, 10.5f, false);
    p->SetText(ui->user, ::W(cfg.user));
    y += 28;
    ui->lPass = Lbl(p, 20, y, 90, 24, L"Пароль:", 10.5f, 0xcccccc);
    ui->pass = Edt(p, 110, y, 170, 24, 10.5f, false, nullptr, true);
    p->SetText(ui->pass, ::W(cfg.password));
    y += 36;
    (void)H;

    auto apply = [ui, p]() {
        bool on = ui->enabled->checked;
        bool authOn = ui->authCb->checked && on;
        for (Ctl* e : {ui->host, ui->port}) SetEnabled(p, e, on);
        SetEnabled(p, ui->user, authOn);
        SetEnabled(p, ui->pass, authOn);
        for (Ctl* l : {ui->lServer, ui->lProto, ui->lAuth}) l->fg = on ? 0xaaaaaa : 0x555555;
        for (Ctl* l : {ui->lAddr, ui->lPort, ui->lUser, ui->lPass}) l->fg = on ? 0xcccccc : 0x555555;
        for (Ctl* r : ui->radios) r->en = on;
        ui->authCb->en = on;
        p->InvalAll();
    };
    ui->enabled->onClick = [ui, apply]() { ui->enabled->checked = !ui->enabled->checked; apply(); };
    ui->authCb->onClick = [ui, apply]() { ui->authCb->checked = !ui->authCb->checked; apply(); };
    apply();

    // подогнать высоту под содержимое
    RECT r;
    GetWindowRect(p->hwnd, &r);
    SetWindowPos(p->hwnd, nullptr, 0, 0, r.right - r.left, (int)UF(y), SWP_NOMOVE | SWP_NOZORDER);
    CenterPopup(p);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Настройки скачивания (обрезка)
// ═════════════════════════════════════════════════════════════════════════════
void CloseDlSettings();

static std::wstring TcValue(const std::vector<Ctl*>& f, Panel* p) {
    bool any = false;
    std::wstring out;
    for (size_t i = 0; i < f.size(); i++) {
        std::wstring v = p->GetText(f[i]);
        while (v.size() < 2) v.insert(v.begin(), L'0');
        if (v != L"00") any = true;
        out += (i ? L":" : L"") + v;
    }
    return any ? out : L"";
}

void CloseDlSettings() {
    // значения сохраняются при каждом изменении, поэтому достаточно уничтожить окно
    if (A.dlSettings) {
        Panel* p = A.dlSettings;
        A.dlSettings = nullptr;
        Panel::Dispose(p);
    }
}

void ToggleDlSettings() {
    if (A.dlSettings) { CloseDlSettings(); return; }
    ShowDlSettings();
}

void ShowDlSettings() {
    CloseMenu();
    float W = 300, H = 178;
    Panel* p = NewPopup(W, H, L"Настройки скачивания", nullptr);
    A.dlSettings = p;
    Lbl(p, 20, 44, W - 40, 16, L"Обрезка видео:", 10.5f, 0xaaaaaa, gfx::LEFT, true);
    Lbl(p, 20, 62, W - 40, 15, L"Оставьте пустым чтобы скачать целиком.", 9.5f, 0x666666);

    auto start = std::make_shared<std::vector<Ctl*>>();
    auto end = std::make_shared<std::vector<Ctl*>>();
    auto sync = [start, end, p]() {
        A.trimStart = TcValue(*start, p);
        A.trimEnd = TcValue(*end, p);
    };
    auto makeTc = [&](float x, float y, std::shared_ptr<std::vector<Ctl*>> out, const std::wstring& init) {
        const wchar_t* cues[] = {L"ЧЧ", L"ММ", L"СС"};
        std::vector<std::wstring> parts;
        size_t pos = 0;
        while (pos <= init.size()) {
            size_t q = init.find(L':', pos);
            if (q == std::wstring::npos) q = init.size();
            parts.push_back(init.substr(pos, q - pos));
            pos = q + 1;
        }
        for (int i = 0; i < 3; i++) {
            Ctl* e = nullptr;
            e = Edt(p, x + i * 44, y, 38, 24, 10.5f, false, nullptr, false, cues[i]);
            out->push_back(e);
            std::wstring v = i < (int)parts.size() ? parts[i] : L"";
            size_t nz = v.find_first_not_of(L'0');
            v = nz == std::wstring::npos ? L"" : v.substr(nz);
            p->SetText(e, v);
            if (i < 2) Lbl(p, x + i * 44 + 38, y, 6, 24, L":", 11, 0xaaaaaa, gfx::CENTER);
            e->onChange = [e, i, out, p, sync]() {
                std::wstring v = p->GetText(e), d;
                for (wchar_t c : v) if (c >= L'0' && c <= L'9') d += c;
                if (d.size() > 2) d.resize(2);
                if (d != v) p->SetText(e, d);
                if (d.size() == 2 && i < 2) SetFocus((*out)[i + 1]->h);
                sync();
            };
        }
    };
    Lbl(p, 20, 88, 52, 24, L"Начало:", 10.5f, 0xaaaaaa);
    makeTc(74, 88, start, A.trimStart);
    Lbl(p, 20, 120, 52, 24, L"Конец:", 10.5f, 0xaaaaaa);
    makeTc(74, 120, end, A.trimEnd);

    Btn(p, 90, 150, 120, 20, L"Сбросить обрезку", 9.5f, [start, end, p]() {
        for (auto* e : *start) p->SetText(e, L"", true);
        for (auto* e : *end) p->SetText(e, L"", true);
        A.trimStart.clear();
        A.trimEnd.clear();
    })->bg = NOCOL;
    CenterPopup(p);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Планировщик
// ═════════════════════════════════════════════════════════════════════════════
struct Sched {
    std::wstring hh, mm, ss, date;
    std::vector<std::wstring> urls{L""};
    int fmt = 1, vc = 0, ac = 0, res = 3;
    bool running = false, cancelled = false, started = false, init = false;
    std::wstring status;
    uint32_t statusColor = 0xaaaaaa;
    time_t target = 0;
    int total = 0;
    std::shared_ptr<Proc> proc;
};
static Sched S;

struct SchedUI {
    Panel* p = nullptr;
    Panel* cal = nullptr;
    Ctl *h, *m, *s, *date, *status, *start;
    std::vector<Ctl*> urlEdits;
    Ctl *pFmt, *pVc, *pAc, *pRes;
    int calYear = 0, calMon = 0;
};
static SchedUI SU;

static const std::vector<std::wstring> kSchFmt = {L"Видео: MKV", L"Видео: MP4", L"Аудио: OPUS", L"Аудио: M4A", L"Аудио: MP3"};
static const char* kSchFmtExt[] = {"mkv", "mp4", "opus", "m4a", "mp3"};

static void CloseCalendar() {
    if (SU.cal) { Panel::Dispose(SU.cal); SU.cal = nullptr; }
}

static void SchedSetStatus(const std::wstring& t, uint32_t c) {
    S.status = t;
    S.statusColor = c;
    if (SU.p && SU.status) {
        SU.status->t1 = t;
        SU.status->fg = c;
        SU.p->Inval(SU.status);
    }
}

static void SchedSyncFromUI() {
    if (!SU.p) return;
    S.hh = SU.p->GetText(SU.h); S.mm = SU.p->GetText(SU.m); S.ss = SU.p->GetText(SU.s);
    S.date = SU.p->GetText(SU.date);
    S.urls.clear();
    for (auto* e : SU.urlEdits) S.urls.push_back(SU.p->GetText(e));
    if (S.urls.empty()) S.urls.push_back(L"");
    S.fmt = SU.pFmt->sel; S.vc = SU.pVc->sel; S.ac = SU.pAc->sel; S.res = SU.pRes->sel;
}

static void SchedButtonState() {
    if (!SU.p || !SU.start) return;
    Ctl* b = SU.start;
    if (S.running) {
        b->t1 = L"ОТМЕНА"; b->bg = 0x555555; b->bgH = 0x444444; b->bd = 0x555555; b->bdH = 0x444444;
    } else {
        b->t1 = L"ЗАПЛАНИРОВАТЬ"; b->bg = col::Red; b->bgH = col::RedHover; b->bd = col::Red; b->bdH = col::RedHover;
    }
    b->en = true;
    SU.p->Inval(b);
}

static std::vector<std::wstring> SchedArgs(const std::wstring& url) {
    std::string fmt = kSchFmtExt[S.fmt];
    static const std::vector<std::wstring> vcl = {L"AV1", L"VP9", L"H.264", L"-"};
    static const std::vector<std::wstring> acl = {L"OPUS", L"AAC", L"-"};
    static const std::vector<std::wstring> rel = {L"4320p", L"2160p", L"1440p", L"1080p", L"720p", L"480p", L"-"};
    std::wstring vcN = vcl[S.vc], acN = acl[S.ac], resN = rel[S.res];
    std::string vc = vcN == L"AV1" ? "av01" : vcN == L"VP9" ? "vp9" : vcN == L"H.264" ? "avc1" : "";
    std::string ac = acN == L"OPUS" ? "opus" : acN == L"AAC" ? "mp4a" : "";
    bool audio = (fmt == "opus" || fmt == "m4a" || fmt == "mp3");

    std::vector<std::wstring> a = {ResPath(L"yt-dlp.exe"), L"--newline", L"--no-colors", L"--encoding", L"utf-8"};
    std::wstring proxy = GetProxyUrl();
    if (!proxy.empty()) { a.push_back(L"--proxy"); a.push_back(proxy); }
    if (audio) {
        std::string acCond = ac.empty() ? "" : "[acodec^=" + ac + "]";
        a.push_back(L"-f"); a.push_back(W("bestaudio" + acCond + "/bestaudio"));
        a.push_back(L"-x"); a.push_back(L"--audio-format"); a.push_back(W(fmt));
        if (fmt == "mp3") { a.push_back(L"--audio-quality"); a.push_back(L"0"); }
    } else {
        std::vector<std::string> cond;
        if (resN != L"-") {
            std::string r = U8(resN);
            r.erase(std::remove(r.begin(), r.end(), 'p'), r.end());
            cond.push_back("height<=" + r);
        }
        if (!vc.empty()) cond.push_back("vcodec^=" + vc);
        std::string vstr;
        if (!cond.empty()) { vstr = "["; for (size_t i = 0; i < cond.size(); i++) { if (i) vstr += "]["; vstr += cond[i]; } vstr += "]"; }
        std::string acCond = ac.empty() ? "" : "[acodec^=" + ac + "]";
        a.push_back(L"-f"); a.push_back(W("bestvideo" + vstr + "+bestaudio" + acCond + "/bestvideo+bestaudio/best"));
        std::string sort;
        if (!vc.empty()) sort += "vcodec:" + vc;
        if (!ac.empty()) sort += std::string(sort.empty() ? "" : ",") + "acodec:" + ac;
        if (!sort.empty()) { a.push_back(L"-S"); a.push_back(W(sort)); }
        a.push_back(L"--merge-output-format"); a.push_back(W(fmt));
    }
    a.push_back(L"-o"); a.push_back(A.destFolder + L"\\%(title)s.%(ext)s");
    a.push_back(url);
    return a;
}

static bool ParseSchedTime(time_t& out) {
    int dd, mm, yy, h, mi, s;
    if (swscanf(S.date.c_str(), L"%d.%d.%d", &dd, &mm, &yy) != 3) return false;
    try { h = std::stoi(S.hh); mi = std::stoi(S.mm); s = std::stoi(S.ss); } catch (...) { return false; }
    if (mm < 1 || mm > 12 || dd < 1 || dd > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59 || yy < 1970) return false;
    tm t{};
    t.tm_year = yy - 1900; t.tm_mon = mm - 1; t.tm_mday = dd; t.tm_hour = h; t.tm_min = mi; t.tm_sec = s; t.tm_isdst = -1;
    time_t r = mktime(&t);
    if (r == (time_t)-1 || t.tm_mday != dd || t.tm_mon != mm - 1) return false;   // 31.02 и т.п.
    out = r;
    return true;
}

static void SchedFinishUI() {
    S.running = false;
    SchedButtonState();
}

void SchedTick() {
    if (!S.running || S.cancelled) { KillTimer(A.hwnd, T_SCHED); return; }
    long long remaining = (long long)difftime(S.target, time(nullptr));
    if (remaining > 0) {
        long long r = remaining;
        long long years = r / 31536000; r %= 31536000;
        long long months = r / 2592000; r %= 2592000;
        long long days = r / 86400; r %= 86400;
        long long hours = r / 3600; r %= 3600;
        long long mins = r / 60, secs = r % 60;
        std::wstring parts;
        auto add = [&](long long v, const wchar_t* u) { if (v) parts += std::to_wstring(v) + u + L" "; };
        add(years, L" г."); add(months, L" мес."); add(days, L" д."); add(hours, L" ч."); add(mins, L" мин.");
        parts += std::to_wstring(secs) + L" сек.";
        SchedSetStatus(L"Запуск через " + parts + L" (" + std::to_wstring(S.total) + L" ссылок)", col::Green);
        return;
    }
    KillTimer(A.hwnd, T_SCHED);
    S.started = true;
    SchedSetStatus(L"Скачивание " + std::to_wstring(S.total) + L" ссылок…", col::Amber);
    if (SU.p && SU.start) { SU.start->en = false; SU.p->Inval(SU.start); }

    std::vector<std::wstring> urls;
    for (auto& u : S.urls) { std::wstring t = Trim(u); if (!t.empty()) urls.push_back(t); }
    std::thread([urls]() {
        for (size_t i = 0; i < urls.size(); i++) {
            if (S.cancelled) break;
            std::wstring u = urls[i];
            std::wstring shortU = u.size() > 40 ? u.substr(0, 40) + L"…" : u;
            PostUI([i, n = urls.size(), shortU]() {
                SchedSetStatus(L"Скачивание " + std::to_wstring(i + 1) + L"/" + std::to_wstring(n) + L" — " + shortU, col::Amber);
            });
            auto proc = std::make_shared<Proc>();
            S.proc = proc;
            if (proc->Start(SchedArgs(u), false)) { proc->Wait(); proc->Close(); }
        }
        PostUI([n = urls.size()]() {
            if (!S.cancelled) {
                SchedSetStatus(L"Завершено: " + std::to_wstring(n) + L" загрузок!", col::Green);
                PlayKindSound("success");
                ShowToast(L"RedStream", L"Планировщик завершил " + std::to_wstring(n) + L" загрузок.");
            }
            S.proc.reset();
            SchedFinishUI();
        });
    }).detach();
}

static void SchedCancel() {
    S.cancelled = true;
    S.running = false;
    KillTimer(A.hwnd, T_SCHED);
    if (S.proc) S.proc->Kill();
    SchedSetStatus(L"Отменено", col::Err);
    SchedButtonState();
}

static void SchedRun() {
    SchedSyncFromUI();
    time_t target;
    if (!ParseSchedTime(target)) { SchedSetStatus(L"Неверный формат даты/времени!", col::Err); return; }
    int n = 0;
    for (auto& u : S.urls) if (!Trim(u).empty()) n++;
    if (!n) { SchedSetStatus(L"Добавьте хотя бы одну ссылку!", col::Err); return; }
    if (difftime(target, time(nullptr)) < 0) { SchedSetStatus(L"Указанное время уже прошло!", col::Err); return; }
    S.running = true; S.cancelled = false; S.started = false;
    S.target = target; S.total = n;
    SchedButtonState();
    SetTimer(A.hwnd, T_SCHED, 1000, nullptr);
    SchedTick();
}

bool SchedulerVisible() { return SU.p != nullptr; }

void HideScheduler() {
    if (!SU.p) return;
    SchedSyncFromUI();
    CloseCalendar();
    Panel* p = SU.p;
    SU.p = nullptr;
    SU.status = SU.start = nullptr;
    SU.urlEdits.clear();
    Panel::Dispose(p);
}

static const wchar_t* kMonthsRu[] = {L"", L"Январь", L"Февраль", L"Март", L"Апрель", L"Май", L"Июнь",
                                     L"Июль", L"Август", L"Сентябрь", L"Октябрь", L"Ноябрь", L"Декабрь"};

static void ShowCalendar();

static void ShowCalendar() {
    bool toggle = false;
    (void)toggle;
    Panel* old = SU.cal;
    SU.cal = nullptr;
    int px = -1, py = -1;
    if (old) {
        RECT r;
        GetWindowRect(old->hwnd, &r);
        px = r.left; py = r.top;
        Panel::Dispose(old);
    }
    int y = SU.calYear, mo = SU.calMon;
    int selD = 0, selM = 0, selY = 0;
    swscanf(SU.p->GetText(SU.date).c_str(), L"%d.%d.%d", &selD, &selM, &selY);
    time_t now = time(nullptr);
    tm tn;
    localtime_s(&tn, &now);

    float cell = 28, W = 7 * cell + 14, H = 6 + 22 + 4 + 18 + 6 * 24 + 30 + 10;
    auto* p = new Panel();
    p->Create(SU.p->hwnd, WS_POPUP, WS_EX_TOOLWINDOW, 0, 0, (int)UF(W), (int)UF(H));
    p->paintBg = [](G::Graphics& g, const RECT& cr) {
        G::SolidBrush bg(C(0x2a2a2a));
        g.FillRectangle(&bg, 0, 0, cr.right, cr.bottom);
        G::Pen pen(C(0x555555), 1.f);
        g.DrawRectangle(&pen, 0.5f, 0.5f, (float)cr.right - 1, (float)cr.bottom - 1);
    };
    SU.cal = p;

    auto nav = [](int dm) {
        SU.calMon += dm;
        if (SU.calMon < 1) { SU.calMon = 12; SU.calYear--; }
        if (SU.calMon > 12) { SU.calMon = 1; SU.calYear++; }
        PostUI([]() { if (SU.cal && SU.p) ShowCalendar(); });
    };
    Ctl* l = Btn(p, 6, 6, 24, 22, L"‹", 9.5f, [nav]() { nav(-1); });
    l->bg = NOCOL; l->bd = NOCOL;
    Ctl* t = Lbl(p, 30, 6, W - 60, 22, std::wstring(kMonthsRu[mo]) + L" " + std::to_wstring(y), 10.5f, 0xffffff, gfx::CENTER, true);
    (void)t;
    Ctl* r = Btn(p, W - 30, 6, 24, 22, L"›", 9.5f, [nav]() { nav(1); });
    r->bg = NOCOL; r->bd = NOCOL;
    const wchar_t* wd[] = {L"Пн", L"Вт", L"Ср", L"Чт", L"Пт", L"Сб", L"Вс"};
    for (int i = 0; i < 7; i++) Lbl(p, 7 + i * cell, 32, cell, 16, wd[i], 9.5f, 0x777777, gfx::CENTER);

    tm first{};
    first.tm_year = y - 1900; first.tm_mon = mo - 1; first.tm_mday = 1; first.tm_hour = 12;
    mktime(&first);
    int fw = (first.tm_wday + 6) % 7;                        // понедельник = 0
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = dim[mo - 1] + ((mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 1 : 0);
    int row = 0, colm = fw;
    for (int d = 1; d <= days; d++) {
        bool sel = (d == selD && mo == selM && y == selY);
        bool today = (d == tn.tm_mday && mo == tn.tm_mon + 1 && y == tn.tm_year + 1900);
        Ctl* b = p->AddButton(G::RectF(UF(7 + colm * cell + 1), UF(50 + row * 24), UF(cell - 2), UF(22)), std::to_wstring(d), nullptr);
        b->px = UF(10); b->rad = UF(3);
        b->bg = sel ? 0xcc0000 : today ? 0x444444 : 0x2a2a2a;
        b->bgH = sel ? 0xbf0000 : 0x555555;
        b->fg = 0xffffff; b->fgH = 0xffffff;
        b->onClick = [d, mo, y]() {
            SU.p->SetText(SU.date, Fmt(L"%02d.%02d.%04d", d, mo, y));
            CloseCalendar();
        };
        if (++colm > 6) { colm = 0; row++; }
    }
    int rows = row + (colm ? 1 : 0);
    wchar_t ts[32];
    swprintf(ts, 32, L"%02d.%02d.%04d", tn.tm_mday, tn.tm_mon + 1, tn.tm_year + 1900);
    std::wstring today = ts;
    Ctl* tb = Btn(p, 6, 50 + rows * 24 + 4, W - 12, 22, L"Сегодня: " + today, 9.5f, [today]() {
        SU.p->SetText(SU.date, today);
        CloseCalendar();
    });
    (void)tb;
    SetWindowPos(p->hwnd, nullptr, 0, 0, (int)UF(W), (int)UF(50 + rows * 24 + 4 + 22 + 8), SWP_NOMOVE | SWP_NOZORDER);

    if (px < 0) {
        RECT br = CtlScreenRect(SU.p, SU.date);
        px = br.left; py = br.bottom + 2;
    }
    SetWindowPos(p->hwnd, HWND_TOP, px, py, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

static void ToggleCalendar() {
    if (SU.cal) { CloseCalendar(); return; }
    int d, m, y;
    time_t now = time(nullptr);
    tm tn;
    localtime_s(&tn, &now);
    if (swscanf(SU.p->GetText(SU.date).c_str(), L"%d.%d.%d", &d, &m, &y) == 3 && m >= 1 && m <= 12 && y > 1900) {
        SU.calYear = y; SU.calMon = m;
    } else {
        SU.calYear = tn.tm_year + 1900; SU.calMon = tn.tm_mon + 1;
    }
    ShowCalendar();
}

static void BuildScheduler() {
    float W = 340;
    int nUrls = (int)S.urls.size();
    float urlsH = std::min(nUrls, 5) * 32.f + 8;
    float H = 44 + 18 + 32 + 10 + 8 + 18 + urlsH + 28 + 8 + 8 + 18 + 2 * 50 + 8 + 18 + 18 + 34 + 20;
    Panel* p = NewPopup(W, H, L"Планировщик загрузок", []() { HideScheduler(); });
    SU.p = p;
    SU.urlEdits.clear();

    float y = 44;
    Lbl(p, 20, y, W - 40, 16, L"Начать скачивание в:", 10.5f, 0xaaaaaa, gfx::LEFT, true);
    y += 22;
    auto digits = [p](Ctl* e, int mx) {
        e->onChange = [p, e, mx]() {
            std::wstring v = p->GetText(e), d;
            for (wchar_t c : v) if (c >= L'0' && c <= L'9') d += c;
            if (d.size() > (size_t)mx) d.resize(mx);
            if (d != v) p->SetText(e, d);
        };
    };
    SU.h = Edt(p, 20, y, 36, 26, 10.5f, false);
    Lbl(p, 56, y, 8, 26, L":", 12, 0xaaaaaa, gfx::CENTER, true);
    SU.m = Edt(p, 64, y, 36, 26, 10.5f, false);
    Lbl(p, 100, y, 8, 26, L":", 12, 0xaaaaaa, gfx::CENTER, true);
    SU.s = Edt(p, 108, y, 36, 26, 10.5f, false);
    for (Ctl* e : {SU.h, SU.m, SU.s}) { digits(e, 2); SendMessageW(e->h, EM_SETLIMITTEXT, 2, 0); e->r.X += 0; }
    SU.date = Edt(p, 160, y, 96, 26, 10.5f, false);
    digits(SU.date, 10);
    SendMessageW(SU.date->h, EM_SETLIMITTEXT, 10, 0);
    Ctl* calBtn = Btn(p, 262, y, 30, 26, L"", 10, []() { ToggleCalendar(); });
    calBtn->icon = gfx::I_CALENDAR; calBtn->iconPx = UF(14);
    if (!S.init) {
        time_t now = time(nullptr);
        tm tn;
        localtime_s(&tn, &now);
        S.hh = Fmt(L"%02d", tn.tm_hour); S.mm = Fmt(L"%02d", tn.tm_min); S.ss = Fmt(L"%02d", tn.tm_sec);
        S.date = Fmt(L"%02d.%02d.%04d", tn.tm_mday, tn.tm_mon + 1, tn.tm_year + 1900);
        S.init = true;
    }
    p->SetText(SU.h, S.hh); p->SetText(SU.m, S.mm); p->SetText(SU.s, S.ss); p->SetText(SU.date, S.date);
    y += 36;
    HLine(p, y, W, 20);
    y += 8;
    Lbl(p, 20, y, W - 40, 16, L"Ссылки для скачивания:", 10.5f, 0xaaaaaa, gfx::LEFT, true);
    y += 20;
    for (int i = 0; i < nUrls; i++) {
        Ctl* e = Edt(p, 20, y, W - 40 - 30, 26, 10, true, nullptr, false, L"Вставьте ссылку…");
        p->SetText(e, S.urls[i]);
        SU.urlEdits.push_back(e);
        int idx = i;
        Ctl* x = Btn(p, W - 20 - 26, y, 26, 26, L"", 10, [idx]() {
            SchedSyncFromUI();
            if (S.urls.size() > 1) S.urls.erase(S.urls.begin() + idx); else S.urls[0].clear();
            PostUI([]() { HideScheduler(); ShowScheduler(); });
        });
        x->icon = gfx::I_CLOSE; x->iconPx = UF(9);
        y += 32;
    }
    Ctl* add = Btn(p, 20, y, W - 40, 24, L"+  Добавить ссылку", 10, []() {
        SchedSyncFromUI();
        if (S.urls.size() >= 12) return;
        S.urls.push_back(L"");
        PostUI([]() { HideScheduler(); ShowScheduler(); });
    });
    add->bg = NOCOL;
    y += 32;
    HLine(p, y, W, 20);
    y += 8;
    Lbl(p, 20, y, W - 40, 16, L"Параметры загрузки:", 10.5f, 0xaaaaaa, gfx::LEFT, true);
    y += 22;
    float cw2 = (W - 40 - 8) / 2;
    auto mk = [&](float x, float yy, const wchar_t* lab, const std::vector<std::wstring>& items, int sel) {
        Ctl* c = p->AddPill(G::RectF(UF(x), UF(yy), UF(cw2), UF(24)), lab, nullptr);
        c->px = UF(10);
        c->items = items; c->sel = sel; c->t2 = items[sel];
        c->onClick = [p, c]() { PillDropdown(p, c, c->items, c->sel, [c, p](int) {
            if (c == SU.pFmt) {
                bool audio = SU.pFmt->sel >= 2;
                for (Ctl* d : {SU.pVc, SU.pRes}) SetEnabled(p, d, !audio);
            }
        }); };
        return c;
    };
    SU.pFmt = mk(20, y, L"Формат", kSchFmt, S.fmt);
    SU.pVc  = mk(20 + cw2 + 8, y, L"Видеокодек", {L"AV1", L"VP9", L"H.264", L"-"}, S.vc);
    y += 30;
    SU.pAc  = mk(20, y, L"Аудиокодек", {L"OPUS", L"AAC", L"-"}, S.ac);
    SU.pRes = mk(20 + cw2 + 8, y, L"Разрешение", {L"4320p", L"2160p", L"1440p", L"1080p", L"720p", L"480p", L"-"}, S.res);
    if (S.fmt >= 2) for (Ctl* d : {SU.pVc, SU.pRes}) { d->en = false; }
    y += 34;
    HLine(p, y, W, 20);
    y += 8;
    SU.status = Lbl(p, 20, y, W - 40, 16, S.status, 10, S.statusColor, gfx::CENTER);
    y += 22;
    SU.start = RedBtn(p, 20, y, W - 40, 34, L"ЗАПЛАНИРОВАТЬ", 12, []() {
        if (S.running) SchedCancel(); else SchedRun();
    });
    SchedButtonState();
    if (S.running && S.started) { SU.start->en = false; }
    y += 34 + 18;

    RECT r;
    GetWindowRect(p->hwnd, &r);
    SetWindowPos(p->hwnd, nullptr, 0, 0, r.right - r.left, (int)UF(y), SWP_NOMOVE | SWP_NOZORDER);
    (void)H;
    CenterPopup(p);
}

void ShowScheduler() {
    CloseMenu();
    if (SU.p) { SetWindowPos(SU.p->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); return; }
    BuildScheduler();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Обновление компонентов
// ═════════════════════════════════════════════════════════════════════════════
struct Upd {
    bool running = false, cancel = false, pulsing = false;
    int pulse = 0;
    std::shared_ptr<Proc> proc;
    Ctl *bFfmpeg = nullptr, *bYtdlp = nullptr, *bar = nullptr, *label = nullptr, *close = nullptr;
};
static Upd UP;

bool UpdaterBusy() { return UP.running; }

void UpdaterPulse() {
    if (!UP.running || !UP.pulsing) { KillTimer(A.hwnd, T_PULSE); return; }
    if (!A.updaterPopup) return;
    UP.pulse++;
    UP.bar->val = (UP.pulse % 20) / 20.f;
    A.updaterPopup->Inval(UP.bar);
}

static void UpdReset() {
    Panel* p = A.updaterPopup;
    UP.running = false; UP.cancel = false; UP.pulsing = false; UP.proc.reset();
    KillTimer(A.hwnd, T_PULSE);
    if (!p) return;
    for (Ctl* b : {UP.bFfmpeg, UP.bYtdlp}) {
        b->en = true; b->bg = col::Red; b->bgH = col::RedHover; b->bd = col::Red; b->bdH = col::RedHover; b->fg = 0xffffff;
    }
    UP.bFfmpeg->t1 = L"Обновить ffmpeg";
    UP.bYtdlp->t1 = L"Обновить yt-dlp";
    UP.close->en = true;
    p->InvalAll();
}

static void UpdFinish(const std::wstring& text, bool ok) {
    if (!A.updaterPopup) { UP.running = false; return; }
    UP.bar->barA = UP.bar->barB = ok ? col::Green : col::Red;
    UP.bar->val = 1.f;
    UP.label->t1 = text;
    UpdReset();
    PlayKindSound(ok ? "success" : "error");
}

static void UpdSetBusy(Ctl* active, const wchar_t* label) {
    UP.running = true; UP.cancel = false;
    Ctl* other = active == UP.bFfmpeg ? UP.bYtdlp : UP.bFfmpeg;
    active->t1 = L"Отмена";
    active->bg = 0x555555; active->bgH = 0x666666; active->bd = 0x555555; active->bdH = 0x666666;
    other->en = false;
    UP.close->en = false;
    UP.bar->barA = UP.bar->barB = col::Amber;
    UP.bar->val = 0;
    UP.label->t1 = label;
    A.updaterPopup->InvalAll();
}

static void UpdCancel() {
    UP.cancel = true;
    if (UP.proc) UP.proc->Kill();
    UP.label->t1 = L"Отмена…";
    A.updaterPopup->Inval(UP.label);
}

static void UpdateFfmpeg() {
    UpdSetBusy(UP.bFfmpeg, L"Подключение…");
    UP.bFfmpeg->onClick = []() { UpdCancel(); };
    std::thread([]() {
        const std::wstring url = URL_FFMPEG;
        std::wstring base = ResPath(L"");
        std::wstring out = base + L"\\ffmpeg-git-essentials.7z";
        if (!FileExists(base + L"\\7za.exe")) {
            PostUI([]() { UpdFinish(L"Не найден 7za.exe — он нужен для распаковки", false); });
            return;
        }
        bool ok = HttpDownload(url, out, [](uint64_t done, uint64_t total) {
            if (UP.cancel) return false;
            float pct = total ? (float)((double)done / total) : 0.f;
            wchar_t b[128];
            if (total) swprintf(b, 128, L"Скачивание: %.1f%%  (%.1f МБ из %.1f МБ)", pct * 100, done / 1048576.0, total / 1048576.0);
            else swprintf(b, 128, L"Скачивание: %.1f МБ…", done / 1048576.0);
            std::wstring t = b;
            std::replace(t.begin(), t.end(), L'.', L',');
            PostUI([pct, t]() { if (A.updaterPopup) { UP.bar->val = pct; UP.label->t1 = t; A.updaterPopup->InvalAll(); } });
            return true;
        }, 60000);
        if (UP.cancel || !ok) {
            DeleteFileW(out.c_str());
            bool c = UP.cancel;
            PostUI([c]() { UpdFinish(c ? L"Отменено." : L"Ошибка", false); });
            return;
        }
        PostUI([]() { if (A.updaterPopup) { UP.label->t1 = L"Распаковка и установка ffmpeg…"; A.updaterPopup->InvalAll(); } });
        std::wstring err;
        bool inst = InstallFfmpegFromArchive(base, out, err);
        DeleteFileW(out.c_str());
        PostUI([inst, err]() { UpdFinish(inst ? L"Успешно завершено!" : L"Ошибка: " + err, inst); });
    }).detach();
}

static void UpdateYtdlp() {
    UpdSetBusy(UP.bYtdlp, L"Проверка обновлений…");
    UP.bYtdlp->onClick = []() { UpdCancel(); };
    UP.pulsing = true;
    UP.pulse = 0;
    SetTimer(A.hwnd, T_PULSE, 80, nullptr);
    auto proc = std::make_shared<Proc>();
    UP.proc = proc;
    std::thread([proc]() {
        if (!proc->Start({ResPath(L"yt-dlp.exe"), L"-U"}, true, ResPath(L""))) {
            PostUI([]() { UpdFinish(L"Ошибка: не найден yt-dlp.exe", false); });
            return;
        }
        std::string out;
        char buf[4096];
        DWORD n;
        while ((n = proc->Read(buf, sizeof buf)) > 0) out.append(buf, n);
        int code = proc->Wait();
        proc->Close();
        if (UP.cancel) { PostUI([]() { UpdFinish(L"Отменено.", false); }); return; }
        std::string low = out;
        for (auto& c : low) c = (char)tolower((unsigned char)c);
        bool upToDate = low.find("up to date") != std::string::npos;
        PostUI([upToDate, code]() {
            if (upToDate) UpdFinish(L"У вас установлена последняя версия.", true);
            else if (code == 0) UpdFinish(L"Успешно обновлено!", true);
            else UpdFinish(L"Ошибка при обновлении.", false);
        });
    }).detach();
}

void ShowUpdater() {
    CloseMenu();
    if (A.updaterPopup) { SetWindowPos(A.updaterPopup->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); return; }
    float W = 280, H = 250;
    Panel* p = NewPopup(W, H, L"Обновление компонентов", nullptr);
    A.updaterPopup = p;
    UP = Upd{};
    for (auto& c : p->ctls) if (c->icon == gfx::I_CLOSE) {
        UP.close = c.get();
        c->onClick = [p]() {
            if (UP.running) return;
            A.updaterPopup = nullptr;
            Panel::Dispose(p);
        };
    }
    Ctl* t = Lbl(p, 20, 46, W - 40, 36, L"Перед обновлением убедитесь,\nчто нет активных загрузок.", 10.5f, 0xcccccc, gfx::CENTER);
    t->wrap = true;
    UP.bFfmpeg = RedBtn(p, 20, 92, W - 40, 32, L"Обновить ffmpeg", 11, nullptr);
    UP.bYtdlp  = RedBtn(p, 20, 132, W - 40, 32, L"Обновить yt-dlp", 11, nullptr);
    UP.bFfmpeg->onClick = []() { if (!UP.running) UpdateFfmpeg(); else UpdCancel(); };
    UP.bYtdlp->onClick  = []() { if (!UP.running) UpdateYtdlp(); else UpdCancel(); };
    HLine(p, 174, W, 20);
    UP.bar = p->Add(K::Progress, G::RectF(UF(20), UF(186), UF(W - 40), UF(14)));
    UP.bar->rad = UF(5); UP.bar->barA = UP.bar->barB = col::Dim;
    UP.label = Lbl(p, 20, 206, W - 40, 18, L"Ожидание…", 10.5f, 0xffffff, gfx::CENTER);
    CenterPopup(p);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Установка недостающих компонентов: 7za → yt-dlp → ffmpeg
// ═════════════════════════════════════════════════════════════════════════════
struct Comp { const wchar_t* file; const wchar_t* title; const wchar_t* url; };
static const Comp kComps[3] = {
    {L"7za.exe",    L"7za.exe — распаковщик архивов",       URL_7ZA},
    {L"yt-dlp.exe", L"yt-dlp.exe — загрузчик видео",         URL_YTDLP},
    {L"ffmpeg.exe", L"ffmpeg.exe — обработка видео и аудио", URL_FFMPEG},
};

bool MissingComponents() {
    for (auto& c : kComps) if (!FileExists(ResPath(c.file))) return true;
    return false;
}

struct Inst {
    bool running = false;
    std::atomic<bool> cancel{false};
    int gen = 0;
    bool need[3] = {false, false, false};
    std::wstring st[3];
    uint32_t stCol[3] = {0, 0, 0};
    Ctl *bar = nullptr, *label = nullptr, *go = nullptr, *later = nullptr, *close = nullptr;
};
static Inst Ins;

static void InstInitStates() {
    for (int i = 0; i < 3; i++) {
        Ins.need[i] = !FileExists(ResPath(kComps[i].file));
        Ins.st[i]    = Ins.need[i] ? L"нужно скачать" : L"установлен";
        Ins.stCol[i] = Ins.need[i] ? col::Amber : col::Green;
    }
}

static void InstClosePopup() {
    if (!A.installerPopup) return;
    Panel* p = A.installerPopup;
    A.installerPopup = nullptr;
    Ins.gen++;                     // устаревшие PostUI от рабочего потока игнорируются
    Panel::Dispose(p);
}

static void InstSetButtons(const wchar_t* goText, bool goEnabled, bool goVisible, const wchar_t* laterText) {
    Ins.go->t1 = goText;
    Ins.go->en = goEnabled;
    Ins.go->vis = goVisible;
    Ins.later->t1 = laterText;
    A.installerPopup->InvalAll();
}

static bool LooksLikeExe(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char mz[2] = {0, 0};
    size_t n = fread(mz, 1, 2, f);
    fclose(f);
    return n == 2 && mz[0] == 'M' && mz[1] == 'Z';
}

static void InstRun() {
    if (Ins.running || !A.installerPopup) return;
    InstInitStates();
    bool need[3] = {Ins.need[0], Ins.need[1], Ins.need[2]};
    if (!need[0] && !need[1] && !need[2]) return;
    Ins.running = true;
    Ins.cancel = false;
    int gen = ++Ins.gen;
    for (int i = 0; i < 3; i++) if (need[i]) { Ins.st[i] = L"в очереди"; Ins.stCol[i] = 0x999999; }
    Ins.bar->barA = Ins.bar->barB = col::Amber;
    Ins.bar->val = 0;
    Ins.label->t1 = L"Подключение…";
    Ins.label->fg = 0xffffff;
    Ins.close->en = false;
    InstSetButtons(L"Скачивание…", false, true, L"Отмена");

    std::thread([gen, need]() {
        std::wstring base = ResPath(L"");
        auto alive = [gen]() { return A.installerPopup && gen == Ins.gen; };
        auto setState = [&](int i, const std::wstring& t, uint32_t c) {
            PostUI([=]() { if (!alive()) return; Ins.st[i] = t; Ins.stCol[i] = c; A.installerPopup->InvalAll(); });
        };
        auto setProg = [&](float v, const std::wstring& t) {
            PostUI([=]() { if (!alive()) return; Ins.bar->val = v; Ins.label->t1 = t; A.installerPopup->InvalAll(); });
        };

        for (int i = 0; i < 3 && !Ins.cancel; i++) {
            if (!need[i]) continue;
            const Comp& c = kComps[i];
            bool isArchive = (i == 2);
            std::wstring name = c.file;
            std::wstring dst = base + L"\\" + name;
            std::wstring tmp = isArchive ? base + L"\\ffmpeg-git-essentials.7z" : dst + L".part";
            setState(i, L"скачивание…", col::Amber);
            setProg(0, L"Скачивание " + name + L"…");

            DWORD last = 0;
            bool ok = HttpDownload(c.url, tmp, [&](uint64_t done, uint64_t total) {
                if (Ins.cancel) return false;
                DWORD now = GetTickCount();
                if (now - last < 120 && done != total) return true;
                last = now;
                float pct = total ? (float)((double)done / (double)total) : (float)((done / 65536) % 20) / 20.f;
                wchar_t b[160];
                if (total) swprintf(b, 160, L"Скачивание %ls: %.1f МБ из %.1f МБ", name.c_str(), done / 1048576.0, total / 1048576.0);
                else swprintf(b, 160, L"Скачивание %ls: %.1f МБ…", name.c_str(), done / 1048576.0);
                std::wstring t = b;
                std::replace(t.begin(), t.end(), L'.', L',');
                wchar_t s[16];
                swprintf(s, 16, total ? L"%d%%" : L"…", (int)(pct * 100));
                setProg(pct, t);
                setState(i, s, col::Amber);
                return true;
            }, 60000);

            if (Ins.cancel) { DeleteFileW(tmp.c_str()); setState(i, L"отменено", 0x999999); break; }

            // защита от HTML-заглушек и обрывов
            if (ok) {
                uint64_t sz = 0;
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesExW(tmp.c_str(), GetFileExInfoStandard, &fad)) sz = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
                uint64_t minSize = i == 0 ? 100 * 1024 : 1024 * 1024;
                if (sz < minSize || (!isArchive && !LooksLikeExe(tmp))) ok = false;
            }
            if (!ok) {
                DeleteFileW(tmp.c_str());
                setState(i, L"ошибка загрузки", col::Err);
                continue;
            }

            if (isArchive) {
                setState(i, L"распаковка…", col::Amber);
                setProg(1.f, L"Распаковка и установка ffmpeg…");
                std::wstring err;
                bool inst = InstallFfmpegFromArchive(base, tmp, err);
                DeleteFileW(tmp.c_str());
                if (!inst) { setState(i, L"ошибка: " + err, col::Err); continue; }
            } else {
                if (!MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                    DeleteFileW(tmp.c_str());
                    setState(i, L"не удалось сохранить файл", col::Err);
                    continue;
                }
            }
            setState(i, L"установлен", col::Green);
        }

        PostUI([gen]() {
            if (!A.installerPopup || gen != Ins.gen) { Ins.running = false; return; }
            Ins.running = false;
            bool cancelled = Ins.cancel;
            bool allOk = !MissingComponents();
            Ins.close->en = true;
            if (allOk) {
                Ins.bar->barA = Ins.bar->barB = col::Green; Ins.bar->val = 1.f;
                Ins.label->t1 = L"Готово! Все компоненты установлены."; Ins.label->fg = col::Green;
                InstSetButtons(L"", false, false, L"Закрыть");
                PlayKindSound("success");
            } else if (cancelled) {
                Ins.bar->barA = Ins.bar->barB = col::Amber;
                Ins.label->t1 = L"Отменено."; Ins.label->fg = 0xffffff;
                InstSetButtons(L"Скачать", true, true, L"Закрыть");
            } else {
                Ins.bar->barA = Ins.bar->barB = col::Red; Ins.bar->val = 1.f;
                Ins.label->t1 = L"Не всё удалось скачать. Проверьте интернет и повторите."; Ins.label->fg = col::Err;
                InstSetButtons(L"Повторить", true, true, L"Закрыть");
                PlayKindSound("error");
            }
        });
    }).detach();
}

void ShowInstaller() {
    if (A.installerPopup) { SetWindowPos(A.installerPopup->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); return; }
    CloseMenu();
    InstInitStates();
    Ins.running = false;
    Ins.cancel = false;
    float W = 340, H = 300;
    Panel* p = NewPopup(W, H, L"Установка компонентов", nullptr);
    A.installerPopup = p;
    for (auto& c : p->ctls) if (c->icon == gfx::I_CLOSE) {
        Ins.close = c.get();
        c->onClick = []() { if (!Ins.running) InstClosePopup(); };
    }

    Ctl* t = Lbl(p, 20, 46, W - 40, 34, L"Для работы программы нужны файлы, которых нет рядом с RedStream.exe. Скачать их сейчас?", 10.5f, 0xcccccc, gfx::CENTER);
    t->wrap = true;

    // три строки: название слева, состояние справа
    float y = 92;
    for (int i = 0; i < 3; i++) {
        Ctl* r = p->Add(K::Custom, G::RectF(UF(20), UF(y), UF(W - 40), UF(24)));
        int idx = i;
        r->draw = [idx](G::Graphics& g, Ctl& c) {
            gfx::FillRR(g, c.r, UF(5), C(0x252525));
            gfx::StrokeRR(g, c.r, UF(5), C(0x333333), std::max(1.f, UF(1)));
            gfx::Text(g, kComps[idx].title, G::RectF(c.r.X + UF(9), c.r.Y, c.r.Width * 0.62f, c.r.Height), gfx::GetFont(UF(9.5f)),
                      C(Ins.need[idx] || Ins.running ? 0xdddddd : 0x888888), gfx::LEFT, true);
            gfx::Text(g, Ins.st[idx], G::RectF(c.r.X + c.r.Width * 0.60f, c.r.Y, c.r.Width * 0.40f - UF(9), c.r.Height),
                      gfx::GetFont(UF(9.5f), true), C(Ins.stCol[idx]), gfx::RIGHT, true);
        };
        y += 30;
    }
    HLine(p, y + 2, W, 20);
    y += 14;
    Ins.bar = p->Add(K::Progress, G::RectF(UF(20), UF(y), UF(W - 40), UF(12)));
    Ins.bar->rad = UF(5); Ins.bar->barA = Ins.bar->barB = col::Dim;
    y += 18;
    Ins.label = Lbl(p, 20, y, W - 40, 18, L"Ожидание…", 10, 0x999999, gfx::CENTER);
    y += 28;
    Ins.go = RedBtn(p, 20, y, (W - 40 - 8) / 2, 34, L"Скачать", 11.5f, []() { InstRun(); });
    Ins.later = Btn(p, 20 + (W - 40 - 8) / 2 + 8, y, (W - 40 - 8) / 2, 34, L"Позже", 11.5f, []() {
        if (Ins.running) { Ins.cancel = true; Ins.label->t1 = L"Отмена…"; A.installerPopup->Inval(Ins.label); }
        else InstClosePopup();
    });
    y += 34 + 18;

    RECT r;
    GetWindowRect(p->hwnd, &r);
    SetWindowPos(p->hwnd, nullptr, 0, 0, r.right - r.left, (int)UF(y), SWP_NOMOVE | SWP_NOZORDER);
    (void)H;
    CenterPopup(p);
}
