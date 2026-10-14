#include "gfx.h"
#include <tuple>

namespace gfx {

static ULONG_PTR g_gdipToken = 0;
static std::map<std::tuple<int, bool, bool, bool>, std::unique_ptr<G::Font>> g_fonts;
static std::map<std::pair<int, bool>, HFONT> g_hfonts;

static const wchar_t* kUi   = L"Segoe UI";
static const wchar_t* kMono = L"Consolas";

void Init() {
    G::GdiplusStartupInput in;
    G::GdiplusStartup(&g_gdipToken, &in, nullptr);
}

void Shutdown() {
    g_fonts.clear();
    for (auto& kv : g_hfonts) DeleteObject(kv.second);
    g_hfonts.clear();
    if (g_gdipToken) G::GdiplusShutdown(g_gdipToken);
    g_gdipToken = 0;
}

// Подбор доступного семейства: Segoe UI → Tahoma → Arial → системный sans-serif
static const G::FontFamily* Family(bool mono) {
    static std::unique_ptr<G::FontFamily> ui, mo;
    auto& slot = mono ? mo : ui;
    if (!slot) {
        const wchar_t* ui_c[]   = {L"Segoe UI", L"Tahoma", L"Arial", L"Liberation Sans", L"DejaVu Sans"};
        const wchar_t* mono_c[] = {L"Consolas", L"Cascadia Mono", L"Courier New", L"Liberation Mono", L"DejaVu Sans Mono"};
        for (const wchar_t* n : (mono ? mono_c : ui_c)) {
            auto f = std::make_unique<G::FontFamily>(n);
            if (f->GetLastStatus() == G::Ok && f->IsStyleAvailable(G::FontStyleRegular)) { slot = std::move(f); break; }
        }
        if (!slot) slot.reset(G::FontFamily::GenericSansSerif()->Clone());
    }
    return slot.get();
}

G::Font* GetFont(float px, bool bold, bool mono, bool underline) {
    int key = (int)std::lround(px * 100);
    auto k = std::make_tuple(key, bold, mono, underline);
    auto it = g_fonts.find(k);
    if (it != g_fonts.end()) return it->second.get();
    int style = (bold ? G::FontStyleBold : G::FontStyleRegular) | (underline ? G::FontStyleUnderline : 0);
    const G::FontFamily* fam = Family(mono);
    if (!fam->IsStyleAvailable(style)) style &= ~G::FontStyleBold;
    auto f = std::make_unique<G::Font>(fam, px, style, G::UnitPixel);
    G::Font* raw = f.get();
    g_fonts[k] = std::move(f);
    return raw;
}

HFONT GetHFont(float px, bool mono) {
    auto k = std::make_pair((int)std::lround(px), mono);
    auto it = g_hfonts.find(k);
    if (it != g_hfonts.end()) return it->second;
    HFONT f = CreateFontW(-(int)std::lround(px), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, mono ? kMono : kUi);
    g_hfonts[k] = f;
    return f;
}

void AddRR(G::GraphicsPath& p, const G::RectF& r, float rad) {
    float d = std::min(rad * 2.f, std::min(r.Width, r.Height));
    if (d <= 0.5f) { p.AddRectangle(r); return; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

void FillRR(G::Graphics& g, const G::RectF& r, float rad, G::Color c) {
    G::SolidBrush b(c);
    FillRRBrush(g, r, rad, b);
}

void FillRRBrush(G::Graphics& g, const G::RectF& r, float rad, G::Brush& b) {
    G::GraphicsPath p;
    AddRR(p, r, rad);
    g.FillPath(&b, &p);
}

void StrokeRR(G::Graphics& g, const G::RectF& r, float rad, G::Color c, float w) {
    G::GraphicsPath p;
    // обводка внутри прямоугольника, как CSS border (box-sizing: border-box)
    G::RectF rr(r.X + w / 2, r.Y + w / 2, r.Width - w, r.Height - w);
    AddRR(p, rr, std::max(0.f, rad - w / 2));
    G::Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

static void MakeFmt(G::StringFormat& sf, int ha, bool ellipsis, bool path) {
    sf.SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsNoFitBlackBox);
    sf.SetAlignment(ha == LEFT ? G::StringAlignmentNear : ha == CENTER ? G::StringAlignmentCenter : G::StringAlignmentFar);
    sf.SetLineAlignment(G::StringAlignmentCenter);
    sf.SetTrimming(ellipsis ? (path ? G::StringTrimmingEllipsisPath : G::StringTrimmingEllipsisCharacter)
                            : G::StringTrimmingNone);
}

void Text(G::Graphics& g, const std::wstring& s, const G::RectF& r, G::Font* f, G::Color c,
          int ha, bool ellipsis, bool pathEllipsis) {
    if (s.empty()) return;
    G::StringFormat sf(G::StringFormat::GenericTypographic());
    MakeFmt(sf, ha, ellipsis, pathEllipsis);
    G::SolidBrush br(c);
    g.DrawString(s.c_str(), -1, f, r, &sf, &br);
}

float TextWidth(G::Graphics& g, const std::wstring& s, G::Font* f) {
    if (s.empty()) return 0;
    G::StringFormat sf(G::StringFormat::GenericTypographic());
    sf.SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsMeasureTrailingSpaces);
    G::RectF box;
    g.MeasureString(s.c_str(), -1, f, G::PointF(0, 0), &sf, &box);
    return box.Width;
}

float TextHeight(G::Graphics& g, G::Font* f) {
    G::StringFormat sf(G::StringFormat::GenericTypographic());
    G::RectF box;
    g.MeasureString(L"Ay", -1, f, G::PointF(0, 0), &sf, &box);
    return box.Height;
}

void DrawIcon(G::Graphics& g, int icon, const G::RectF& r, G::Color c, float stroke) {
    float s  = std::min(r.Width, r.Height);
    float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
    float x0 = cx - s / 2, y0 = cy - s / 2;
    auto X = [&](float v) { return x0 + v * s; };
    auto Y = [&](float v) { return y0 + v * s; };
    G::Pen pen(c, stroke);
    pen.SetStartCap(G::LineCapRound);
    pen.SetEndCap(G::LineCapRound);
    pen.SetLineJoin(G::LineJoinRound);
    G::SolidBrush br(c);

    switch (icon) {
    case I_CLOSE:
        g.DrawLine(&pen, X(.2f), Y(.2f), X(.8f), Y(.8f));
        g.DrawLine(&pen, X(.8f), Y(.2f), X(.2f), Y(.8f));
        break;
    case I_MINUS:
        g.DrawLine(&pen, X(.15f), Y(.5f), X(.85f), Y(.5f));
        break;
    case I_ARROW_DOWN:
        g.DrawLine(&pen, X(.5f), Y(.12f), X(.5f), Y(.86f));
        { G::PointF p[3] = { {X(.2f), Y(.58f)}, {X(.5f), Y(.88f)}, {X(.8f), Y(.58f)} }; g.DrawLines(&pen, p, 3); }
        break;
    case I_STOP:
        FillRR(g, G::RectF(X(.2f), Y(.2f), s * .6f, s * .6f), s * .08f, c);
        break;
    case I_FOLDER:
        FillRR(g, G::RectF(X(.06f), Y(.17f), s * .40f, s * .26f), s * .07f, c);
        FillRR(g, G::RectF(X(.06f), Y(.30f), s * .88f, s * .52f), s * .08f, c);
        break;
    case I_DOWNLOAD:
        g.DrawLine(&pen, X(.5f), Y(.1f), X(.5f), Y(.62f));
        { G::PointF p[3] = { {X(.26f), Y(.40f)}, {X(.5f), Y(.64f)}, {X(.74f), Y(.40f)} }; g.DrawLines(&pen, p, 3); }
        g.DrawLine(&pen, X(.18f), Y(.84f), X(.82f), Y(.84f));
        break;
    case I_CLOCK:
        g.DrawEllipse(&pen, X(.1f), Y(.1f), s * .8f, s * .8f);
        g.DrawLine(&pen, X(.5f), Y(.5f), X(.5f), Y(.26f));
        g.DrawLine(&pen, X(.5f), Y(.5f), X(.68f), Y(.58f));
        break;
    case I_GEAR: {
        G::Pen ring(c, s * .15f);
        g.DrawEllipse(&ring, X(.5f - .22f), Y(.5f - .22f), s * .44f, s * .44f);
        G::Pen tooth(c, s * .17f);
        tooth.SetStartCap(G::LineCapFlat);
        tooth.SetEndCap(G::LineCapFlat);
        for (int i = 0; i < 8; i++) {
            float a = (float)(i * 3.14159265358979 / 4);
            float r1 = s * .27f, r2 = s * .46f;
            g.DrawLine(&tooth, cx + std::cos(a) * r1, cy + std::sin(a) * r1,
                               cx + std::cos(a) * r2, cy + std::sin(a) * r2);
        }
        break;
    }
    case I_PLAY: {
        G::PointF p[3] = { {X(.28f), Y(.14f)}, {X(.28f), Y(.86f)}, {X(.86f), Y(.5f)} };
        g.FillPolygon(&br, p, 3);
        break;
    }
    case I_CALENDAR:
        StrokeRR(g, G::RectF(X(.12f), Y(.2f), s * .76f, s * .68f), s * .1f, c, stroke);
        g.DrawLine(&pen, X(.14f), Y(.42f), X(.86f), Y(.42f));
        g.DrawLine(&pen, X(.32f), Y(.1f), X(.32f), Y(.28f));
        g.DrawLine(&pen, X(.68f), Y(.1f), X(.68f), Y(.28f));
        break;
    case I_PLUS:
        g.DrawLine(&pen, X(.5f), Y(.18f), X(.5f), Y(.82f));
        g.DrawLine(&pen, X(.18f), Y(.5f), X(.82f), Y(.5f));
        break;
    case I_CHECK: {
        G::PointF p[3] = { {X(.2f), Y(.52f)}, {X(.42f), Y(.74f)}, {X(.82f), Y(.28f)} };
        g.DrawLines(&pen, p, 3);
        break;
    }
    case I_CHEVRON: {
        G::PointF p[3] = { {X(.22f), Y(.38f)}, {X(.5f), Y(.66f)}, {X(.78f), Y(.38f)} };
        g.DrawLines(&pen, p, 3);
        break;
    }
    case I_QUESTION:
        Text(g, L"?", r, GetFont(s * 1.05f, true), c, CENTER, false);
        break;
    }
}

void Spinner(G::Graphics& g, const G::RectF& r, G::Color c, int phase, float stroke) {
    float s = std::min(r.Width, r.Height) * 0.62f;
    float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
    G::Pen pen(c, stroke);
    pen.SetStartCap(G::LineCapRound);
    pen.SetEndCap(G::LineCapRound);
    g.DrawArc(&pen, cx - s / 2, cy - s / 2, s, s, (float)((phase * 36) % 360), 250.f);
}

std::unique_ptr<G::Bitmap> LoadBitmapMem(const void* data, size_t n) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!h) return nullptr;
    void* p = GlobalLock(h);
    memcpy(p, data, n);
    GlobalUnlock(h);
    IStream* st = nullptr;
    if (CreateStreamOnHGlobal(h, TRUE, &st) != S_OK) { GlobalFree(h); return nullptr; }
    std::unique_ptr<G::Bitmap> out;
    {
        G::Bitmap src(st);
        if (src.GetLastStatus() == G::Ok && src.GetWidth() > 0) {
            out = std::make_unique<G::Bitmap>((INT)src.GetWidth(), (INT)src.GetHeight(), PixelFormat32bppPARGB);
            G::Graphics g(out.get());
            g.DrawImage(&src, 0, 0, (INT)src.GetWidth(), (INT)src.GetHeight());
        }
    }
    st->Release();
    return out;
}

std::unique_ptr<G::Bitmap> LoadBitmapFile(const std::wstring& path) {
    std::string data = ReadFileA(path);
    if (data.empty()) return nullptr;
    return LoadBitmapMem(data.data(), data.size());
}

std::unique_ptr<G::Bitmap> LoadBitmapRes(int id, const std::wstring& fallbackFile) {
    if (HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA)) {
        if (HGLOBAL h = LoadResource(nullptr, r)) {
            if (const void* p = LockResource(h)) {
                auto bmp = LoadBitmapMem(p, SizeofResource(nullptr, r));
                if (bmp) return bmp;
            }
        }
    }
    return LoadBitmapFile(ResPath(fallbackFile));
}

}  // namespace gfx
