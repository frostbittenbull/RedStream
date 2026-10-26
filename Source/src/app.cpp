#include "app.h"
#include <set>
#include <shlobj.h>
#include <oleidl.h>
#include <objidl.h>

App A;

enum { T_URL = 1, T_SPIN, T_SCHED, T_TOAST, T_PULSE, T_UPD, T_BANNER = 100, T_BALLOON };
static unsigned long long g_dlStartFt = 0;    // момент старта текущей загрузки (для очистки .part при отмене)

// ═════════════════════════════════════════════════════════════════════════════
//  Маппинги (из main.py)
// ═════════════════════════════════════════════════════════════════════════════
struct FmtDef { const wchar_t* full; const wchar_t* ext; const wchar_t* shortName; };
static const FmtDef kFormats[] = {
    {L"Видео: MKV", L"mkv", L"MKV"},
    {L"Видео: MP4", L"mp4", L"MP4"},
    {L"Аудио: OPUS", L"opus", L"OPUS"},
    {L"Аудио: M4A (с аудиокодеком AAC)", L"m4a", L"M4A"},
    {L"Аудио: MP3 (конвертация с аудиокодека OPUS)", L"mp3", L"MP3"},
};

static const std::map<std::string, std::string> VC_DISP = {
    {"av01", "AV1"}, {"vp9", "VP9"}, {"vp09", "VP9"}, {"avc1", "H.264"}, {"h264", "H.264"}, {"H264", "H.264"},
    {"hvc1", "H.265"}, {"hev1", "H.265"}, {"h265", "H.265"}, {"H265", "H.265"}, {"bytevc1", "H.265"}};
static const std::map<std::string, std::string> VC_RAW = {
    {"AV1", "av01"}, {"VP9", "vp9"}, {"H.264", "avc1"}, {"H.265", "hvc1"}};
static const std::map<std::string, std::string> AC_DISP = {{"opus", "OPUS"}, {"mp4a", "AAC"}};
static const std::map<std::string, std::string> AC_RAW  = {{"OPUS", "opus"}, {"AAC", "mp4a"}};

static std::string MapGet(const std::map<std::string, std::string>& m, const std::string& k, const std::string& d) {
    auto it = m.find(k);
    return it == m.end() ? d : it->second;
}
static std::string UpperA(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
static std::string LowerA(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static const wchar_t* kBrowsers[] = {L"Без авторизации", L"Chrome", L"Chromium", L"Opera", L"Opera GX",
                                     L"Edge", L"Firefox", L"Brave", L"Vivaldi", L"Whale", L"Файл cookies.txt"};
static const wchar_t* kCookiesItem = L"Файл cookies.txt";

// ── JSON-помощники (терпимы к null/пропущенным полям) ─────────────────────────
static std::string js(const json& f, const char* k, const char* d = "") {
    auto it = f.find(k);
    if (it == f.end() || !it->is_string()) return d;
    return it->get<std::string>();
}
static double jn(const json& f, const char* k, double d = 0) {
    auto it = f.find(k);
    if (it == f.end() || !it->is_number()) return d;
    return it->get<double>();
}
static double Weight(const json& f) {
    for (const char* k : {"tbr", "vbr", "abr", "filesize", "filesize_approx"}) {
        double v = jn(f, k);
        if (v != 0) return v;
    }
    return 0;
}
// Размер, который сообщил сам сайт (без оценки по битрейту) — по нему yt-dlp сравнивает форматы
static double KnownSize(const json& f) {
    double v = jn(f, "filesize");
    return v != 0 ? v : jn(f, "filesize_approx");
}

static double SizeOf(const json& f) {
    double v = jn(f, "filesize");
    if (v == 0) v = jn(f, "filesize_approx");
    if (v == 0 && A.prev.duration > 0) {                 // размера нет (например, HLS) — оцениваем по битрейту
        double br = jn(f, "tbr");
        if (br == 0) br = jn(f, "vbr") + jn(f, "abr");
        if (br > 0) v = br * 1000.0 / 8.0 * A.prev.duration;
    }
    return v;
}

// Названия кодека в формате yt-dlp для фильтра vcodec^= (у одного кодека их может быть несколько)
std::vector<std::string> VcodecAliases(const std::string& raw) {
    static const std::map<std::string, std::vector<std::string>> ALIAS = {
        {"avc1", {"avc1", "h264", "h.264"}}, {"hvc1", {"hvc1", "hev1", "h265", "h.265", "bytevc1"}},
        {"vp9", {"vp9", "vp09"}}, {"av01", {"av01"}}};
    auto it = ALIAS.find(raw);
    return it != ALIAS.end() ? it->second : std::vector<std::string>{raw};
}

// ═════════════════════════════════════════════════════════════════════════════
//  Общие вспомогательные
// ═════════════════════════════════════════════════════════════════════════════
RECT CtlScreenRect(Panel* p, const Ctl* c) {
    POINT tl{(LONG)c->r.X, (LONG)c->r.Y}, br{(LONG)(c->r.X + c->r.Width), (LONG)(c->r.Y + c->r.Height)};
    ClientToScreen(p->hwnd, &tl);
    ClientToScreen(p->hwnd, &br);
    return RECT{tl.x, tl.y, br.x, br.y};
}

void CenterPopup(Panel* pop, int dy) {
    RECT mr, pc;
    GetWindowRect(A.hwnd, &mr);
    GetWindowRect(pop->hwnd, &pc);
    int w = pc.right - pc.left, h = pc.bottom - pc.top;
    int x = mr.left + ((mr.right - mr.left) - w) / 2;
    int y = mr.top + ((mr.bottom - mr.top) - h) / 2 + dy;
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(A.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    x = std::max<int>(mi.rcWork.left, std::min<int>(x, mi.rcWork.right - w));
    y = std::max<int>(mi.rcWork.top, std::min<int>(y, mi.rcWork.bottom - h));
    SetWindowPos(pop->hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

static std::wstring PV(const Ctl* c) {
    return (c->sel >= 0 && c->sel < (int)c->items.size()) ? c->items[c->sel] : L"";
}
static std::string PVa(const Ctl* c) { return U8(PV(c)); }

static void RefreshPill(Ctl* c) {
    std::wstring v = PV(c);
    if (c == A.pFormat) {
        for (auto& f : kFormats) if (v == f.full) v = f.shortName;
    }
    c->t2 = v.empty() ? L"—" : v;
}

static void SetPill(Ctl* c, const std::vector<std::wstring>& items, int sel = 0) {
    c->items = items;
    c->sel = items.empty() ? 0 : std::max(0, std::min(sel, (int)items.size() - 1));
    RefreshPill(c);
}

static void SetPillState(Ctl* c, bool enabled) {
    c->en = enabled;
    A.main->Inval(c);
}

std::wstring FormatExt() {
    std::wstring v = PV(A.pFormat);
    for (auto& f : kFormats) if (v == f.full) return f.ext;
    return L"mp4";
}
bool IsAudioExt(const std::wstring& e) { return e == L"mp3" || e == L"m4a" || e == L"opus"; }

void PillDropdown(Panel* panel, Ctl* c, const std::vector<std::wstring>& items, int sel,
                  std::function<void(int)> onPick, bool) {
    if (!c->en) return;
    RECT r = CtlScreenRect(panel, c);
    ListStyle st;
    st.px = UF(9.5f);
    st.bg = 0x262626; st.bd = 0x555555; st.hoverBg = 0x3a3a3a; st.fg = 0xdddddd; st.selFg = col::RedSoft;
    CloseMenu();
    ShowList(panel->hwnd, r, items, sel, st, [c, panel, onPick](int i) {
        c->sel = i;
        RefreshPill(c);
        panel->Inval(c);
        if (onPick) onPick(i);
    });
}

// ═════════════════════════════════════════════════════════════════════════════
//  Спиннер на ⚙
// ═════════════════════════════════════════════════════════════════════════════
static void StartSpinner() {
    A.spinning = true;
    A.spinPhase = 0;
    A.gear->spin = 0;
    SetTimer(A.hwnd, T_SPIN, 100, nullptr);
    A.main->Inval(A.gear);
}
static void StopSpinner() {
    A.spinning = false;
    KillTimer(A.hwnd, T_SPIN);
    A.gear->spin = -1;
    A.main->Inval(A.gear);
}
void SetGearEnabled(bool en) {
    A.gear->en = en;
    A.main->Inval(A.gear);
    if (!en) CloseDlSettings();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Расчёт размера (порт _calc_filesize)
// ═════════════════════════════════════════════════════════════════════════════
static void SetSizeText(const std::wstring& s) {
    A.sizeText = s;
    A.main->Inval(A.prevCard);
    A.main->InvalAll();
}

static void CalcFilesize() {
    if (!A.hasFormats) { SetSizeText(L""); A.lblWarn->t1 = L""; A.main->InvalAll(); return; }
    std::string selExt = U8(FormatExt());
    std::string vcDisp = PVa(A.pVc), acDisp = PVa(A.pAc), selRes = PVa(A.pRes), selFps = PVa(A.pFps);
    std::string selVc = vcDisp != "-" ? MapGet(VC_RAW, vcDisp, LowerA(vcDisp)) : "-";
    std::string selAc = acDisp != "-" ? MapGet(AC_RAW, acDisp, LowerA(acDisp)) : "-";
    bool audioOnly = (selExt == "mp3" || selExt == "m4a" || selExt == "opus");

    double vSize = 0, aSize = 0;
    const json* bestV = nullptr;
    const json* bestA = nullptr;

    std::wstring av1Warn;           // предупреждение о подмене кодека / частоты кадров
    if (!audioOnly) {
        std::vector<std::string> variants = VcodecAliases(selVc);
        bool codecSel = (selVc != "-" && !selVc.empty());
        int resH = 0, wantFps = 0;
        {
            std::smatch m;
            if (selRes != "-" && !selRes.empty()) {
                if (std::regex_search(selRes, m, std::regex("x(\\d+)"))) resH = std::stoi(m[1]);
                else if (std::regex_search(selRes, m, std::regex("(\\d+)"))) resH = std::stoi(m[1]);
            }
            if (!(selFps == "-" || selFps.empty())) { try { wantFps = std::stoi(selFps); } catch (...) {} }
        }
        // как «bestvideo» в yt-dlp: только видео без звука; совмещённые форматы — лишь если других нет
        bool anyVideoOnly = false;
        for (auto& f : A.rawFormats) {
            std::string vc = js(f, "vcodec", "none");
            if (vc != "none" && vc != "images" && js(f, "acodec", "none") == "none") { anyVideoOnly = true; break; }
        }
        auto isVideo = [&](const json& f) {
            std::string vc = js(f, "vcodec", "none");
            return vc != "none" && vc != "images" && (!anyVideoOnly || js(f, "acodec", "none") == "none");
        };
        auto codecMatch = [&](const json& f) {
            std::string lvc = LowerA(js(f, "vcodec", "none"));
            for (auto& a : variants) if (lvc.find(a) != std::string::npos) return true;
            return false;
        };
        auto codecRank = [](const json& f) {            // как в yt-dlp: av01 > vp9 > h265 > h264
            std::string v = LowerA(js(f, "vcodec", ""));
            if (v.rfind("av01", 0) == 0) return 0;
            if (v.rfind("vp", 0) == 0) return 1;
            if (v.rfind("hvc", 0) == 0 || v.rfind("hev", 0) == 0) return 2;
            if (v.rfind("avc", 0) == 0) return 3;
            return 4;
        };
        auto worse = [&](const json* a, const json* b) {  // true, если a хуже b
            double ha = jn(*a, "height", 0), hb = jn(*b, "height", 0);
            if (ha != hb) return ha < hb;
            double fa = jn(*a, "fps", 0), fb = jn(*b, "fps", 0);
            if (fa != fb) return fa < fb;
            int ca = codecRank(*a), cb = codecRank(*b);
            if (ca != cb) return ca > cb;
            double sa = KnownSize(*a), sb = KnownSize(*b);    // как в yt-dlp: при равенстве выигрывает формат с известным большим размером
            if (sa != sb) return sa < sb;
            return Weight(*a) < Weight(*b);
        };
        // уровни — те же, что в цепочке «-f» при скачивании: точное совпадение -> без кодека -> без частоты кадров -> без ограничений
        // resMode: 0 — без ограничения, 1 — ровно выбранная высота, 2 — не выше выбранной
        auto pick = [&](bool needCodec, bool needFps, int resMode) -> const json* {
            const json* best = nullptr;
            for (auto& f : A.rawFormats) {
                if (!isVideo(f)) continue;
                if (needCodec && !codecMatch(f)) continue;
                if (resMode && resH > 0) {
                    double h = jn(f, "height", 0);
                    if (resMode == 1 ? (h != resH) : !(h > 0 && h <= resH)) continue;
                }
                if (needFps && wantFps > 0) {
                    auto it = f.find("fps");
                    if (it == f.end() || !it->is_number() || it->get<double>() > wantFps) continue;
                }
                if (!best || worse(best, &f)) best = &f;
            }
            return best;
        };
        // Разрешение важнее частоты кадров и кодека: сначала ищем выбранную высоту, и только если её нет — ниже.
        // Тот же порядок, что в цепочке «-f» при скачивании.
        if (codecSel) { bestV = pick(true, true, 1); if (!bestV) bestV = pick(true, false, 1); }
        if (!bestV) bestV = pick(false, true, 1);
        if (!bestV) bestV = pick(false, false, 1);
        if (!bestV) bestV = pick(false, true, 2);
        if (!bestV) bestV = pick(false, false, 2);
        if (!bestV) bestV = pick(false, false, 0);
        if (bestV) {
            vSize = SizeOf(*bestV);
            bool codecDiff = codecSel && !codecMatch(*bestV);
            int gotFps = (int)jn(*bestV, "fps", 0);
            bool fpsDiff = wantFps > 0 && gotFps > 0 && gotFps != wantFps;
            if (codecDiff || fpsDiff) {
                bool codecAnywhere = false;
                for (auto& f : A.rawFormats) if (isVideo(f) && codecMatch(f)) codecAnywhere = true;
                std::string actual = js(*bestV, "vcodec");
                actual = actual.substr(0, actual.find('.'));
                std::wstring actualName = W(MapGet(VC_DISP, actual, UpperA(actual)));
                std::wstring where = codecAnywhere ? L" в этом разрешении" : L"";
                if (codecDiff && fpsDiff)
                    av1Warn = W(vcDisp) + L" и " + std::to_wstring(wantFps) + L" к/с недоступны" + where + L", будет " + actualName + L", " + std::to_wstring(gotFps) + L" к/с";
                else if (codecDiff)
                    av1Warn = W(vcDisp) + L" недоступен" + where + (codecAnywhere ? L", будет скачан " : L", вместо этого будет скачан ") + actualName;
                else
                    av1Warn = std::to_wstring(wantFps) + L" к/с недоступны в этом разрешении, будет " + std::to_wstring(gotFps) + L" к/с";
            }
        }
    }

    // для аудиоформатов кодек определяется самим форматом: OPUS и MP3 берут поток opus, M4A — поток AAC (mp4a)
    std::string wantA = audioOnly ? (selExt == "m4a" ? "mp4a" : "opus") : selAc;
    std::vector<const json*> validA, anyA;
    for (auto& f : A.rawFormats) {
        std::string ac = js(f, "acodec", "none"), vc = js(f, "vcodec", "none");
        if (vc != "none" || ac == "none") continue;
        anyA.push_back(&f);
        if (ac.find(wantA) != std::string::npos) validA.push_back(&f);
    }
    auto bestOf = [](const std::vector<const json*>& v) {
        return *std::max_element(v.begin(), v.end(), [](const json* a, const json* b) {
            double ka = KnownSize(*a), kb = KnownSize(*b);
            if (ka != kb) return ka < kb;
            return Weight(*a) < Weight(*b);
        });
    };
    if (!validA.empty()) bestA = bestOf(validA);
    else if (!anyA.empty()) bestA = bestOf(anyA);          // нужного кодека нет — yt-dlp возьмёт лучший звук и сконвертирует
    if (bestA) aSize = SizeOf(*bestA);

    double total = 0;
    if (audioOnly) total = aSize;
    else if (vSize <= 0) total = 0;                      // размер видео неизвестен — лучше ничего не показывать, чем размер одного звука
    else if (bestV && js(*bestV, "acodec", "none") != "none") total = vSize;
    else total = vSize + aSize;

    if (total > 0) {
        double mb = total / (1024.0 * 1024.0);
        wchar_t b[64];
        swprintf(b, 64, L"~%.2f МБ", mb);
        std::wstring s = b;
        std::replace(s.begin(), s.end(), L'.', L',');
        SetSizeText(s);
    } else {
        SetSizeText(L"");
    }

    std::wstring audioWarn;
    if (audioOnly) {
        if (validA.empty() && bestA) {
            // нужного потока нет — yt-dlp возьмёт другой звук и сконвертирует его
            std::string real = js(*bestA, "acodec", "");
            real = real.substr(0, real.find('.'));
            std::wstring want = W(MapGet(AC_DISP, wantA, UpperA(wantA))), got = W(MapGet(AC_DISP, real, UpperA(real)));
            audioWarn = want + L" недоступен, вместо этого будет использован " + got;
        } else if (selExt == "mp3") {
            audioWarn = L"В процессе конвертации размер файла может измениться";     // MP3 получается конвертацией из OPUS
        }
    }
    A.lblWarn->t1 = av1Warn.empty() ? audioWarn : av1Warn;
    A.main->InvalAll();
}

static void ApplyTrackPills();

static void OnFormatChange() {
    std::wstring ext = FormatExt();
    bool audio = IsAudioExt(ext);
    if (audio) {
        for (Ctl* c : {A.pVc, A.pRes, A.pFps}) { SetPill(c, {L""}); SetPillState(c, false); }
        std::string raw = ext == L"m4a" ? "mp4a" : "opus";
        SetPill(A.pAc, {W(MapGet(AC_DISP, raw, UpperA(raw)))});
        SetPillState(A.pAc, false);
    } else if (A.hasFormats) {
        SetPill(A.pVc, A.savedVcs);   SetPillState(A.pVc, true);
        SetPill(A.pAc, A.savedAcs);   SetPillState(A.pAc, true);
        SetPill(A.pRes, A.savedRess); SetPillState(A.pRes, true);
        SetPill(A.pFps, A.savedFpss); SetPillState(A.pFps, true);
    } else {
        for (Ctl* c : {A.pVc, A.pAc, A.pRes, A.pFps}) SetPillState(c, false);
    }
    ApplyTrackPills();
    CalcFilesize();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Предпросмотр (порт _fetch_preview)
// ═════════════════════════════════════════════════════════════════════════════
static int ResKey(const std::string& r) {
    std::smatch m;
    if (std::regex_search(r, m, std::regex("x(\\d+)"))) return std::stoi(m[1]);
    if (std::regex_search(r, m, std::regex("\\d+"))) return std::stoi(m[0]);
    return 0;
}

static std::wstring FixThumbUrl(std::wstring u) {
    // GDI+ не умеет webp: у YouTube есть тот же кадр в jpg
    size_t p = u.find(L"/vi_webp/");
    if (p != std::wstring::npos) u.replace(p, 9, L"/vi/");
    if (EndsWith(Lower(u), L".webp")) u = u.substr(0, u.size() - 5) + L".jpg";
    return u;
}

static std::wstring LangName(const std::string& code) {
    std::string base = code, region;
    size_t d = code.find_first_of("-_");
    if (d != std::string::npos) { base = code.substr(0, d); region = code.substr(d + 1); }
    static const std::map<std::string, const wchar_t*> N = {
        {"en", L"Английский"}, {"ru", L"Русский"}, {"uk", L"Украинский"}, {"be", L"Белорусский"}, {"de", L"Немецкий"},
        {"fr", L"Французский"}, {"es", L"Испанский"}, {"it", L"Итальянский"}, {"pt", L"Португальский"}, {"pl", L"Польский"},
        {"cs", L"Чешский"}, {"sk", L"Словацкий"}, {"nl", L"Нидерландский"}, {"sv", L"Шведский"}, {"da", L"Датский"},
        {"no", L"Норвежский"}, {"nb", L"Норвежский"}, {"fi", L"Финский"}, {"tr", L"Турецкий"}, {"ar", L"Арабский"},
        {"hi", L"Хинди"}, {"ja", L"Японский"}, {"ko", L"Корейский"}, {"zh", L"Китайский"}, {"id", L"Индонезийский"},
        {"vi", L"Вьетнамский"}, {"th", L"Тайский"}, {"fa", L"Персидский"}, {"he", L"Иврит"}, {"iw", L"Иврит"},
        {"el", L"Греческий"}, {"ro", L"Румынский"}, {"hu", L"Венгерский"}, {"bg", L"Болгарский"}, {"sr", L"Сербский"},
        {"hr", L"Хорватский"}, {"lt", L"Литовский"}, {"lv", L"Латышский"}, {"et", L"Эстонский"}, {"ka", L"Грузинский"},
        {"hy", L"Армянский"}, {"kk", L"Казахский"}, {"az", L"Азербайджанский"}, {"uz", L"Узбекский"}, {"tk", L"Туркменский"},
        {"ms", L"Малайский"}, {"bn", L"Бенгальский"}, {"ta", L"Тамильский"}, {"ur", L"Урду"}, {"fil", L"Филиппинский"}};
    auto it = N.find(LowerA(base));
    std::wstring n = it != N.end() ? std::wstring(it->second) : W(UpperA(base));
    if (!region.empty()) n += L" (" + W(region) + L")";
    return n;
}

// Аудиодорожки и субтитры доступны только когда есть из чего выбирать
static void ApplyTrackPills() {
    if (!A.pAudioTrack || !A.pSubs) return;
    bool haveFmts = A.hasFormats;
    // аудиодорожка: «Авто» + языки; активна, если дорожек больше одной
    {
        std::vector<std::wstring> items = {L"Авто"};
        for (auto& n : A.audioNames) items.push_back(n);
        bool en = haveFmts && A.audioCodes.size() >= 2;
        int sel = en ? std::min(A.pAudioTrack->sel, (int)items.size() - 1) : 0;
        if (!en) items = {L"—"};
        SetPill(A.pAudioTrack, items, sel);
        SetPillState(A.pAudioTrack, en);
    }
    // субтитры: «Нет» + языки; не нужны для аудиоформатов
    {
        bool audioFmt = IsAudioExt(FormatExt());
        std::vector<std::wstring> items = {L"Нет"};
        for (auto& n : A.subNames) items.push_back(n);
        bool en = haveFmts && !audioFmt && !A.subCodes.empty();
        int sel = en ? std::min(A.pSubs->sel, (int)items.size() - 1) : 0;
        if (!en) items = {L"—"};
        SetPill(A.pSubs, items, sel);
        SetPillState(A.pSubs, en);
    }
}

static void RunDownload();
static bool g_autoDownload = false;          // «Скачать снова» из истории: стартуем сразу после предпросмотра

static void AutoDownloadIfRequested() {
    if (!g_autoDownload) return;
    g_autoDownload = false;
    if (!A.dlProc) RunDownload();
}

static void ApplyPreviewFailure() {
    StopSpinner();
    SetGearEnabled(false);
    AutoDownloadIfRequested();
}

static void FetchPreview(const std::wstring& url) {
    int gen = ++A.prevGen;
    A.prev = Preview{};
    A.thumb.reset();
    A.audioCodes.clear(); A.audioNames.clear(); A.subCodes.clear(); A.subNames.clear(); A.subAuto.clear();
    ApplyTrackPills();
    SetGearEnabled(false);
    StartSpinner();
    A.main->Inval(A.prevCard);

    std::wstring cookiesFile;
    if (PV(A.pBrowser) == kCookiesItem) {
        std::wstring cf = CfgGet("cookies_file");
        if (!cf.empty() && FileExists(cf)) cookiesFile = cf;
    }
    std::thread([url, gen, cookiesFile]() {
        std::vector<std::wstring> args = {ResPath(L"yt-dlp.exe"), L"--dump-json", L"--no-playlist"};
        std::wstring proxy = GetProxyUrl();
        if (!proxy.empty()) { args.push_back(L"--proxy"); args.push_back(proxy); }
        if (!cookiesFile.empty()) { args.push_back(L"--cookies"); args.push_back(cookiesFile); }
        args.push_back(url);
        ProcResult r = RunCapture(args, 15000);
        auto fail = [gen]() { PostUI([gen]() { if (gen == A.prevGen) ApplyPreviewFailure(); }); };
        if (!r.started || r.code != 0) { fail(); return; }

        json data;
        try {
            // stdout может содержать лишние строки (предупреждения) — берём первую строку с '{'
            size_t b = r.out.find('{');
            data = json::parse(b == std::string::npos ? r.out : r.out.substr(b), nullptr, true, false);
        } catch (...) { fail(); return; }
        if (!data.is_object()) { fail(); return; }

        Preview pv;
        pv.valid = true;
        pv.title = W(js(data, "title"));
        pv.duration = data.contains("duration") && data["duration"].is_number() ? data["duration"].get<double>() : -1;
        pv.thumbUrl = W(js(data, "thumbnail"));
        std::string ex = js(data, "extractor_key");
        if (ex.empty()) ex = js(data, "extractor");
        pv.extractor = W(ex);

        json formats = data.contains("formats") && data["formats"].is_array() ? data["formats"] : json::array();

        std::set<std::string> exts, vcodecs, acodecs, resolutions;
        std::set<int> fpsSet;
        int bestH = 0, bestFps = 0;
        for (auto& f : formats) {
            if (js(f, "vcodec") == "images") continue;
            if (js(f, "vcodec") == "none" && js(f, "acodec") == "none") continue;
            std::string e = js(f, "ext");
            if (!e.empty()) exts.insert(e);
            std::string vc = js(f, "vcodec");
            if (!vc.empty() && vc != "none") vcodecs.insert(vc.substr(0, vc.find('.')));
            std::string ac = js(f, "acodec");
            if (!ac.empty() && ac != "none") acodecs.insert(ac.substr(0, ac.find('.')));
            std::string res = js(f, "resolution");
            double h = jn(f, "height"), w = jn(f, "width");
            if (h > 0) {
                resolutions.insert(w > 0 ? std::to_string((int)w) + "x" + std::to_string((int)h) : std::to_string((int)h) + "p");
                if ((int)h > bestH) { bestH = (int)h; bestFps = (int)jn(f, "fps"); }
            } else if (!res.empty() && res != "audio only" && res != "x") {
                resolutions.insert(res);
            }
            double fps = jn(f, "fps");
            if (fps != 0) fpsSet.insert((int)fps);
        }
        if (bestH > 0) pv.bestRes = W(std::to_string(bestH) + "p" + (bestFps > 30 ? std::to_string(bestFps) : ""));

        std::vector<std::string> vRaw(vcodecs.begin(), vcodecs.end()), aRaw(acodecs.begin(), acodecs.end());
        auto order = [](const std::map<std::string, int>& m) {
            return [m](const std::string& a, const std::string& b) {
                auto ia = m.find(a), ib = m.find(b);
                int va = ia == m.end() ? 999 : ia->second, vb = ib == m.end() ? 999 : ib->second;
                return va < vb;
            };
        };
        std::stable_sort(vRaw.begin(), vRaw.end(), order({{"av01", 0}, {"vp9", 1}, {"vp09", 1}, {"avc1", 2}}));
        std::stable_sort(aRaw.begin(), aRaw.end(), order({{"opus", 0}, {"mp4a", 1}}));

        std::vector<std::wstring> vcs, acs, ress, fpss;
        for (auto& v : vRaw) {                          // «vp9» и «vp09» — один и тот же VP9, в списке он один раз
            std::wstring d = W(MapGet(VC_DISP, v, UpperA(v)));
            if (std::find(vcs.begin(), vcs.end(), d) == vcs.end()) vcs.push_back(d);
        }
        if (vcs.empty()) vcs.push_back(L"-");
        if (std::find(vcs.begin(), vcs.end(), L"AV1") == vcs.end()) vcs.insert(vcs.begin(), L"AV1");
        for (auto& a : aRaw) acs.push_back(W(MapGet(AC_DISP, a, UpperA(a))));
        if (acs.empty()) acs.push_back(L"-");
        std::vector<std::string> rr(resolutions.begin(), resolutions.end());
        std::stable_sort(rr.begin(), rr.end(), [](const std::string& a, const std::string& b) { return ResKey(a) > ResKey(b); });
        for (auto& s : rr) ress.push_back(W(s));
        if (ress.empty()) ress.push_back(L"-");
        for (auto it = fpsSet.rbegin(); it != fpsSet.rend(); ++it) fpss.push_back(std::to_wstring(*it));
        if (fpss.empty()) fpss.push_back(L"-");

        // аудиодорожки (разные языки у аудиоформатов)
        std::vector<std::string> aCodes;
        std::vector<std::wstring> aNames;
        {
            std::set<std::string> seen;
            for (auto& f : formats) {
                if (js(f, "acodec", "none") == "none") continue;
                std::string lang = js(f, "language");
                if (lang.empty() || !seen.insert(lang).second) continue;
                aCodes.push_back(lang);
                aNames.push_back(LangName(lang));
            }
        }
        // субтитры: обычные, затем автосубтитры на языке оригинала («xx-orig»)
        std::vector<std::string> sCodes;
        std::vector<std::wstring> sNames;
        std::vector<char> sAuto;
        auto addSubs = [&](const char* key, bool autoCaps) {
            if (!data.contains(key) || !data[key].is_object()) return;
            struct E { int pri; std::wstring name; std::string code; };
            std::vector<E> tmp;
            for (auto it = data[key].begin(); it != data[key].end(); ++it) {
                std::string k = it.key();
                if (k == "live_chat" || !it.value().is_array() || it.value().empty()) continue;
                std::string base = k;
                if (autoCaps) {
                    if (k.size() <= 5 || k.compare(k.size() - 5, 5, "-orig") != 0) continue;
                    base = k.substr(0, k.size() - 5);
                }
                std::string b2 = LowerA(base.substr(0, base.find_first_of("-_")));
                tmp.push_back({b2 == "ru" ? 0 : b2 == "en" ? 1 : 2, LangName(base) + (autoCaps ? L" (авто)" : L""), k});
            }
            std::stable_sort(tmp.begin(), tmp.end(), [](const E& a, const E& b) { return a.pri != b.pri ? a.pri < b.pri : a.name < b.name; });
            for (auto& e : tmp) { sCodes.push_back(e.code); sNames.push_back(e.name); sAuto.push_back(autoCaps ? 1 : 0); }
        };
        addSubs("subtitles", false);
        addSubs("automatic_captions", true);
        if (sCodes.size() > 80) { sCodes.resize(80); sNames.resize(80); sAuto.resize(80); }

        // миниатюра
        std::unique_ptr<G::Bitmap> bmp;
        if (!pv.thumbUrl.empty()) {
            std::string raw;
            if (HttpGet(FixThumbUrl(pv.thumbUrl), raw, 8000)) bmp = gfx::LoadBitmapMem(raw.data(), raw.size());
        }
        auto* bmpRaw = bmp.release();

        PostUI([gen, pv, formats, vcs, acs, ress, fpss, bmpRaw, aCodes, aNames, sCodes, sNames, sAuto]() mutable {
            if (gen != A.prevGen) { delete bmpRaw; return; }
            A.prev = pv;
            A.audioCodes = aCodes; A.audioNames = aNames;
            A.subCodes = sCodes; A.subNames = sNames; A.subAuto = sAuto;
            A.rawFormats = formats;
            A.hasFormats = !formats.empty();
            A.thumb.reset(bmpRaw);
            A.savedVcs = vcs; A.savedAcs = acs; A.savedRess = ress; A.savedFpss = fpss;
            OnFormatChange();
            StopSpinner();
            SetGearEnabled(true);
            A.main->Inval(A.prevCard);
            AutoDownloadIfRequested();
        });
    }).detach();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Поле URL
// ═════════════════════════════════════════════════════════════════════════════
static void ProcessUrl(bool force) {
    std::wstring url = Trim(A.main->GetText(A.urlEdit));
    if (url.empty()) return;
    if (!StartsWith(url, L"http://") && !StartsWith(url, L"https://")) {
        if (url.find(L'.') == std::wstring::npos) return;
        url = L"https://" + url;
        A.main->SetText(A.urlEdit, url, true);
        SendMessageW(A.urlEdit->h, EM_SETSEL, (WPARAM)url.size(), (LPARAM)url.size());
    }
    if (!StartsWith(url, L"http")) return;
    if (!force && url == A.prevUrl) return;
    A.prevUrl = url;
    g_autoDownload = false;
    FetchPreview(url);
}

// Подставляет ссылку в поле (старая заменяется) и сразу запрашивает предпросмотр
static void SetUrlText(std::wstring text) {
    text = Trim(text);
    if (text.empty()) return;
    if (!StartsWith(text, L"http://") && !StartsWith(text, L"https://")) text = L"https://" + text;
    g_autoDownload = false;
    A.main->SetText(A.urlEdit, text, true);
    SetFocus(A.urlEdit->h);
    SendMessageW(A.urlEdit->h, EM_SETSEL, (WPARAM)text.size(), (LPARAM)text.size());
    KillTimer(A.hwnd, T_URL);
    A.prevUrl = text;
    FetchPreview(text);
}

void UseHistoryUrl(const std::wstring& url, bool download) {
    SetUrlText(url);
    if (download) {
        if (A.dlProc) ReportError(L"Ссылка подставлена. Дождитесь окончания текущей загрузки и нажмите «СКАЧАТЬ».");
        else g_autoDownload = true;
    }
}

static void PasteUrl() {
    std::wstring text;
    if (OpenClipboard(A.hwnd)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* p = (const wchar_t*)GlobalLock(h)) { text = p; GlobalUnlock(h); }
        }
        CloseClipboard();
    }
    SetUrlText(text);
}

void ClearFields() {
    g_autoDownload = false;
    A.prevGen++;
    A.main->SetText(A.urlEdit, L"", true);
    A.prevUrl.clear();
    A.prev = Preview{};
    A.thumb.reset();
    A.rawFormats = json::array();
    A.hasFormats = false;
    A.audioCodes.clear(); A.audioNames.clear(); A.subCodes.clear(); A.subNames.clear(); A.subAuto.clear();
    StopSpinner();
    SetGearEnabled(false);
    for (Ctl* c : {A.pVc, A.pAc, A.pRes, A.pFps}) { SetPill(c, {L""}); SetPillState(c, false); }
    SetSizeText(L"");
    A.lblWarn->t1 = L"";
    A.bar->val = 0;
    A.bar->barA = col::Dim; A.bar->barB = col::Dim;
    A.lblLeft->t1 = L"Вставьте ссылку на видео или плейлист, и нажмите «СКАЧАТЬ».";
    A.lblLeft->fg = 0x777777;
    A.lblRight->t1 = L"";
    A.btnOpen->en = false;
    OnFormatChange();
    A.main->InvalAll();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Загрузка (порт _run_download / _check_progress)
// ═════════════════════════════════════════════════════════════════════════════
static void RestoreDlButton() {
    A.downloading = false;
    A.btnDl->t1 = L"СКАЧАТЬ";
    A.btnDl->icon = gfx::I_DOWNLOAD;
    A.btnDl->bg = col::Red; A.btnDl->bgH = col::RedHover; A.btnDl->bd = col::Red; A.btnDl->bdH = col::RedHover;
    A.btnDl->fg = 0xffffff; A.btnDl->fgH = 0xffffff; A.btnDl->bold = true;
    A.main->Inval(A.btnDl);
}
static void SetStopButton() {
    A.downloading = true;
    A.btnDl->t1 = L"СТОП";
    A.btnDl->icon = gfx::I_STOP;
    A.btnDl->bg = col::GrayBg; A.btnDl->bgH = col::GrayHover; A.btnDl->bd = col::GrayBorder; A.btnDl->bdH = 0x555555;
    A.btnDl->fg = col::GrayFg; A.btnDl->fgH = 0xdddddd; A.btnDl->bold = false;
    A.main->Inval(A.btnDl);
}

static void SetStatus(const std::wstring& left, uint32_t color, const std::wstring& right = L"") {
    A.lblLeft->t1 = left;
    A.lblLeft->fg = color;
    A.lblRight->t1 = right;
    A.main->Inval(A.lblLeft);
    A.main->Inval(A.lblRight);
}

static void SetBar(float v, uint32_t a, uint32_t b) {
    A.bar->val = v; A.bar->barA = a; A.bar->barB = b;
    A.main->Inval(A.bar);
}

static void CancelDownload() {
    if (A.dlProc) A.dlProc->Kill();
    CleanupPartialFiles(A.destFolder, g_dlStartFt, false);     // .part и прочие хвосты не засоряют папку
    A.dlProc.reset();
    A.dlGen++;
    RestoreDlButton();
    SetBar(A.bar->val, col::Amber, col::Amber);
    SetStatus(L"Загрузка отменена.", 0xdddddd);
    PlayKindSound("error");
}

static std::wstring GroupNum(double v, int dec) {
    wchar_t b[64];
    swprintf(b, 64, L"%.*f", dec, v);
    std::wstring s = b, intp = s, frac;
    size_t dot = s.find(L'.');
    if (dot != std::wstring::npos) { intp = s.substr(0, dot); frac = s.substr(dot + 1); }
    std::wstring out;
    int cnt = 0;
    for (int i = (int)intp.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), intp[i]);
        if (++cnt % 3 == 0 && i > 0 && intp[i - 1] != L'-') out.insert(out.begin(), L'\u00a0');
    }
    return frac.empty() ? out : out + L"," + frac;
}

struct DlLineState {
    int  lastKind = 0;        // 1 = прогресс, 2 = финальная обработка, 3 = ошибка
    bool videoStream = true;
    int  destCount = 0;       // сколько файлов уже начато для текущего ролика (видео, затем звук)
    bool subtitle = false;    // сейчас скачивается файл субтитров
    std::map<std::string, bool> fmtHasVideo;   // format_id -> есть видеодорожка
    std::string title;
};

static bool g_selfUpdating = false;           // идёт самообновление программы

// «ETA 08:03» / «ETA 1:02:03» -> «8 мин 3 сек» / «1 ч 2 мин»
static std::wstring FmtEta(const std::string& s) {
    std::vector<long> parts;
    std::string cur;
    for (char c : s + ":") {
        if (c == ':') { if (!cur.empty()) parts.push_back(std::atol(cur.c_str())); cur.clear(); }
        else if (isdigit((unsigned char)c)) cur += c;
    }
    if (parts.empty() || parts.size() > 3) return L"";
    long sec = 0;
    for (long p : parts) sec = sec * 60 + p;
    long h = sec / 3600, m = (sec % 3600) / 60, r = sec % 60;
    wchar_t b[64];
    if (h > 0)      swprintf(b, 64, L"%ld ч %ld мин", h, m);
    else if (m > 0) swprintf(b, 64, L"%ld мин %ld сек", m, r);
    else            swprintf(b, 64, L"%ld сек", r);
    return b;
}

static void HandleDlLine(const std::string& line, const std::wstring& ext, bool audioFmt, int gen,
                         DlLineState& st) {
    if (line.find("ERROR:") != std::string::npos) {
        st.lastKind = 3;
        PostUI([gen]() {
            if (gen != A.dlGen) return;
            SetBar(A.bar->val, col::Red, col::Red);
            SetStatus(L"Ошибка! Проверьте ссылку или закройте браузер.", 0xffffff);
        });
        return;
    }
    if (line.find("Downloading item") != std::string::npos) st.destCount = 0;    // следующий ролик плейлиста
    if (line.find("[download] Destination:") != std::string::npos) {
        std::string lower = LowerA(line);
        size_t p;
        while ((p = lower.find(".part")) != std::string::npos) lower.erase(p, 5);
        auto ends = [&](const char* e) { return lower.size() >= strlen(e) && lower.compare(lower.size() - strlen(e), strlen(e), e) == 0; };
        st.subtitle = false;
        for (const char* e : {".vtt", ".srt", ".ass", ".ttml", ".srv1", ".srv2", ".srv3", ".json3", ".lrc"}) if (ends(e)) { st.subtitle = true; break; }
        if (!audioFmt && !st.subtitle) {
            st.destCount++;
            bool resolved = false;
            // 1) по идентификатору формата в имени временного файла: «Название.f251.webm»
            std::string clean = line;
            while (!clean.empty() && (clean.back() == '\r' || clean.back() == ' ')) clean.pop_back();
            {
                std::smatch fm;
                static const std::regex rf("\\.f([A-Za-z0-9_-]+)\\.[A-Za-z0-9]+(?:\\.part)?$");
                if (std::regex_search(clean, fm, rf)) {
                    auto it = st.fmtHasVideo.find(fm[1].str());
                    if (it != st.fmtHasVideo.end()) { st.videoStream = it->second; resolved = true; }
                }
            }
            // 2) по расширению: m4a/opus/... — точно звук; для webm/mp4 второй файл при слиянии — звук
            if (!resolved) {
                bool audioExt = false;
                for (const char* e : {".opus", ".m4a", ".mp3", ".aac", ".ogg", ".wav"}) if (ends(e)) { audioExt = true; break; }
                st.videoStream = !audioExt && st.destCount < 2;
            }
        }
        std::smatch m;
        std::string l2 = line;
        while (!l2.empty() && (l2.back() == '\r' || l2.back() == ' ')) l2.pop_back();
        if (!st.subtitle && std::regex_search(l2, m, std::regex("\\[download\\] Destination: .+?[/\\\\](.+?)(\\.\\w+)?(\\.\\w+)?$")))
            st.title = m[1];
    }
    if (line.find("[download]") != std::string::npos && line.find('%') != std::string::npos) {
        st.lastKind = 1;
        std::smatch m;
        static const std::regex re("\\[download\\]\\s+([\\d.]+)%\\s+of\\s+([\\d.]+)(\\w+)");
        static const std::regex rs("at\\s+([\\d.]+)(\\w+/s)");
        if (std::regex_search(line, m, re)) {
            double pct = std::atof(m[1].str().c_str()) / 100.0;
            double totalVal = std::atof(m[2].str().c_str());
            std::string unit = UpperA(m[3].str());
            static const std::map<std::string, double> toMb = {{"KIB", 1.0 / 1024}, {"MIB", 1}, {"GIB", 1024},
                                                               {"KB", 1.0 / 1000}, {"MB", 1}, {"GB", 1000}};
            auto uit = toMb.find(unit);
            double totalMb = totalVal * (uit == toMb.end() ? 1 : uit->second);
            double doneMb = totalMb * pct;
            std::wstring size = L"(" + GroupNum(doneMb, 2) + L" МБ из " + GroupNum(totalMb, 2) + L" МБ)";
            std::wstring speed;
            std::smatch sm;
            if (std::regex_search(line, sm, rs)) {
                double sv = std::atof(sm[1].str().c_str());
                std::string su = UpperA(sm[2].str());
                static const std::map<std::string, double> conv = {{"KIB/S", 1.0 / 1024}, {"MIB/S", 1}, {"GIB/S", 1024},
                                                                   {"KB/S", 1.0 / 1000}, {"MB/S", 1}, {"GB/S", 1000}};
                auto cit = conv.find(su);
                speed = GroupNum(sv * (cit == conv.end() ? 1 : cit->second), 1) + L" МБ/с";
            }
            static const std::regex reEta("ETA\\s+([0-9:]+)");
            std::smatch em;
            if (std::regex_search(line, em, reEta)) {
                std::wstring eta = FmtEta(em[1].str());
                if (!eta.empty()) speed += (speed.empty() ? L"" : L" · ") + eta;
            }
            std::wstring label = st.subtitle ? L"Загрузка субтитров" : audioFmt ? L"Загрузка" : (st.videoStream ? L"Загрузка видеопотока" : L"Загрузка аудиопотока");
            std::wstring left = label + L": " + W(m[1].str()) + L"% " + size;
            PostUI([gen, pct, left, speed]() {
                if (gen != A.dlGen) return;
                SetBar((float)pct, col::Red, col::Orange);
                SetStatus(left, 0x999999, speed);
            });
        } else {
            std::wstring l = W(line);
            PostUI([gen, l]() { if (gen == A.dlGen) SetStatus(l, 0x999999); });
        }
        return;
    }
    if (line.find("[Merger]") != std::string::npos || line.find("[ExtractAudio]") != std::string::npos) {
        st.lastKind = 2;
        PostUI([gen]() { if (gen == A.dlGen) SetStatus(L"Финальная обработка файла…", 0x999999); });
    }
}

// Цепочка «-f» со ступенчатыми запасными вариантами: каждый уровень — набор условий для видео.
// Сначала всё строго, потом без кодека, без частоты кадров, без ограничений — чтобы никогда не уйти на «самое большое видео».
std::string BuildFormatChain(const std::vector<std::vector<std::string>>& tiers, const std::string& aCond, const std::string& lc) {
    std::vector<std::string> parts;
    auto add = [&](const std::string& s) { if (std::find(parts.begin(), parts.end(), s) == parts.end()) parts.push_back(s); };
    for (auto& t : tiers) {
        std::string v = "bestvideo";
        if (!t.empty()) {
            v += "[";
            for (size_t i = 0; i < t.size(); i++) { if (i) v += "]["; v += t[i]; }
            v += "]";
        }
        if (!lc.empty()) { add(v + "+bestaudio" + aCond + lc); add(v + "+bestaudio" + lc); }
        add(v + "+bestaudio" + aCond);
        add(v + "+bestaudio");
    }
    add("best");
    std::string r;
    for (size_t i = 0; i < parts.size(); i++) { if (i) r += "/"; r += parts[i]; }
    return r;
}

// Аргументы yt-dlp для cookies по названию пункта в списке «Браузер»
std::vector<std::wstring> BuildCookieArgs(const std::wstring& browser) {
    std::string bkey = "none";
    static const std::map<std::wstring, std::string> bmap = {
        {L"Без авторизации", "none"}, {L"Chrome", "chrome"}, {L"Chromium", "chromium"}, {L"Opera", "opera"},
        {L"Opera GX", "opera_gx"}, {L"Edge", "edge"}, {L"Firefox", "firefox"}, {L"Brave", "brave"},
        {L"Vivaldi", "vivaldi"}, {L"Whale", "whale"}};
    auto bi = bmap.find(browser);
    if (bi != bmap.end()) bkey = bi->second;

    std::vector<std::wstring> cookie;
    if (browser == kCookiesItem) {
        std::wstring cf = CfgGet("cookies_file");
        if (!cf.empty()) cookie = {L"--cookies", cf};
    } else if (bkey != "none") {
        if (bkey == "opera_gx") {
            wchar_t ad[MAX_PATH] = {};
            GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH);
            cookie = {L"--cookies-from-browser", L"opera:" + std::wstring(ad) + L"\\Opera Software\\Opera GX Stable"};
        } else {
            cookie = {L"--cookies-from-browser", W(bkey)};
        }
    }
    return cookie;
}

std::vector<std::wstring> BrowserList() { return std::vector<std::wstring>(std::begin(kBrowsers), std::end(kBrowsers)); }
bool IsCookiesItem(const std::wstring& v) { return v == kCookiesItem; }

std::vector<std::wstring> FormatFullList() {
    std::vector<std::wstring> v;
    for (auto& f : kFormats) v.push_back(f.full);
    return v;
}
std::wstring FormatShortName(const std::wstring& full) {
    for (auto& f : kFormats) if (full == f.full) return f.shortName;
    return full;
}

// Диалог выбора cookies.txt. true — есть пригодный файл (выбранный сейчас или сохранённый раньше)
bool PickCookiesFile(bool* changed, HWND owner) {
    if (changed) *changed = false;
    wchar_t path[MAX_PATH * 2] = {};
    std::wstring prev = CfgGet("cookies_file");
    if (!prev.empty()) wcsncpy(path, prev.c_str(), MAX_PATH * 2 - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner ? owner : A.hwnd;
    ofn.lpstrFilter = L"Файлы cookies (*.txt)\0*.txt\0Все файлы\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrTitle = L"Выберите файл cookies.txt (формат Netscape)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        CfgSet("cookies_file", path);
        if (changed) *changed = true;
        return true;
    }
    return !prev.empty() && FileExists(prev);
}

static std::vector<std::wstring> BuildDownloadArgs() {
    std::vector<std::wstring> cookie = BuildCookieArgs(PV(A.pBrowser));

    std::string selExt = U8(FormatExt());
    std::string vcDisp = PVa(A.pVc), acDisp = PVa(A.pAc), selRes = PVa(A.pRes), selFps = PVa(A.pFps);
    std::string selVc = vcDisp != "-" ? MapGet(VC_RAW, vcDisp, LowerA(vcDisp)) : "-";
    std::string selAc = acDisp != "-" ? MapGet(AC_RAW, acDisp, LowerA(acDisp)) : "-";
    bool audio = (selExt == "mp3" || selExt == "m4a" || selExt == "opus");

    // выбранная аудиодорожка (язык): сначала пробуем её, при неудаче — обычный выбор
    std::string lc;
    if (A.pAudioTrack->en && A.pAudioTrack->sel > 0 && (size_t)(A.pAudioTrack->sel - 1) < A.audioCodes.size())
        lc = "[language=" + A.audioCodes[A.pAudioTrack->sel - 1] + "]";

    std::vector<std::string> fmt;
    if (audio) {
        std::string wanted = (!selAc.empty() && selAc != "-") ? selAc : "opus";
        std::vector<std::string> prefixes = wanted == "mp4a" ? std::vector<std::string>{"mp4a", "aac"}
                                                              : std::vector<std::string>{wanted};
        bool avail = false;
        for (auto& f : A.rawFormats) {
            if (js(f, "vcodec", "none") != "none") continue;
            std::string ac = js(f, "acodec");
            for (auto& p : prefixes) if (ac.rfind(p, 0) == 0) avail = true;
        }
        if (!avail) {
            fmt = {"-f", (lc.empty() ? "" : "bestaudio" + lc + "/") + std::string("bestaudio/best"), "-x", "--audio-format", selExt};
        } else {
            std::string acCond = (!selAc.empty() && selAc != "-") ? "[acodec^=" + selAc + "]" : "";
            std::string sel = "bestaudio" + acCond + "/bestaudio";
            if (!lc.empty()) sel = "bestaudio" + acCond + lc + "/bestaudio" + lc + "/" + sel;
            fmt = {"-f", sel, "-x", "--audio-format", selExt};
        }
        if (selExt == "mp3") { fmt.push_back("--audio-quality"); fmt.push_back("0"); }
    } else {
        std::string resCond, fpsCond;
        if (!selRes.empty() && selRes != "-") {
            std::smatch m;
            if (std::regex_search(selRes, m, std::regex("x(\\d+)"))) resCond = "height<=" + m[1].str();
            else if (std::regex_search(selRes, m, std::regex("\\d+"))) resCond = "height<=" + m[0].str();
        }
        if (!selFps.empty() && selFps != "-") fpsCond = "fps<=" + selFps;
        std::string aCond = (!selAc.empty() && selAc != "-") ? "[acodec^=" + selAc + "]" : "";
        // Разрешение важнее частоты кадров и кодека: сначала ровно выбранная высота (height=H), потом не выше (height<=H)
        std::string exactCond = resCond.empty() ? "" : "height=" + resCond.substr(resCond.find("<=") + 2);
        auto tier = [&](const std::string& h, bool fps) {
            std::vector<std::string> c;
            if (!h.empty()) c.push_back(h);
            if (fps && !fpsCond.empty()) c.push_back(fpsCond);
            return c;
        };
        std::vector<std::vector<std::string>> tiers;
        if (!selVc.empty() && selVc != "-") {
            for (bool fps : {true, false})
                for (auto& al : VcodecAliases(selVc)) { auto t = tier(exactCond, fps); t.push_back("vcodec^=" + al); tiers.push_back(t); }
        }
        tiers.push_back(tier(exactCond, true)); tiers.push_back(tier(exactCond, false));
        tiers.push_back(tier(resCond, true));   tiers.push_back(tier(resCond, false));
        tiers.push_back(tier("", false));
        fmt = {"-f", BuildFormatChain(tiers, aCond, lc)};
        if (!selExt.empty() && selExt != "-") { fmt.push_back("--merge-output-format"); fmt.push_back(selExt); }
    }

    std::vector<std::wstring> cmd = {ResPath(L"yt-dlp.exe"), L"--newline", L"--no-colors", L"--encoding", L"utf-8"};
    cmd.insert(cmd.end(), cookie.begin(), cookie.end());
    std::wstring proxy = GetProxyUrl();
    if (!proxy.empty()) { cmd.push_back(L"--proxy"); cmd.push_back(proxy); }
    for (auto& s : fmt) cmd.push_back(W(s));
    if (!A.trimStart.empty() || !A.trimEnd.empty()) {
        std::wstring sec = (!A.trimStart.empty() && !A.trimEnd.empty()) ? L"*" + A.trimStart + L"-" + A.trimEnd
                           : !A.trimStart.empty() ? L"*" + A.trimStart + L"-inf" : L"*0-" + A.trimEnd;
        cmd.push_back(L"--download-sections"); cmd.push_back(sec);
        cmd.push_back(L"--force-keyframes-at-cuts");
    }
    // субтитры вшиваются в видео (MKV/MP4)
    if (!audio && A.pSubs->en && A.pSubs->sel > 0 && (size_t)(A.pSubs->sel - 1) < A.subCodes.size()) {
        size_t si = (size_t)(A.pSubs->sel - 1);
        cmd.push_back(si < A.subAuto.size() && A.subAuto[si] ? L"--write-auto-subs" : L"--write-subs");
        cmd.push_back(L"--sub-langs"); cmd.push_back(W(A.subCodes[si]));
        cmd.push_back(L"--embed-subs");
    }
    cmd.push_back(L"-o");
    cmd.push_back(A.destFolder + L"\\%(title)s.%(ext)s");
    return cmd;
}

static void RunDownload() {
    std::wstring url = Trim(A.main->GetText(A.urlEdit));
    if (url.empty()) { ReportError(L"Вставьте ссылку на видео или плейлист!"); return; }
    if (g_selfUpdating) { ReportError(L"Идёт обновление программы. Подождите."); return; }
    if (PV(A.pBrowser) == kCookiesItem) {
        std::wstring cf = CfgGet("cookies_file");
        if (cf.empty() || !FileExists(cf)) { ReportError(L"Файл cookies.txt не найден. Выберите его заново в списке «Браузер»."); return; }
    }
    if (!FileExists(ResPath(L"yt-dlp.exe")) || !FileExists(ResPath(L"ffmpeg.exe"))) { ShowInstaller(); return; }   // нечем качать — сначала ставим компоненты

    SHCreateDirectoryExW(nullptr, A.destFolder.c_str(), nullptr);
    std::vector<std::wstring> cmd = BuildDownloadArgs();
    cmd.push_back(url);

    SetStopButton();
    A.btnOpen->en = false;
    A.main->Inval(A.btnOpen);
    SetBar(0, col::Amber, col::Amber);
    SetStatus(L"Подключение и анализ…", 0xffffff);

    int gen = ++A.dlGen;
    g_dlStartFt = NowFileTime();
    std::wstring ext = FormatExt();
    bool audioFmt = IsAudioExt(ext);
    auto proc = std::make_shared<Proc>();
    A.dlProc = proc;
    std::wstring dlUrl = url;
    std::map<std::string, bool> fmtMap;
    for (auto& f : A.rawFormats) {
        std::string vc = js(f, "vcodec", "none");
        fmtMap[js(f, "format_id")] = !vc.empty() && vc != "none";
    }

    std::thread([proc, cmd, gen, ext, audioFmt, dlUrl, fmtMap]() {
        time_t t = time(nullptr);
        tm lt;
        localtime_s(&lt, &t);
        char ts[32];
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &lt);
        WriteFileA(GetLogPath(), std::string("\n[") + ts + "] " + U8(dlUrl) + "\n", true);

        if (!proc->Start(cmd, true)) {
            PostUI([gen]() {
                if (gen != A.dlGen) return;
                RestoreDlButton();
                SetBar(0, col::Red, col::Red);
                SetStatus(L"Не удалось запустить yt-dlp.exe — проверьте, что он лежит рядом с программой.", 0xffffff);
                PlayKindSound("error");
            });
            return;
        }
        DlLineState st;
        st.fmtHasVideo = fmtMap;
        std::string buf;
        char chunk[8192];
        DWORD n;
        while ((n = proc->Read(chunk, sizeof chunk)) > 0) {
            WriteFileA(GetLogPath(), std::string(chunk, n), true);
            buf.append(chunk, n);
            size_t pos;
            while ((pos = buf.find_first_of("\r\n")) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (!line.empty()) HandleDlLine(line, ext, audioFmt, gen, st);
            }
        }
        if (!buf.empty()) HandleDlLine(buf, ext, audioFmt, gen, st);
        int code = proc->Wait();
        proc->Close();
        WriteFileA(GetLogPath(), "\n", true);

        PostUI([gen, st, code]() {
            if (gen != A.dlGen) return;
            A.dlProc.reset();
            RestoreDlButton();
            bool error = (st.lastKind == 3) || (code != 0);
            if (!error) {
                SetBar(1.f, col::Green, col::Green);
                SetStatus(L"Успешно завершено!", 0xffffff);
                A.btnOpen->en = true;
                A.main->Inval(A.btnOpen);
                PlayKindSound("success");
                std::wstring u = Trim(A.main->GetText(A.urlEdit));
                std::wstring title = A.prev.valid ? A.prev.title : L"";
                if (title.empty()) title = W(st.title);
                if (!u.empty()) SaveHistory(u, title);
                ShowToast(L"RedStream", title.empty() ? L"Скачивание завершено!" : title + L"\nСкачивание завершено!");
            } else {
                if (st.lastKind != 3) {
                    SetBar(A.bar->val, col::Red, col::Red);
                    SetStatus(L"Ошибка! Проверьте ссылку или закройте браузер.", 0xffffff);
                }
                PlayKindSound("error");
            }
        });
    }).detach();
}

void BrowseFolder() {
    BROWSEINFOW bi{};
    bi.hwndOwner = A.hwnd;
    bi.lpszTitle = L"Выберите папку для сохранения";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    std::wstring init = DirExists(A.destFolder) ? A.destFolder : A.destFolder.substr(0, A.destFolder.find_last_of(L"\\/"));
    bi.lParam = (LPARAM)init.c_str();
    bi.lpfn = [](HWND h, UINT m, LPARAM, LPARAM d) -> int {
        if (m == BFFM_INITIALIZED && d) SendMessageW(h, BFFM_SETSELECTIONW, TRUE, d);
        return 0;
    };
    if (PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi)) {
        wchar_t path[MAX_PATH * 2];
        if (SHGetPathFromIDListW(pidl, path)) {
            A.destFolder = path;
            SaveDestFolder(A.destFolder);
            A.pFolder->t2 = A.destFolder;
            A.main->Inval(A.pFolder);
        }
        CoTaskMemFree(pidl);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Меню/всплывающие окна: закрытие
// ═════════════════════════════════════════════════════════════════════════════
void CloseMenu() {
    if (A.menu) {
        Panel::Dispose(A.menu);
        A.menu = nullptr;
        A.menuAnchor = nullptr;
    }
}

void ReportError(const std::wstring& msg) {
    PlayKindSound("error");
    float w = 280, h = 120;
    Panel* p = NewPopup(w, h, L"Внимание!", nullptr);
    auto* t = p->AddLabel(G::RectF(UF(16), UF(46), UF(w - 32), UF(36)), msg, UF(10), 0xffffff, gfx::CENTER);
    t->wrap = true;
    Ctl* ok = p->AddButton(G::RectF(UF(w / 2 - 45), UF(h - 36), UF(90), UF(24)), L"ОК", [p]() { Panel::Dispose(p); });
    ok->px = UF(10); ok->bold = true; ok->bg = col::Red; ok->bgH = col::RedHover; ok->fg = 0xffffff; ok->rad = UF(5);
    CenterPopup(p);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Трей и уведомления
// ═════════════════════════════════════════════════════════════════════════════
static bool g_trayAdded = false;
static HICON g_icon = nullptr;

static HICON AppIcon() {
    if (!g_icon) g_icon = (HICON)LoadImageW(A.hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                            GetSystemMetrics(SM_CYSMICON), 0);
    return g_icon;
}

static NOTIFYICONDATAW TrayData() {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof n;
    n.hWnd = A.hwnd;
    n.uID = 1;
    n.uCallbackMessage = WM_TRAYICON;
    n.hIcon = AppIcon();
    wcscpy(n.szTip, L"RedStream");
    return n;
}

static void TrayAdd() {
    if (g_trayAdded) return;
    NOTIFYICONDATAW n = TrayData();
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    Shell_NotifyIconW(NIM_ADD, &n);
    g_trayAdded = true;
}
static void TrayRemove() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW n = TrayData();
    Shell_NotifyIconW(NIM_DELETE, &n);
    g_trayAdded = false;
}

static HICON AppIconBig() {
    static HICON g_big = nullptr;
    if (!g_big) g_big = (HICON)LoadImageW(A.hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, 0);
    return g_big ? g_big : AppIcon();
}

static std::wstring g_toastTitle, g_toastMsg;

static void SendBalloon() {
    NOTIFYICONDATAW n = TrayData();
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = AppIconBig();
    wcsncpy(n.szInfoTitle, g_toastTitle.c_str(), 63);
    wcsncpy(n.szInfo, g_toastMsg.c_str(), 255);
    if (!Shell_NotifyIconW(NIM_MODIFY, &n))
        WriteFileA(GetLogPath(), "[toast] Shell_NotifyIcon(NIM_MODIFY) не удалось, ошибка " + std::to_string(GetLastError()) + "\n", true);
}

// Только что добавленная иконка трея не всегда успевает показать уведомление, поэтому ждём 0,4 с
void ShowToast(const std::wstring& title, const std::wstring& msg) {
    bool fresh = !g_trayAdded;
    TrayAdd();
    g_toastTitle = title;
    g_toastMsg = msg;
    if (fresh) SetTimer(A.hwnd, T_BALLOON, 400, nullptr);
    else SendBalloon();
    if (!A.inTray) SetTimer(A.hwnd, T_TOAST, 15000, nullptr);
}

static void RestoreFromTray() {
    A.inTray = false;
    TrayRemove();
    ShowWindow(A.hwnd, SW_SHOW);
    ShowWindow(A.hwnd, SW_RESTORE);
    SetForegroundWindow(A.hwnd);
}

void MinimizeToTray() {
    CloseMenu();
    CloseList();
    A.inTray = true;
    TrayAdd();
    ShowWindow(A.hwnd, SW_HIDE);
}

int  GetAfterAction() { return CfgGetInt("sched_after", 0); }   // 0 — ничего, 1 — выключить, 2 — сон, 3 — гибернация
void SetAfterAction(int v) { CfgSetInt("sched_after", v); }

// Вызывается после завершения всех загрузок планировщика
void DoAfterAction() {
    int a = GetAfterAction();
    if (a < 1 || a > 3) return;
    if (A.inTray) RestoreFromTray();      // предупреждение должно быть видно
    ShowPowerCountdown(a);
}

enum { M_OPEN = 100, M_UPDATE, M_ABOUT, M_EXIT };

static void TrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, M_OPEN, L"Открыть RedStream");
    SetMenuDefaultItem(m, M_OPEN, FALSE);
    AppendMenuW(m, MF_STRING, M_UPDATE, L"Обновление компонентов");
    AppendMenuW(m, MF_STRING, M_ABOUT, L"О программе…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_EXIT, L"Выход");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(A.hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, A.hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case M_OPEN: RestoreFromTray(); break;
    case M_UPDATE: RestoreFromTray(); PostUI([]() { ShowUpdater(); }); break;
    case M_ABOUT: RestoreFromTray(); PostUI([]() { ShowAbout(); }); break;
    case M_EXIT: DestroyWindow(A.hwnd); break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Баннеры обновлений
// ═════════════════════════════════════════════════════════════════════════════
static Ctl* g_appBannerBg = nullptr;                          // баннер «новая версия программы»
static std::shared_ptr<std::wstring> g_appBannerOvr;          // не пусто — вместо текста баннера показывается статус
static void StartSelfUpdate();

static int g_bannerActive = -1;                // индекс показываемого баннера (-1 — нет)


// Баннеры занимают место панели меню (поверх «Настройки / Дополнительно / Помощь»), размер окна не меняется.
// Показывается один баннер за раз; если их несколько — они сменяют друг друга.
static void ApplyBannerVis() {
    for (int i = 0; i < (int)A.banners.size(); i++) {
        Banner& b = A.banners[i];
        bool act = (i == g_bannerActive) && !b.dismissed;
        bool busy = (b.kind == 0 && g_appBannerOvr && !g_appBannerOvr->empty());
        b.bgCtl->vis = act;
        b.blockCtl->vis = act;
        b.linkCtl->vis = act && !busy;
        b.closeCtl->vis = act && !busy;
    }
    A.main->InvalAll();
}

static void RotateBanner() {
    int n = (int)A.banners.size();
    if (n < 2 || g_selfUpdating) return;
    for (int k = 1; k <= n; k++) {
        int i = (g_bannerActive + k + n) % n;
        if (!A.banners[i].dismissed) { g_bannerActive = i; break; }
    }
    ApplyBannerVis();
}

static void DismissBanner(int i) {
    int n = (int)A.banners.size();
    if (i < 0 || i >= n) return;
    if (A.banners[i].kind == 0 && g_selfUpdating) return;
    A.banners[i].dismissed = true;
    g_bannerActive = -1;
    for (int k = 1; k <= n; k++) {
        int j = (i + k) % n;
        if (!A.banners[j].dismissed) { g_bannerActive = j; break; }
    }
    ApplyBannerVis();
}

static void ShowBanner(int kind, const std::wstring& pre, const std::wstring& link, const std::wstring& post) {
    uint32_t bg, fg, lk, lkH;
    if (kind == 0)      { bg = 0xffaa00; fg = 0x1e1e1e; lk = 0xff0000; lkH = 0xcc0000; }
    else if (kind == 1) { bg = 0x1a6b1a; fg = 0xffffff; lk = 0x00ff88; lkH = 0x88ffbb; }
    else                { bg = 0x1a5a8a; fg = 0xffffff; lk = 0x88ddff; lkH = 0xc0eeff; }

    int idx = (int)A.banners.size();
    float sep = std::max(1.f, UF(1));
    float y = UF(lay::TitleH) + sep;                    // ровно поверх панели меню
    float H = UF(lay::ToolbarH) - sep;
    float W = UF(lay::W);
    float textW = W - H;                                // справа — кнопка закрытия

    HDC dc = GetDC(A.hwnd);
    G::Graphics g(dc);
    G::Font* f = gfx::GetFont(UF(10));
    G::Font* fb = gfx::GetFont(UF(10), true, false, true);
    float w1 = gfx::TextWidth(g, pre, f), w2 = gfx::TextWidth(g, link, fb), w3 = gfx::TextWidth(g, post, f);
    ReleaseDC(A.hwnd, dc);
    float total = w1 + w2 + w3, x0 = std::max(UF(6), (textW - total) / 2);

    Ctl* bgc = A.main->Add(K::Custom, G::RectF(0, y, W, H));
    bgc->body = false;
    auto ovr = std::make_shared<std::wstring>();
    if (kind == 0) { g_appBannerOvr = ovr; g_appBannerBg = bgc; }
    bgc->draw = [=](G::Graphics& gg, Ctl& c) {
        G::SolidBrush b(C(bg));
        gg.FillRectangle(&b, c.r);
        if (!ovr->empty()) {
            gfx::Text(gg, *ovr, c.r, gfx::GetFont(UF(10)), C(fg), gfx::CENTER, false);
            return;
        }
        gfx::Text(gg, pre, G::RectF(x0, c.r.Y, w1 + 2, c.r.Height), gfx::GetFont(UF(10)), C(fg), gfx::LEFT, false);
        gfx::Text(gg, post, G::RectF(x0 + w1 + w2, c.r.Y, w3 + 2, c.r.Height), gfx::GetFont(UF(10)), C(fg), gfx::LEFT, false);
    };

    // невидимая кнопка на всю ширину — чтобы клики не проваливались в кнопки панели меню под баннером
    Ctl* blk = A.main->AddButton(G::RectF(0, y, W, H), L"", []() {});
    blk->body = false; blk->hand = false;

    Ctl* lc = A.main->AddButton(G::RectF(x0 + w1 - UF(4), y, w2 + UF(8), H), link, nullptr);
    lc->body = false;
    lc->px = UF(10); lc->bold = true; lc->ul = true; lc->fg = lk; lc->fgH = lkH;
    lc->ha = gfx::CENTER;
    lc->onClick = [kind, idx]() {
        if (kind == 0) { StartSelfUpdate(); return; }              // здесь баннер показывает ход обновления
        DismissBanner(idx);                                         // плашка компонентов пропадает после нажатия
        ShowUpdater();
    };

    Ctl* cl = A.main->AddButton(G::RectF(W - H - sep, y, H, H), L"", [idx]() { DismissBanner(idx); });
    cl->body = false; cl->px = UF(10); cl->icon = gfx::I_CLOSE; cl->iconPx = UF(8);
    cl->fg = fg; cl->fgH = lkH; cl->rad = 0;

    A.banners.push_back({kind, pre, link, post, lc});
    Banner& nb = A.banners.back();
    nb.bgCtl = bgc; nb.blockCtl = blk; nb.closeCtl = cl;
    if (g_bannerActive < 0) g_bannerActive = idx;
    ApplyBannerVis();
    if (A.banners.size() > 1) SetTimer(A.hwnd, T_BANNER, 6000, nullptr);
}

// Текст поверх баннера обновления программы (пустая строка — вернуть обычный вид)
static void SetAppBanner(const std::wstring& text) {
    if (!g_appBannerOvr || !g_appBannerBg) return;
    *g_appBannerOvr = text;
    if (!text.empty()) {                                 // на время обновления баннер программы всегда на виду
        for (int i = 0; i < (int)A.banners.size(); i++)
            if (A.banners[i].kind == 0) { A.banners[i].dismissed = false; g_bannerActive = i; }
    }
    ApplyBannerVis();
}

// Скачивает свежий RedStream.exe с GitHub и подменяет им текущий файл:
// работающий exe переименовывается в .old (Windows это позволяет), новый встаёт на его место, затем перезапуск.
static void StartSelfUpdate() {
    if (g_selfUpdating) return;
    if (A.dlProc) { ReportError(L"Дождитесь окончания загрузки и повторите."); return; }
    if (UpdaterBusy()) { ReportError(L"Дождитесь окончания обновления компонентов."); return; }
    g_selfUpdating = true;
    SetAppBanner(L"Подключение…");

    std::thread([]() {
        std::wstring exe = ExePath(), tmp = exe + L".new", old = exe + L".old";
        DeleteFileW(tmp.c_str());

        int lastPct = -1;
        bool ok = HttpDownload(APP_UPDATE_URL, tmp, [&lastPct](uint64_t done, uint64_t total) {
            int pct = total ? (int)(done * 100 / total) : (int)(done / 1048576);
            if (pct == lastPct) return true;
            lastPct = pct;
            wchar_t b[160];
            if (total) swprintf(b, 160, L"Скачивание обновления… %d%%  (%.1f МБ из %.1f МБ)", pct, done / 1048576.0, total / 1048576.0);
            else       swprintf(b, 160, L"Скачивание обновления… %.1f МБ", done / 1048576.0);
            std::wstring t = b;
            std::replace(t.begin(), t.end(), L'.', L',');
            PostUI([t]() { SetAppBanner(t); });
            return true;
        }, 60000);

        // проверка: файл разумного размера и начинается с «MZ»
        if (ok) {
            WIN32_FILE_ATTRIBUTE_DATA fa{};
            ok = GetFileAttributesExW(tmp.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeHigh == 0 && fa.nFileSizeLow > 256 * 1024;
            if (ok) {
                char mz[2] = {0, 0};
                if (FILE* f = _wfopen(tmp.c_str(), L"rb")) { fread(mz, 1, 2, f); fclose(f); }
                ok = mz[0] == 'M' && mz[1] == 'Z';
            }
        }

        bool installed = false;
        if (ok) {
            PostUI([]() { SetAppBanner(L"Установка обновления…"); });
            DeleteFileW(old.c_str());
            if (MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                if (MoveFileExW(tmp.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) installed = true;
                else MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);     // откат
            }
        }
        if (!installed) DeleteFileW(tmp.c_str());

        PostUI([installed, exe]() {
            if (!installed) {
                g_selfUpdating = false;
                SetAppBanner(L"");
                for (int i = 0; i < (int)A.banners.size(); i++) if (A.banners[i].kind == 0) DismissBanner(i);
                ReportError(L"Не удалось обновиться автоматически. Открываю страницу загрузки.");
                ShellOpen(GITHUB_RELEASES);
                return;
            }
            SetAppBanner(L"Обновление установлено. Перезапуск…");
            PlayKindSound("success");
            ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, ExeDir().c_str(), SW_SHOWNORMAL);
            DestroyWindow(A.hwnd);
        });
    }).detach();
}

static void CheckFfmpegUpdate() {
    std::thread([]() {
        ProcResult r = RunCapture({ResPath(L"ffmpeg.exe"), L"-version"}, 10000);
        std::smatch m;
        if (!r.started || !std::regex_search(r.out, m, std::regex("(\\d{4}-\\d{2}-\\d{2})"))) return;
        std::string current = m[1];
        std::string online;
        if (!HttpGet(L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-git-essentials.7z.ver", online, 10000)) return;
        std::smatch m2;
        if (!std::regex_search(online, m2, std::regex("(\\d{4}-\\d{2}-\\d{2})"))) return;
        std::string latest = m2[1];
        if (latest > current) {
            PostUI([=]() {
                ShowBanner(2, L"ffmpeg устарел (" + W(current) + L" → " + W(latest) + L"). Нажмите ", L"обновить", L", чтобы продолжить.");
            });
        }
    }).detach();
}

static bool VersionNewer(const std::string& a, const std::string& b);

static void CheckYtdlpUpdate() {
    std::thread([]() {
        ProcResult r = RunCapture({ResPath(L"yt-dlp.exe"), L"--version"}, 10000);
        std::string current = TrimA(r.out);
        if (r.started && !current.empty()) {
            std::string body;
            if (HttpGet(L"https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest", body, 10000)) {
                try {
                    std::string latest = TrimA(json::parse(body).value("tag_name", ""));
                    if (!latest.empty() && VersionNewer(latest, current)) {
                        PostUI([=]() {
                            ShowBanner(1, L"yt-dlp устарел (" + W(current) + L" → " + W(latest) + L"). Нажмите ", L"обновить", L", чтобы продолжить.");
                        });
                    }
                } catch (...) {}
            }
        }
        PostUI([]() { CheckFfmpegUpdate(); });
    }).detach();
}

// Достаёт числовую версию из чего угодно: «v3.2», «RedStream 3.2 (build 5)», «2026.03.17.232842»
static std::vector<long> VerParts(const std::string& s) {
    std::vector<long> v;
    std::smatch m;
    static const std::regex re("(\\d+(?:\\.\\d+)*)");
    if (!std::regex_search(s, m, re)) return v;
    std::string t = m[1].str(), cur;
    for (char c : t + ".") {
        if (c == '.') { v.push_back(std::atol(cur.c_str())); cur.clear(); }
        else cur += c;
    }
    return v;
}

static std::string VerString(const std::string& s) {
    std::smatch m;
    static const std::regex re("(\\d+(?:\\.\\d+)*)");
    return std::regex_search(s, m, re) ? m[1].str() : s;
}

// true, если версия a строго новее версии b (числовое сравнение по компонентам; не разобрали — не новее)
static bool VersionNewer(const std::string& a, const std::string& b) {
    auto va = VerParts(a), vb = VerParts(b);
    if (va.empty()) return false;
    size_t n = std::max(va.size(), vb.size());
    va.resize(n, 0); vb.resize(n, 0);
    return std::lexicographical_compare(vb.begin(), vb.end(), va.begin(), va.end());
}

static void CheckAppUpdate() {
    std::thread([]() {
        DeleteFileW((ExePath() + L".old").c_str());     // остатки предыдущего самообновления
        DeleteFileW((ExePath() + L".new").c_str());
        std::string body;
        if (HttpGet(GITHUB_API_URL, body, 5000)) {
            try {
                json rel = json::parse(body);
                bool skip = rel.value("draft", false) || rel.value("prerelease", false);
                std::string tag = rel.value("tag_name", "");
                if (VerParts(tag).empty()) tag = rel.value("name", "");       // тег без цифр — пробуем название релиза
                tag = VerString(tag);
                if (!skip && !tag.empty() && VersionNewer(tag, U8(APP_VERSION))) {
                    PostUI([tag]() {
                        ShowBanner(0, L"Вышла новая версия «" + W(tag) + L"». Нажмите ", L"сюда", L", чтобы обновить.");
                    });
                }
            } catch (...) {}
        }
        PostUI([]() { CheckYtdlpUpdate(); });
    }).detach();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Заставка (banner.png: fade in → hold → fade out)
// ═════════════════════════════════════════════════════════════════════════════
struct Splash {
    HWND hwnd = nullptr;
    std::unique_ptr<G::Bitmap> img;
    int step = 0, phase = 0;           // phase 0 = in, 1 = hold, 2 = out
    std::function<void()> done;
};
static Splash* g_splash = nullptr;
static const int kSplashSteps = 30;

static LRESULT CALLBACK SplashProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Splash* s = g_splash;
    if (!s) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        G::Graphics g(dc);
        g.DrawImage(s->img.get(), 0, 0, (INT)s->img->GetWidth(), (INT)s->img->GetHeight());
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: {
        KillTimer(h, 1);
        if (s->phase == 0) {
            SetLayeredWindowAttributes(h, 0, (BYTE)(255 * s->step / kSplashSteps), LWA_ALPHA);
            if (s->step < kSplashSteps) { s->step++; SetTimer(h, 1, 30, nullptr); }
            else { s->phase = 1; SetTimer(h, 1, 1200, nullptr); }
        } else if (s->phase == 1) {
            s->phase = 2; s->step = 0;
            SetTimer(h, 1, 30, nullptr);
        } else {
            SetLayeredWindowAttributes(h, 0, (BYTE)(255 - 255 * s->step / kSplashSteps), LWA_ALPHA);
            if (s->step < kSplashSteps) { s->step++; SetTimer(h, 1, 30, nullptr); }
            else {
                auto done = s->done;
                g_splash = nullptr;
                DestroyWindow(h);
                delete s;
                if (done) done();
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowSplash(std::function<void()> done) {
    auto img = gfx::LoadBitmapRes(2, L"banner.png");
    if (!img) { done(); return; }
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = SplashProc;
    wc.hInstance = A.hinst;
    wc.lpszClassName = L"RSSplash";
    RegisterClassExW(&wc);
    auto* s = new Splash();
    s->img = std::move(img);
    s->done = std::move(done);
    g_splash = s;
    int iw = (int)s->img->GetWidth(), ih = (int)s->img->GetHeight();
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    s->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"RSSplash", L"", WS_POPUP,
                              (sw - iw) / 2, (sh - ih) / 2, iw, ih, nullptr, nullptr, A.hinst, nullptr);
    SetLayeredWindowAttributes(s->hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(s->hwnd, SW_SHOWNOACTIVATE);
    SetTimer(s->hwnd, 1, 50, nullptr);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Построение главного окна — 1:1 с блоком .mockup-window
// ═════════════════════════════════════════════════════════════════════════════
static std::wstring DurStr(double d) {
    long long s = (long long)d, h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    return h ? Fmt(L"%lld:%02lld:%02lld", h, m, sec) : Fmt(L"%lld:%02lld", m, sec);
}

static void StyleGray(Ctl* c, float px) {
    c->px = px;
    c->bg = col::GrayBg; c->bgH = col::GrayHover; c->bgD = 0x2a2a2a;
    c->bd = col::GrayBorder; c->bdH = 0x555555; c->bdD = 0x333333;
    c->fg = col::GrayFg; c->fgH = 0xdddddd; c->fgD = 0x555555;
    c->rad = UF(5);
}

static Ctl* MakePill(float x, float y, float w, const wchar_t* label) {
    Ctl* c = A.main->AddPill(G::RectF(x, y, w, UF(22)), label, nullptr);
    c->px = UF(9);
    return c;
}

static void BuildMain() {
    Panel* p = A.main;
    const float W = UF(lay::W);

    p->paintBg = [](G::Graphics& g, const RECT& cr) {
        float w = (float)cr.right, h = (float)cr.bottom;
        G::SolidBrush body(C(col::Body)), bar(C(col::Bar));
        g.FillRectangle(&body, 0.f, 0.f, w, h);
        g.FillRectangle(&bar, 0.f, 0.f, w, UF(lay::TitleH + lay::ToolbarH));
        G::SolidBrush bdr(C(col::BarBorder));
        g.FillRectangle(&bdr, 0.f, UF(lay::TitleH), w, std::max(1.f, UF(1)));
        // иконка приложения (из icon.ico, вшита в exe)
        static std::unique_ptr<G::Bitmap> ico;
        if (!ico) {
            HICON hi = (HICON)LoadImageW(A.hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, 64, 64, 0);
            if (hi) { ico.reset(G::Bitmap::FromHICON(hi)); DestroyIcon(hi); }
        }
        float d = UF(16);
        if (ico) g.DrawImage(ico.get(), G::RectF(UF(8), (UF(lay::TitleH) - d) / 2, d, d));
        // внешняя рамка окна
        G::Pen pen(C(col::WinBorder), std::max(1.f, UF(1)));
        g.DrawRectangle(&pen, 0.5f, 0.5f, w - 1, h - 1);
    };

    // ── Заголовок ─────────────────────────────────────────────────────────────
    float bx = W;
    auto winBtn = [&](int icon, const wchar_t* t, float w, std::function<void()> fn, uint32_t hoverBg) {
        bx -= UF(w);
        Ctl* b = p->AddButton(G::RectF(bx, 0, UF(w), UF(lay::TitleH)), t, fn);
        b->body = false; b->px = UF(10); b->icon = icon; b->iconPx = UF(9);
        b->fg = col::WinBtn; b->fgH = 0xffffff; b->bgH = hoverBg; b->rad = 0;
        return b;
    };
    winBtn(gfx::I_CLOSE, L"", 34, []() { DestroyWindow(A.hwnd); }, 0xc42b1c);
    winBtn(gfx::I_MINUS, L"", 30, []() { ShowWindow(A.hwnd, SW_MINIMIZE); }, 0x3a3a3a);
    winBtn(gfx::I_QUESTION, L"", 28, []() { ShowAbout(); }, 0x3a3a3a);
    winBtn(gfx::I_ARROW_DOWN, L"", 28, []() { ShowUpdater(); }, 0x3a3a3a);
    Ctl* title = p->AddLabel(G::RectF(UF(30), 0, bx - UF(30), UF(lay::TitleH)),
                             L"RedStream: Video Downloader", UF(10), col::TitleText, gfx::CENTER);
    title->body = false;

    // ── Панель меню ───────────────────────────────────────────────────────────
    float y = UF(lay::TitleH) + std::max(1.f, UF(1));
    float x = UF(4);
    G::RectF tmp;
    HDC dc = GetDC(A.hwnd);
    G::Graphics gm(dc);
    auto menuBtn = [&](const wchar_t* t, int which) {
        float w = gfx::TextWidth(gm, t, gfx::GetFont(UF(10))) + UF(18);
        Ctl* b = p->AddButton(G::RectF(x, y + UF(2), w, UF(lay::ToolbarH) - UF(4)), t, [which]() { OpenMenu(which); });
        b->body = false; b->px = UF(10); b->fg = col::MenuText; b->fgH = 0xffffff; b->bgH = 0x2a2a2a; b->rad = UF(3);
        x += w + UF(1);
        return b;
    };
    A.btnSettings = menuBtn(L"Настройки", 0);
    A.btnExtra    = menuBtn(L"Дополнительно", 1);
    A.btnHelp     = menuBtn(L"Помощь", 2);
    {
        const wchar_t* t = L"Свернуть в трей";
        float w = gfx::TextWidth(gm, t, gfx::GetFont(UF(9))) + UF(18);
        A.btnTray = p->AddButton(G::RectF(W - UF(4) - w, y + UF(2), w, UF(lay::ToolbarH) - UF(4)), t, []() { MinimizeToTray(); });
        A.btnTray->body = false; A.btnTray->px = UF(9); A.btnTray->fg = col::WinBtn; A.btnTray->fgH = 0xdddddd;
        A.btnTray->bgH = 0x2a2a2a; A.btnTray->rad = UF(3);
    }
    ReleaseDC(A.hwnd, dc);

    // ── Тело ──────────────────────────────────────────────────────────────────
    const float pad = UF(lay::Pad);
    const float cw = W - pad * 2;
    float by = UF(lay::TitleH + lay::ToolbarH) + pad;

    // строка URL: поле + история + «Вставить» + ⚙
    float hGear = UF(26), wGear = UF(30), wPaste = UF(54), wHist = UF(22), gap = UF(6);
    float wInput = cw - wGear - wPaste - wHist - gap * 3;
    A.urlEdit = p->AddEdit(G::RectF(pad, by, wInput, hGear), UF(10), true, []() {
        SetTimer(A.hwnd, T_URL, 700, nullptr);
    }, false, L"Ссылка на видео или плейлист");
    A.urlEdit->hand = false;
    g_replaceOnPaste = A.urlEdit->h;
    Ctl* hist = p->AddButton(G::RectF(pad + wInput + gap, by, wHist, hGear), L"", nullptr);
    StyleGray(hist, UF(10));
    hist->icon = gfx::I_CHEVRON; hist->iconPx = UF(11);
    hist->onClick = [hist]() {
        auto items = LoadHistoryCombo();
        if (items.empty()) return;
        PillDropdown(A.main, hist, items, -1, [items](int i) {
            A.main->SetText(A.urlEdit, items[i], true);
            KillTimer(A.hwnd, T_URL);
            A.prevUrl = items[i];
            FetchPreview(items[i]);
        });
        // PillDropdown не меняет кнопку «▾» (у неё нет items) — просто выпадающий список
    };
    Ctl* paste = p->AddButton(G::RectF(pad + wInput + wHist + gap * 2, by, wPaste, hGear), L"Вставить", []() { PasteUrl(); });
    StyleGray(paste, UF(9));
    A.gear = p->AddButton(G::RectF(W - pad - wGear, by, wGear, hGear), L"", []() { ToggleDlSettings(); });
    StyleGray(A.gear, UF(10));
    A.gear->icon = gfx::I_GEAR; A.gear->iconPx = UF(13);
    A.gear->en = false;
    by += hGear + UF(9);

    // карточка предпросмотра
    float hPrev = UF(52);
    A.prevCard = p->Add(K::Custom, G::RectF(pad, by, cw, hPrev));
    A.prevCard->draw = [](G::Graphics& g, Ctl& c) {
        gfx::FillRR(g, c.r, UF(7), C(col::PrevBg));
        gfx::StrokeRR(g, c.r, UF(7), C(col::PrevBorder), std::max(1.f, UF(1)));
        float tx = c.r.X + UF(9), ty = c.r.Y + (c.r.Height - UF(34)) / 2;
        G::RectF tr(tx, ty, UF(48), UF(34));
        if (A.thumb) {
            G::GraphicsPath path;
            gfx::AddRR(path, tr, UF(4));
            G::GraphicsState st = g.Save();
            g.SetClip(&path);
            float iw = (float)A.thumb->GetWidth(), ih = (float)A.thumb->GetHeight();
            float sc = std::max(tr.Width / iw, tr.Height / ih);
            float dw = iw * sc, dh = ih * sc;
            g.DrawImage(A.thumb.get(), G::RectF(tr.X + (tr.Width - dw) / 2, tr.Y + (tr.Height - dh) / 2, dw, dh));
            g.Restore(st);
        } else {
            G::LinearGradientBrush lb(tr, C(0xff0000), C(0x880000), 45.f, false);
            gfx::FillRRBrush(g, tr, UF(4), lb);
            gfx::DrawIcon(g, gfx::I_PLAY, G::RectF(tr.X + UF(17), tr.Y + UF(10), UF(14), UF(14)), C(0xffffff, 180));
        }
        float x0 = tr.X + tr.Width + UF(9);
        float w = c.r.X + c.r.Width - UF(9) - x0;
        bool has = A.prev.valid;
        std::wstring title = has ? A.prev.title : L"Название видео появится здесь";
        gfx::Text(g, title, G::RectF(x0, c.r.Y + UF(9), w, UF(16)), gfx::GetFont(UF(9), true), C(has ? col::PrevTitle : 0x888888),
                  gfx::LEFT, true);
        float my = c.r.Y + UF(26);
        if (has) {
            float ix = x0;
            std::wstring meta;
            if (A.prev.duration >= 0) {
                gfx::DrawIcon(g, gfx::I_CLOCK, G::RectF(ix, my + UF(2), UF(11), UF(11)), C(col::PrevMeta), std::max(1.f, UF(1)));
                ix += UF(15);
                meta = DurStr(A.prev.duration);
            }
            if (!A.prev.extractor.empty()) meta += (meta.empty() ? L"" : L" · ") + A.prev.extractor;
            if (!A.prev.bestRes.empty()) meta += (meta.empty() ? L"" : L" · ") + A.prev.bestRes;
            gfx::Text(g, meta, G::RectF(ix, my, c.r.X + c.r.Width - UF(9) - ix, UF(15)), gfx::GetFont(UF(9)), C(col::PrevMeta), gfx::LEFT, true);
        } else {
            gfx::Text(g, L"Длительность · Источник · Качество", G::RectF(x0, my, w, UF(15)), gfx::GetFont(UF(9)), C(0x555555), gfx::LEFT, true);
        }
    };
    by += hPrev + UF(9);

    // ряды «комбо»
    float cg = UF(5);
    float w3 = (cw - cg * 2) / 3, w2 = (cw - cg) / 2;
    A.pFormat = MakePill(pad, by, w3, L"Формат");
    A.pVc     = MakePill(pad + w3 + cg, by, w3, L"Видеокодек");
    A.pAc     = MakePill(pad + (w3 + cg) * 2, by, w3, L"Аудиокодек");
    by += UF(22) + UF(6);
    A.pAudioTrack = MakePill(pad, by, w2, L"Аудиодорожка");
    A.pSubs       = MakePill(pad + w2 + cg, by, w2, L"Субтитры");
    by += UF(22) + UF(6);
    A.pRes = MakePill(pad, by, w2, L"Разрешение");
    A.pFps = MakePill(pad + w2 + cg, by, w2, L"FPS");
    by += UF(22) + UF(6);
    A.pBrowser = MakePill(pad, by, w2, L"Браузер");
    A.pFolder  = MakePill(pad + w2 + cg, by, w2, L"Папка");
    by += UF(22) + UF(6);

    std::vector<std::wstring> fmts;
    for (auto& f : kFormats) fmts.push_back(f.full);
    SetPill(A.pFormat, fmts, 1);
    for (Ctl* c : {A.pVc, A.pAc, A.pRes, A.pFps}) { SetPill(c, {L""}); c->en = false; }
    for (Ctl* c : {A.pAudioTrack, A.pSubs}) { SetPill(c, {L"—"}); c->en = false; }
    std::vector<std::wstring> brs(std::begin(kBrowsers), std::end(kBrowsers));
    SetPill(A.pBrowser, brs, 0);
    A.pFolder->t2 = A.destFolder;
    A.pFolder->mono = true;     // «путь» — сокращается по центру

    auto bindPill = [&](Ctl* c, std::function<std::vector<std::wstring>()> items, std::function<void()> after) {
        c->onClick = [c, items, after]() { PillDropdown(A.main, c, items(), c->sel, [after](int) { if (after) after(); }); };
    };
    bindPill(A.pFormat, []() { return A.pFormat->items; }, OnFormatChange);
    bindPill(A.pVc,  []() { return A.pVc->items; },  CalcFilesize);
    bindPill(A.pAc,  []() { return A.pAc->items; },  CalcFilesize);
    bindPill(A.pRes, []() { return A.pRes->items; }, CalcFilesize);
    bindPill(A.pFps, []() { return A.pFps->items; }, CalcFilesize);
    bindPill(A.pAudioTrack, []() { return A.pAudioTrack->items; }, nullptr);
    bindPill(A.pSubs, []() { return A.pSubs->items; }, nullptr);
    bindPill(A.pBrowser, []() { return A.pBrowser->items; }, []() {
        if (PV(A.pBrowser) != kCookiesItem) return;
        bool changed = false;
        if (!PickCookiesFile(&changed, A.hwnd)) {                 // отказались и файла нет — возвращаем «Без авторизации»
            A.pBrowser->sel = 0;
            RefreshPill(A.pBrowser);
            A.main->Inval(A.pBrowser);
        } else if (changed) {
            ProcessUrl(true);                                     // заново получаем предпросмотр уже с cookies
        }
    });
    A.pFolder->onClick = []() { BrowseFolder(); };

    // итоговый размер + предупреждение
    float hl = UF(13);
    Ctl* sz = p->Add(K::Custom, G::RectF(pad, by, cw, hl));
    sz->draw = [](G::Graphics& g, Ctl& c) {
        std::wstring s = A.sizeText.empty() ? L"Итоговый размер: —" : L"Итоговый размер: " + A.sizeText;
        G::Font* f = gfx::GetFont(UF(9), !A.sizeText.empty());
        float w = gfx::TextWidth(g, s, f);
        gfx::Text(g, s, G::RectF(c.r.X, c.r.Y, w + 2, c.r.Height), f, C(A.sizeText.empty() ? 0x555555 : col::Green), gfx::LEFT, false);
        if (A.lblWarn && !A.lblWarn->t1.empty()) {                  // красное пояснение — сразу после размера, в скобках
            float x = c.r.X + w + UF(6);
            gfx::Text(g, L"(" + A.lblWarn->t1 + L")", G::RectF(x, c.r.Y, c.r.Width - (x - c.r.X), c.r.Height),
                      gfx::GetFont(UF(9)), C(col::Red), gfx::LEFT, true);
        }
    };
    A.lblWarn = p->AddLabel(G::RectF(pad, by, UF(1), hl), L"", UF(9), col::Red, gfx::LEFT);
    A.lblWarn->vis = false;                                           // только хранит текст, рисует его поле размера
    by += hl + UF(8);

    // прогресс
    A.bar = p->Add(K::Progress, G::RectF(pad, by, cw, UF(9)));
    A.bar->rad = UF(4);
    A.bar->barA = col::Dim; A.bar->barB = col::Dim;
    by += UF(9) + UF(4);
    float hlab = UF(13);
    A.lblLeft = p->AddLabel(G::RectF(pad, by, cw - UF(150), hlab),
                            L"Вставьте ссылку на видео или плейлист, и нажмите «СКАЧАТЬ».", UF(9), 0x777777, gfx::LEFT);
    A.lblRight = p->AddLabel(G::RectF(pad + cw - UF(150), by, UF(150), hlab), L"", UF(9), 0x777777, gfx::RIGHT);
    by += hlab + UF(8);

    // подвал: СТОП/СКАЧАТЬ + Открыть папку
    float hf = UF(28), wOpen = UF(124);
    A.btnDl = p->AddButton(G::RectF(pad, by, cw - wOpen - UF(6), hf), L"", []() {
        if (A.downloading) CancelDownload(); else RunDownload();
    });
    A.btnDl->px = UF(10); A.btnDl->rad = UF(5); A.btnDl->iconPx = UF(11);
    RestoreDlButton();
    A.btnOpen = p->AddButton(G::RectF(pad + cw - wOpen, by, wOpen, hf), L"Открыть папку", []() {
        SHCreateDirectoryExW(nullptr, A.destFolder.c_str(), nullptr);
        ShellOpen(A.destFolder);
    });
    StyleGray(A.btnOpen, UF(9));
    A.btnOpen->icon = gfx::I_FOLDER; A.btnOpen->iconPx = UF(11);
    A.btnOpen->en = false;
    by += hf + pad;

    // подогнать высоту окна
    RECT wr;
    GetWindowRect(A.hwnd, &wr);
    SetWindowPos(A.hwnd, nullptr, 0, 0, (int)std::lround(W), (int)std::lround(by), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Drag & drop ссылки в окно (из браузера: ссылка, адресная строка, выделенный текст, .url-файл)
// ═════════════════════════════════════════════════════════════════════════════
static UINT CfUrlW() { static UINT f = RegisterClipboardFormatW(L"UniformResourceLocatorW"); return f; }

static bool DataHas(IDataObject* d, UINT cf) {
    FORMATETC f{(CLIPFORMAT)cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return d->QueryGetData(&f) == S_OK;
}

static bool DropAccepts(IDataObject* d) {
    return DataHas(d, CfUrlW()) || DataHas(d, CF_UNICODETEXT) || DataHas(d, CF_TEXT) || DataHas(d, CF_HDROP);
}

static std::wstring DataText(IDataObject* d, UINT cf, bool wide) {
    FORMATETC f{(CLIPFORMAT)cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM sm{};
    std::wstring out;
    if (d->GetData(&f, &sm) == S_OK) {
        if (sm.tymed == TYMED_HGLOBAL) {
            if (void* p = GlobalLock(sm.hGlobal)) {
                out = wide ? std::wstring((const wchar_t*)p) : W(std::string((const char*)p));
                GlobalUnlock(sm.hGlobal);
            }
        }
        ReleaseStgMedium(&sm);
    }
    return out;
}

// первая ссылка из текста; одиночное слово с точкой («youtu.be/xyz») тоже считаем ссылкой
static std::wstring FirstUrl(const std::wstring& t) {
    size_t p = t.find(L"https://");
    size_t q = t.find(L"http://");
    if (q != std::wstring::npos && (p == std::wstring::npos || q < p)) p = q;
    if (p != std::wstring::npos) {
        size_t e = t.find_first_of(L" \t\r\n\"'<>", p);
        return t.substr(p, e == std::wstring::npos ? std::wstring::npos : e - p);
    }
    std::wstring s = Trim(t);
    if (!s.empty() && s.size() < 500 && s.find_first_of(L" \t\r\n") == std::wstring::npos && s.find(L'.') != std::wstring::npos) return s;
    return L"";
}

static std::wstring UrlFromDrop(IDataObject* d) {
    std::wstring u;
    if (DataHas(d, CfUrlW())) u = FirstUrl(DataText(d, CfUrlW(), true));
    if (u.empty() && DataHas(d, CF_UNICODETEXT)) u = FirstUrl(DataText(d, CF_UNICODETEXT, true));
    if (u.empty() && DataHas(d, CF_TEXT)) u = FirstUrl(DataText(d, CF_TEXT, false));
    if (u.empty() && DataHas(d, CF_HDROP)) {                       // ярлык .url
        FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm{};
        if (d->GetData(&f, &sm) == S_OK) {
            wchar_t path[MAX_PATH * 2] = {};
            if (DragQueryFileW((HDROP)sm.hGlobal, 0, path, MAX_PATH * 2)) {
                std::wstring ps = path;
                if (ps.size() > 4 && _wcsicmp(ps.c_str() + ps.size() - 4, L".url") == 0) {
                    wchar_t buf[2048] = {};
                    GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", buf, 2048, path);
                    u = FirstUrl(buf);
                }
            }
            ReleaseStgMedium(&sm);
        }
    }
    return u;
}

class UrlDropTarget : public IDropTarget {
    LONG ref_ = 1;
    bool ok_ = false;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** o) override {
        if (r == IID_IUnknown || r == IID_IDropTarget) { *o = static_cast<IDropTarget*>(this); AddRef(); return S_OK; }
        *o = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&ref_);
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d, DWORD, POINTL, DWORD* eff) override {
        ok_ = DropAccepts(d);
        *eff = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* eff) override {
        *eff = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* d, DWORD, POINTL, DWORD* eff) override {
        std::wstring url = UrlFromDrop(d);
        *eff = url.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
        if (!url.empty()) PostUI([url]() { SetUrlText(url); });
        return S_OK;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Оконная процедура главного окна (pre-хук панели)
// ═════════════════════════════════════════════════════════════════════════════
static int g_menuClosedBy = -1;

static bool MainPre(UINT m, WPARAM w, LPARAM l, LRESULT& out) {
    switch (m) {
    case WM_APP_CALL: {
        auto* f = (std::function<void()>*)l;
        (*f)();
        delete f;
        out = 0;
        return true;
    }
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(A.hwnd, &pt);
        if (pt.y >= 0 && pt.y < (int)UF(lay::TitleH) && !A.main->HitTest(pt.x, pt.y)) { out = HTCAPTION; return true; }
        return false;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        g_menuClosedBy = -1;
        if (A.menu) {
            Ctl* hit = A.main->HitTest(x, y);
            if (hit && hit == A.menuAnchor) g_menuClosedBy = (hit == A.btnSettings) ? 0 : (hit == A.btnExtra) ? 1 : 2;
            CloseMenu();
        }
        HideScheduler();
        return false;
    }
    case WM_TIMER:
        switch (w) {
        case T_URL: KillTimer(A.hwnd, T_URL); ProcessUrl(false); out = 0; return true;
        case T_SPIN:
            A.spinPhase++;
            if (A.spinning) { A.gear->spin = A.spinPhase; A.main->Inval(A.gear); }
            out = 0; return true;
        case T_SCHED: SchedTick(); out = 0; return true;
        case T_TOAST: KillTimer(A.hwnd, T_TOAST); if (!A.inTray) TrayRemove(); out = 0; return true;
        case T_PULSE: UpdaterPulse(); out = 0; return true;
        case T_UPD:
            KillTimer(A.hwnd, T_UPD);
            if (ComponentsMissing()) ShowInstaller();      // нет 7za / yt-dlp / ffmpeg — скачать автоматически
            CheckAppUpdate();
            out = 0; return true;
        case T_BANNER: RotateBanner(); out = 0; return true;
        case T_BALLOON: KillTimer(A.hwnd, T_BALLOON); SendBalloon(); out = 0; return true;
        }
        return false;
    case WM_TRAYICON:
        if (LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == WM_LBUTTONDBLCLK) { if (A.inTray) RestoreFromTray(); }
        else if (LOWORD(l) == WM_RBUTTONUP && A.inTray) TrayMenu();
        out = 0; return true;
    case WM_CLOSE:
        DestroyWindow(A.hwnd);
        out = 0; return true;
    case WM_DESTROY:
        if (A.dlProc) { A.dlProc->Kill(); CleanupPartialFiles(A.destFolder, g_dlStartFt, true); }
        TrayRemove();
        PostQuitMessage(0);
        return false;
    }
    return false;
}

// вызывается из popups.cpp
bool MenuWasJustClosed(int which) {
    bool r = (g_menuClosedBy == which);
    g_menuClosedBy = -1;
    return r;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Точка входа
// ═════════════════════════════════════════════════════════════════════════════
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    A.hinst = hi;
    OleInitialize(nullptr);               // COM (STA) + OLE для drag & drop
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    HDC sdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);
    g_k = 1.5f * (float)dpi / 96.f;

    gfx::Init();
    Panel::RegisterClasses(hi);

    A.destFolder = LoadDestFolder();
    A.main = new Panel();

    int W = (int)std::lround(UF(lay::W)), H = (int)std::lround(UF(340));
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    A.hwnd = A.main->Create(nullptr, WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, WS_EX_APPWINDOW, (sw - W) / 2, (sh - H) / 2, W, H);
    g_mainWnd = A.hwnd;
    A.main->pre = MainPre;

    // скруглённые углы (Windows 11)
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        typedef HRESULT(WINAPI * Fn)(HWND, DWORD, LPCVOID, DWORD);
        if (auto fn = (Fn)GetProcAddress(dwm, "DwmSetWindowAttribute")) { int pref = 2; fn(A.hwnd, 33, &pref, sizeof pref); }
    }

    BuildMain();
    g_onF1 = []() { ShowHelp(); };
    A.main->onEsc = []() { CloseMenu(); CloseList(); };
    RegisterDragDrop(A.hwnd, new UrlDropTarget());
    if (A.urlEdit && A.urlEdit->h) RevokeDragDrop(A.urlEdit->h);     // чтобы бросок на поле тоже заменял ссылку, а не вставлялся в текст
    // выровнять по центру после подгонки высоты
    {
        RECT wr;
        GetWindowRect(A.hwnd, &wr);
        SetWindowPos(A.hwnd, nullptr, (sw - (wr.right - wr.left)) / 2, (sh - (wr.bottom - wr.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    OnFormatChange();
    SendMessageW(A.hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(hi, MAKEINTRESOURCEW(1)));
    SendMessageW(A.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)AppIcon());

    ShowSplash([]() {
        ShowWindow(A.hwnd, SW_SHOW);
        SetWindowPos(A.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetWindowPos(A.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(A.hwnd);
        SetTimer(A.hwnd, T_UPD, 1500, nullptr);     // проверка обновлений через 1,5 с после показа
    });

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    RevokeDragDrop(A.hwnd);
    gfx::Shutdown();
    OleUninitialize();
    return (int)msg.wParam;
}
