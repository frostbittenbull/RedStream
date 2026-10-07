#include "app.h"
#include <set>
#include <shlobj.h>

App A;

enum { T_URL = 1, T_SPIN, T_SCHED, T_TOAST, T_PULSE, T_UPD };

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
    {"av01", "AV1"}, {"vp9", "VP9"}, {"avc1", "H.264"}, {"h264", "H.264"}, {"H264", "H.264"},
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
                                     L"Edge", L"Firefox", L"Brave", L"Vivaldi", L"Whale"};

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
static double SizeOf(const json& f) {
    double v = jn(f, "filesize");
    return v != 0 ? v : jn(f, "filesize_approx");
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
    if (!A.hasFormats) { SetSizeText(L""); A.lblWarn->t1 = L""; A.main->Inval(A.lblWarn); return; }
    std::string selExt = U8(FormatExt());
    std::string vcDisp = PVa(A.pVc), acDisp = PVa(A.pAc), selRes = PVa(A.pRes), selFps = PVa(A.pFps);
    std::string selVc = vcDisp != "-" ? MapGet(VC_RAW, vcDisp, LowerA(vcDisp)) : "-";
    std::string selAc = acDisp != "-" ? MapGet(AC_RAW, acDisp, LowerA(acDisp)) : "-";
    bool audioOnly = (selExt == "mp3" || selExt == "m4a" || selExt == "opus");

    double vSize = 0, aSize = 0;
    const json* bestV = nullptr;
    const json* bestA = nullptr;
    std::vector<const json*> validV;

    if (!audioOnly) {
        static const std::map<std::string, std::vector<std::string>> ALIAS = {
            {"avc1", {"avc1", "h264", "h.264"}}, {"hvc1", {"hvc1", "hev1", "h265", "h.265", "bytevc1"}},
            {"vp9", {"vp9"}}, {"av01", {"av01"}}};
        std::vector<std::string> variants = {selVc};
        auto al = ALIAS.find(selVc);
        if (al != ALIAS.end()) variants = al->second;
        int resH = 0;
        {
            std::smatch m;
            if (selRes != "-" && !selRes.empty()) {
                if (std::regex_search(selRes, m, std::regex("x(\\d+)"))) resH = std::stoi(m[1]);
                else if (std::regex_search(selRes, m, std::regex("(\\d+)"))) resH = std::stoi(m[1]);
            }
        }
        for (auto& f : A.rawFormats) {
            std::string vc = js(f, "vcodec", "none");
            if (vc == "none" || vc == "images") continue;
            std::string lvc = LowerA(vc);
            bool vcMatch = false;
            for (auto& a : variants) if (lvc.find(a) != std::string::npos) vcMatch = true;
            int fh = (int)jn(f, "height", 0);
            bool resMatch = (resH == 0 || fh == resH);
            bool fpsMatch = true;
            if (!(selFps == "-" || selFps.empty())) {
                try {
                    auto it = f.find("fps");
                    if (it != f.end() && it->is_number()) fpsMatch = ((int)it->get<double>() == std::stoi(selFps));
                } catch (...) { fpsMatch = true; }
            }
            if (vcMatch && resMatch && fpsMatch) validV.push_back(&f);
        }
        if (!validV.empty()) {
            bestV = *std::max_element(validV.begin(), validV.end(),
                                      [](const json* a, const json* b) { return Weight(*a) < Weight(*b); });
            vSize = SizeOf(*bestV);
        }
    }

    std::vector<const json*> validA;
    for (auto& f : A.rawFormats) {
        std::string ac = js(f, "acodec", "none"), vc = js(f, "vcodec", "none");
        if (vc == "none" && ac != "none" && ac.find(selAc) != std::string::npos) validA.push_back(&f);
    }
    if (!validA.empty()) {
        bestA = *std::max_element(validA.begin(), validA.end(),
                                  [](const json* a, const json* b) { return Weight(*a) < Weight(*b); });
        aSize = SizeOf(*bestA);
    }

    double total = 0;
    if (audioOnly) total = aSize;
    else if (bestV && js(*bestV, "acodec", "none") != "none") total = vSize;
    else total = vSize + aSize;

    std::wstring av1Warn;
    if (!audioOnly && selVc == "av01") {
        bool av1 = false;
        for (auto& f : A.rawFormats) if (js(f, "vcodec").rfind("av01", 0) == 0) av1 = true;
        if (!av1 && validV.empty()) {
            static const std::vector<std::string> PREF = {"vp9", "avc1", "hvc1", "hev1"};
            auto prefIdx = [&](const json& f) {
                std::string v = js(f, "vcodec");
                v = v.substr(0, v.find('.'));
                for (size_t i = 0; i < PREF.size(); i++) if (PREF[i] == v) return -(int)i;
                return -999;
            };
            std::vector<const json*> fb;
            for (auto& f : A.rawFormats) {
                std::string vc = js(f, "vcodec", "none");
                if (vc == "none" || vc == "images") continue;
                if (js(f, "resolution").find(selRes) == std::string::npos) continue;
                if (!(selFps.empty() || selFps == "-")) {
                    auto it = f.find("fps");
                    std::string fs = (it != f.end() && it->is_number()) ? std::to_string((long long)it->get<double>()) : "";
                    if (fs != selFps) continue;
                }
                fb.push_back(&f);
            }
            if (!fb.empty()) {
                bestV = *std::max_element(fb.begin(), fb.end(), [&](const json* a, const json* b) {
                    int pa = prefIdx(*a), pb = prefIdx(*b);
                    if (pa != pb) return pa < pb;
                    return Weight(*a) < Weight(*b);
                });
                vSize = SizeOf(*bestV);
                total = (js(*bestV, "acodec", "none") != "none") ? vSize : vSize + aSize;
                std::string actual = js(*bestV, "vcodec");
                actual = actual.substr(0, actual.find('.'));
                av1Warn = L"AV1 недоступен, вместо этого будет скачан " + W(MapGet(VC_DISP, actual, UpperA(actual)));
            }
        }
    }

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
    if (audioOnly && (selExt == "opus" || selExt == "mp3")) {
        std::string wanted = (selAc.empty() || selAc == "-") ? "opus" : selAc;
        std::vector<std::string> prefixes = wanted == "mp4a" ? std::vector<std::string>{"mp4a", "aac"}
                                                              : std::vector<std::string>{wanted};
        bool avail = false;
        for (auto& f : A.rawFormats) {
            if (js(f, "vcodec", "none") != "none") continue;
            std::string ac = js(f, "acodec");
            for (auto& p : prefixes) if (ac.rfind(p, 0) == 0) avail = true;
        }
        if (!avail) {
            std::string real;
            for (auto& f : A.rawFormats) {
                std::string ac = js(f, "acodec", "none");
                if (ac != "none") {
                    ac = ac.substr(0, ac.find('.'));
                    real = MapGet(AC_DISP, ac, UpperA(ac));
                    break;
                }
            }
            std::string name = MapGet(AC_DISP, wanted, UpperA(wanted));
            audioWarn = real.empty() ? W(name) + L" недоступен"
                                     : W(name) + L" недоступен, вместо этого будет использован " + W(real);
        }
    }
    A.lblWarn->t1 = av1Warn.empty() ? audioWarn : av1Warn;
    A.main->Inval(A.lblWarn);
}

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

static void ApplyPreviewFailure() {
    StopSpinner();
    SetGearEnabled(false);
}

static void FetchPreview(const std::wstring& url) {
    int gen = ++A.prevGen;
    A.prev = Preview{};
    A.thumb.reset();
    SetGearEnabled(false);
    StartSpinner();
    A.main->Inval(A.prevCard);

    std::thread([url, gen]() {
        std::vector<std::wstring> args = {ResPath(L"yt-dlp.exe"), L"--dump-json", L"--no-playlist"};
        std::wstring proxy = GetProxyUrl();
        if (!proxy.empty()) { args.push_back(L"--proxy"); args.push_back(proxy); }
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
        std::stable_sort(vRaw.begin(), vRaw.end(), order({{"av01", 0}, {"vp9", 1}, {"avc1", 2}}));
        std::stable_sort(aRaw.begin(), aRaw.end(), order({{"opus", 0}, {"mp4a", 1}}));

        std::vector<std::wstring> vcs, acs, ress, fpss;
        for (auto& v : vRaw) vcs.push_back(W(MapGet(VC_DISP, v, UpperA(v))));
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

        // миниатюра
        std::unique_ptr<G::Bitmap> bmp;
        if (!pv.thumbUrl.empty()) {
            std::string raw;
            if (HttpGet(FixThumbUrl(pv.thumbUrl), raw, 8000)) bmp = gfx::LoadBitmapMem(raw.data(), raw.size());
        }
        auto* bmpRaw = bmp.release();

        PostUI([gen, pv, formats, vcs, acs, ress, fpss, bmpRaw]() mutable {
            if (gen != A.prevGen) { delete bmpRaw; return; }
            A.prev = pv;
            A.rawFormats = formats;
            A.hasFormats = !formats.empty();
            A.thumb.reset(bmpRaw);
            A.savedVcs = vcs; A.savedAcs = acs; A.savedRess = ress; A.savedFpss = fpss;
            OnFormatChange();
            StopSpinner();
            SetGearEnabled(true);
            A.main->Inval(A.prevCard);
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
    FetchPreview(url);
}

static void PasteUrl() {
    std::wstring text;
    if (OpenClipboard(A.hwnd)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* p = (const wchar_t*)GlobalLock(h)) { text = p; GlobalUnlock(h); }
        }
        CloseClipboard();
    }
    text = Trim(text);
    if (text.empty()) return;
    if (!StartsWith(text, L"http://") && !StartsWith(text, L"https://")) text = L"https://" + text;
    A.main->SetText(A.urlEdit, text, true);
    SetFocus(A.urlEdit->h);
    SendMessageW(A.urlEdit->h, EM_SETSEL, (WPARAM)text.size(), (LPARAM)text.size());
    KillTimer(A.hwnd, T_URL);
    A.prevUrl = text;
    FetchPreview(text);
}

void ClearFields() {
    A.prevGen++;
    A.main->SetText(A.urlEdit, L"", true);
    A.prevUrl.clear();
    A.prev = Preview{};
    A.thumb.reset();
    A.rawFormats = json::array();
    A.hasFormats = false;
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
    std::string title;
};

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
    if (line.find("[download] Destination:") != std::string::npos) {
        std::string lower = LowerA(line);
        size_t p;
        while ((p = lower.find(".part")) != std::string::npos) lower.erase(p, 5);
        auto ends = [&](const char* e) { return lower.size() >= strlen(e) && lower.compare(lower.size() - strlen(e), strlen(e), e) == 0; };
        if (!audioFmt) {
            for (const char* e : {".mp4", ".mkv", ".webm", ".m4v", ".avi", ".mov", ".ts"}) if (ends(e)) { st.videoStream = true; break; }
            for (const char* e : {".opus", ".m4a", ".mp3", ".aac", ".ogg", ".wav"}) if (ends(e)) { st.videoStream = false; break; }
        }
        std::smatch m;
        std::string l2 = line;
        while (!l2.empty() && (l2.back() == '\r' || l2.back() == ' ')) l2.pop_back();
        if (std::regex_search(l2, m, std::regex("\\[download\\] Destination: .+?[/\\\\](.+?)(\\.\\w+)?(\\.\\w+)?$")))
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
            std::wstring label = audioFmt ? L"Загрузка" : (st.videoStream ? L"Загрузка видеопотока" : L"Загрузка аудиопотока");
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

static std::vector<std::wstring> BuildDownloadArgs() {
    std::wstring browser = PV(A.pBrowser);
    std::string bkey = "none";
    static const std::map<std::wstring, std::string> bmap = {
        {L"Без авторизации", "none"}, {L"Chrome", "chrome"}, {L"Chromium", "chromium"}, {L"Opera", "opera"},
        {L"Opera GX", "opera_gx"}, {L"Edge", "edge"}, {L"Firefox", "firefox"}, {L"Brave", "brave"},
        {L"Vivaldi", "vivaldi"}, {L"Whale", "whale"}};
    auto bi = bmap.find(browser);
    if (bi != bmap.end()) bkey = bi->second;

    std::vector<std::wstring> cookie;
    if (bkey != "none") {
        if (bkey == "opera_gx") {
            wchar_t ad[MAX_PATH] = {};
            GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH);
            cookie = {L"--cookies-from-browser", L"opera:" + std::wstring(ad) + L"\\Opera Software\\Opera GX Stable"};
        } else {
            cookie = {L"--cookies-from-browser", W(bkey)};
        }
    }

    std::string selExt = U8(FormatExt());
    std::string vcDisp = PVa(A.pVc), acDisp = PVa(A.pAc), selRes = PVa(A.pRes), selFps = PVa(A.pFps);
    std::string selVc = vcDisp != "-" ? MapGet(VC_RAW, vcDisp, LowerA(vcDisp)) : "-";
    std::string selAc = acDisp != "-" ? MapGet(AC_RAW, acDisp, LowerA(acDisp)) : "-";
    bool audio = (selExt == "mp3" || selExt == "m4a" || selExt == "opus");

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
            fmt = {"-f", "bestaudio/best", "-x", "--audio-format", selExt};
        } else {
            std::string acCond = (!selAc.empty() && selAc != "-") ? "[acodec^=" + selAc + "]" : "";
            fmt = {"-f", "bestaudio" + acCond + "/bestaudio", "-x", "--audio-format", selExt};
        }
        if (selExt == "mp3") { fmt.push_back("--audio-quality"); fmt.push_back("0"); }
    } else {
        std::vector<std::string> vCond;
        if (!selRes.empty() && selRes != "-") {
            std::smatch m;
            if (std::regex_search(selRes, m, std::regex("x(\\d+)"))) vCond.push_back("height<=" + m[1].str());
            else if (std::regex_search(selRes, m, std::regex("\\d+"))) vCond.push_back("height<=" + m[0].str());
        }
        if (!selFps.empty() && selFps != "-") vCond.push_back("fps<=" + selFps);
        std::string aCond = (!selAc.empty() && selAc != "-") ? "[acodec^=" + selAc + "]" : "";
        bool av1 = false;
        for (auto& f : A.rawFormats) if (js(f, "vcodec").rfind("av01", 0) == 0) av1 = true;

        auto joinCond = [](const std::vector<std::string>& c) {
            if (c.empty()) return std::string();
            std::string s = "[";
            for (size_t i = 0; i < c.size(); i++) { if (i) s += "]["; s += c[i]; }
            return s + "]";
        };
        if (selVc == "av01" && !av1) {
            fmt = {"-f", "bestvideo" + joinCond(vCond) + "+bestaudio" + aCond + "/bestvideo+bestaudio/best", "-S", "vcodec:av01"};
        } else {
            if (!selVc.empty() && selVc != "-") vCond.push_back("vcodec^=" + selVc);
            fmt = {"-f", "bestvideo" + joinCond(vCond) + "+bestaudio" + aCond + "/bestvideo+bestaudio/best"};
        }
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
    cmd.push_back(L"-o");
    cmd.push_back(A.destFolder + L"\\%(title)s.%(ext)s");
    return cmd;
}

static void RunDownload() {
    std::wstring url = Trim(A.main->GetText(A.urlEdit));
    if (url.empty()) { ReportError(L"Вставьте ссылку на видео или плейлист!"); return; }

    SHCreateDirectoryExW(nullptr, A.destFolder.c_str(), nullptr);
    std::vector<std::wstring> cmd = BuildDownloadArgs();
    cmd.push_back(url);

    SetStopButton();
    A.btnOpen->en = false;
    A.main->Inval(A.btnOpen);
    SetBar(0, col::Amber, col::Amber);
    SetStatus(L"Подключение и анализ…", 0xffffff);

    int gen = ++A.dlGen;
    std::wstring ext = FormatExt();
    bool audioFmt = IsAudioExt(ext);
    auto proc = std::make_shared<Proc>();
    A.dlProc = proc;
    std::wstring dlUrl = url;

    std::thread([proc, cmd, gen, ext, audioFmt, dlUrl]() {
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
                if (!u.empty()) SaveHistory(u);
                std::wstring title = A.prev.valid ? A.prev.title : L"";
                if (title.empty()) title = W(st.title);
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

static void BrowseFolder() {
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

void ShowToast(const std::wstring& title, const std::wstring& msg) {
    TrayAdd();
    NOTIFYICONDATAW n = TrayData();
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = AppIcon();
    wcsncpy(n.szInfoTitle, title.c_str(), 63);
    wcsncpy(n.szInfo, msg.c_str(), 255);
    Shell_NotifyIconW(NIM_MODIFY, &n);
    if (!A.inTray) SetTimer(A.hwnd, T_TOAST, 10000, nullptr);
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
static void ShowBanner(int kind, const std::wstring& pre, const std::wstring& link, const std::wstring& post) {
    uint32_t bg, fg, lk, lkH;
    if (kind == 0)      { bg = 0xffaa00; fg = 0x1e1e1e; lk = 0xff0000; lkH = 0xcc0000; }
    else if (kind == 1) { bg = 0x1a6b1a; fg = 0xffffff; lk = 0x00ff88; lkH = 0x88ffbb; }
    else                { bg = 0x1a5a8a; fg = 0xffffff; lk = 0x88ddff; lkH = 0xc0eeff; }

    int idx = (int)A.banners.size();
    float y = UF(lay::TitleH + lay::ToolbarH) + idx * UF(lay::BannerH);
    float W = UF(lay::W);

    HDC dc = GetDC(A.hwnd);
    G::Graphics g(dc);
    G::Font* f = gfx::GetFont(UF(10));
    G::Font* fb = gfx::GetFont(UF(10), true, false, true);
    float w1 = gfx::TextWidth(g, pre, f), w2 = gfx::TextWidth(g, link, fb), w3 = gfx::TextWidth(g, post, f);
    ReleaseDC(A.hwnd, dc);
    float total = w1 + w2 + w3, x0 = (W - total) / 2;

    Ctl* bgc = A.main->Add(K::Custom, G::RectF(0, y, W, UF(lay::BannerH)));
    bgc->body = false;
    bgc->draw = [=](G::Graphics& gg, Ctl& c) {
        G::SolidBrush b(C(bg));
        gg.FillRectangle(&b, c.r);
        gfx::Text(gg, pre, G::RectF(x0, c.r.Y, w1 + 2, c.r.Height), gfx::GetFont(UF(10)), C(fg), gfx::LEFT, false);
        gfx::Text(gg, post, G::RectF(x0 + w1 + w2, c.r.Y, w3 + 2, c.r.Height), gfx::GetFont(UF(10)), C(fg), gfx::LEFT, false);
    };
    Ctl* lc = A.main->AddButton(G::RectF(x0 + w1 - UF(4), y, w2 + UF(8), UF(lay::BannerH)), link, nullptr);
    lc->body = false;
    lc->px = UF(10); lc->bold = true; lc->ul = true; lc->fg = lk; lc->fgH = lkH;
    lc->ha = gfx::CENTER;
    int bi = idx;
    lc->onClick = [kind, bi]() {
        if (kind == 0) { ShellOpen(GITHUB_RELEASES); return; }
        ShowUpdater();
    };
    A.banners.push_back({kind, pre, link, post, lc});

    A.main->ShiftBody((int)UF(lay::BannerH));
    RECT wr;
    GetWindowRect(A.hwnd, &wr);
    SetWindowPos(A.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top + (int)UF(lay::BannerH),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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
        if (latest != current) {
            PostUI([=]() {
                ShowBanner(2, L"ffmpeg устарел (" + W(current) + L" → " + W(latest) + L"). Нажмите ", L"обновить", L", чтобы продолжить.");
            });
        }
    }).detach();
}

static void CheckYtdlpUpdate() {
    std::thread([]() {
        ProcResult r = RunCapture({ResPath(L"yt-dlp.exe"), L"--version"}, 10000);
        std::string current = TrimA(r.out);
        if (r.started && !current.empty()) {
            std::string body;
            if (HttpGet(L"https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest", body, 10000)) {
                try {
                    std::string latest = TrimA(json::parse(body).value("tag_name", ""));
                    if (!latest.empty() && latest != current) {
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

// true, если версия a строго новее версии b (сравнение по числовым компонентам «1.2.3»)
static bool VersionNewer(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
        std::vector<long> v;
        std::string cur;
        for (char c : s + ".") {
            if (isdigit((unsigned char)c)) cur += c;
            else { if (!cur.empty()) { v.push_back(std::atol(cur.c_str())); cur.clear(); } if (c != '.') break; }
        }
        return v;
    };
    auto va = parts(a), vb = parts(b);
    size_t n = std::max(va.size(), vb.size());
    va.resize(n, 0); vb.resize(n, 0);
    return std::lexicographical_compare(vb.begin(), vb.end(), va.begin(), va.end());
}

static void CheckAppUpdate() {
    std::thread([]() {
        std::string body;
        if (HttpGet(GITHUB_API_URL, body, 5000)) {
            try {
                std::string tag = json::parse(body).value("tag_name", "");
                while (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
                if (!tag.empty() && VersionNewer(tag, U8(APP_VERSION))) {
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
                             L"RedStream: Video Downloader v" APP_VERSION, UF(10), col::TitleText, gfx::CENTER);
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
    A.pVc     = MakePill(pad + w3 + cg, by, w3, L"Кодек");
    A.pAc     = MakePill(pad + (w3 + cg) * 2, by, w3, L"Аудиокодек");
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
    bindPill(A.pBrowser, []() { return A.pBrowser->items; }, nullptr);
    A.pFolder->onClick = []() { BrowseFolder(); };

    // итоговый размер + предупреждение
    float hl = UF(13);
    Ctl* sz = p->Add(K::Custom, G::RectF(pad, by, UF(180), hl));
    sz->draw = [](G::Graphics& g, Ctl& c) {
        std::wstring s = A.sizeText.empty() ? L"Итоговый размер: —" : L"Итоговый размер: " + A.sizeText;
        gfx::Text(g, s, c.r, gfx::GetFont(UF(9), !A.sizeText.empty()), C(A.sizeText.empty() ? 0x555555 : col::Green), gfx::LEFT, true);
    };
    A.lblWarn = p->AddLabel(G::RectF(pad + UF(184), by, cw - UF(184), hl), L"", UF(9), col::Red, gfx::LEFT);
    by += hl + UF(8);

    // прогресс
    A.bar = p->Add(K::Progress, G::RectF(pad, by, cw, UF(9)));
    A.bar->rad = UF(4);
    A.bar->barA = col::Dim; A.bar->barB = col::Dim;
    by += UF(9) + UF(4);
    float hlab = UF(13);
    A.lblLeft = p->AddLabel(G::RectF(pad, by, cw - UF(100), hlab),
                            L"Вставьте ссылку на видео или плейлист, и нажмите «СКАЧАТЬ».", UF(9), 0x777777, gfx::LEFT);
    A.lblRight = p->AddLabel(G::RectF(pad + cw - UF(100), by, UF(100), hlab), L"", UF(9), 0x777777, gfx::RIGHT);
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
        case T_UPD: KillTimer(A.hwnd, T_UPD); CheckAppUpdate(); out = 0; return true;
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
        if (A.dlProc) A.dlProc->Kill();
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
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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
    gfx::Shutdown();
    return (int)msg.wParam;
}
