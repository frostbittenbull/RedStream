#include "ui.h"

static HINSTANCE g_hi = nullptr;
static const wchar_t* kPanelClass = L"RSPanel";
static const wchar_t* kListClass  = L"RSList";

// ─── кэш кистей для WM_CTLCOLOR* ───────────────────────────────────────────────
static HBRUSH BrushFor(uint32_t rgb) {
    static std::map<uint32_t, HBRUSH> cache;
    auto it = cache.find(rgb);
    if (it != cache.end()) return it->second;
    HBRUSH b = CreateSolidBrush(CR(rgb));
    cache[rgb] = b;
    return b;
}

// ─── подкласс EDIT: Ctrl+A/C/V/X/Z по коду клавиши (любая раскладка) ──────────
static LRESULT CALLBACK EditSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) {
    switch (m) {
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            switch (w) {
            case 'A': SendMessageW(h, EM_SETSEL, 0, -1); return 0;
            case 'C': SendMessageW(h, WM_COPY, 0, 0);    return 0;
            case 'V': SendMessageW(h, WM_PASTE, 0, 0);   return 0;
            case 'X': SendMessageW(h, WM_CUT, 0, 0);     return 0;
            case 'Z': SendMessageW(h, EM_UNDO, 0, 0);    return 0;
            }
        }
        break;
    case WM_CHAR:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w < 32 && w != VK_BACK) return 0;
        if (w == VK_RETURN || w == VK_ESCAPE) return 0;     // без «бипа»
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, EditSub, id);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Рисование контролов
// ═════════════════════════════════════════════════════════════════════════════
static uint32_t Pick(bool en, bool hover, bool down, uint32_t n, uint32_t h, uint32_t d) {
    if (!en) return d != NOCOL ? d : n;
    if ((hover || down) && h != NOCOL) return h;
    return n;
}

void PaintCtl(G::Graphics& g, Ctl& c) {
    const float bw = std::max(1.f, UF(1));
    switch (c.kind) {
    case K::Custom:
        if (c.draw) c.draw(g, c);
        break;

    case K::Label: {
        G::Font* f = gfx::GetFont(c.px, c.bold, c.mono, c.ul);
        uint32_t fc = c.en ? c.fg : c.fgD;
        if (c.wrap) {
            G::StringFormat sf(G::StringFormat::GenericTypographic());
            sf.SetFormatFlags(G::StringFormatFlagsNoFitBlackBox);
            sf.SetAlignment(c.ha == gfx::LEFT ? G::StringAlignmentNear : c.ha == gfx::CENTER ? G::StringAlignmentCenter : G::StringAlignmentFar);
            sf.SetLineAlignment(G::StringAlignmentNear);
            G::SolidBrush br(C(fc));
            g.DrawString(c.t1.c_str(), -1, f, c.r, &sf, &br);
        } else {
            gfx::Text(g, c.t1, c.r, f, C(fc), c.ha, true);
        }
        break;
    }

    case K::Button: {
        uint32_t bgc = Pick(c.en, c.hover, c.down, c.bg, c.bgH, c.bgD);
        uint32_t bdc = Pick(c.en, c.hover, c.down, c.bd, c.bdH, c.bdD);
        uint32_t fgc = c.en ? ((c.hover || c.down) && c.fgH != NOCOL ? c.fgH : c.fg) : c.fgD;
        if (bgc != NOCOL) gfx::FillRR(g, c.r, c.rad, C(bgc));
        if (bdc != NOCOL) gfx::StrokeRR(g, c.r, c.rad, C(bdc), bw);
        G::Font* f = gfx::GetFont(c.px, c.bold, c.mono, c.ul);
        float ip  = (c.icon || c.spin >= 0) ? (c.iconPx > 0 ? c.iconPx : c.px * 1.15f) : 0;
        float gap = (ip > 0 && !c.t1.empty()) ? c.px * 0.55f : 0;
        float tw  = gfx::TextWidth(g, c.t1, f);
        float total = ip + gap + tw;
        float x = c.ha == gfx::LEFT ? c.r.X + UF(8) : c.r.X + (c.r.Width - total) / 2;
        if (x < c.r.X + UF(2)) x = c.r.X + UF(2);
        uint32_t ic = c.iconCol != NOCOL && c.en ? c.iconCol : fgc;
        if (c.spin >= 0) {
            gfx::Spinner(g, G::RectF(x, c.r.Y + (c.r.Height - ip) / 2, ip, ip), C(ic), c.spin, std::max(1.5f, ip / 9));
        } else if (c.icon) {
            gfx::DrawIcon(g, c.icon, G::RectF(x, c.r.Y + (c.r.Height - ip) / 2, ip, ip), C(ic),
                          std::max(1.f, ip / 9));
        }
        if (!c.t1.empty()) {
            float avail = c.r.X + c.r.Width - (x + ip + gap) - UF(2);
            gfx::Text(g, c.t1, G::RectF(x + ip + gap, c.r.Y, std::min(tw + 2, avail), c.r.Height), f, C(fgc),
                      gfx::LEFT, true);
        }
        break;
    }

    case K::Pill: {
        uint32_t bgc = Pick(c.en, c.hover, c.down, c.bg, c.bgH, c.bgD);
        uint32_t bdc = Pick(c.en, c.hover, c.down, c.bd, c.bdH, c.bdD);
        gfx::FillRR(g, c.r, c.rad, C(bgc));
        gfx::StrokeRR(g, c.r, c.rad, C(bdc), bw);
        G::Font* f  = gfx::GetFont(c.px, false);
        G::Font* fv = gfx::GetFont(c.px, true);
        float padx = UF(7);
        float lw = gfx::TextWidth(g, c.t1, f);
        float x = c.r.X + padx;
        gfx::Text(g, c.t1, G::RectF(x, c.r.Y, lw + 2, c.r.Height), f, C(c.en ? c.fg : c.fgD), gfx::LEFT, false);
        float gap = UF(6);
        G::RectF vr(x + lw + gap, c.r.Y, c.r.Width - padx * 2 - lw - gap, c.r.Height);
        gfx::Text(g, c.t2, vr, fv, C(c.en ? c.fg2 : c.fg2D), gfx::RIGHT, true, c.mono);
        break;
    }

    case K::Check: {
        float bs = UF(9.5f);
        G::RectF box(c.r.X, c.r.Y + (c.r.Height - bs) / 2, bs, bs);
        if (c.checked) {
            gfx::FillRR(g, box, UF(1.5f), C(c.en ? col::Red : col::Dim));
            gfx::DrawIcon(g, gfx::I_CHECK, box, C(0xffffff), std::max(1.2f, bs / 7));
        } else {
            gfx::FillRR(g, box, UF(1.5f), C(col::InputBg));
            gfx::StrokeRR(g, box, UF(1.5f), C(c.hover ? 0x888888 : 0x666666), bw);
        }
        gfx::Text(g, c.t1, G::RectF(box.X + bs + UF(6), c.r.Y, c.r.Width - bs - UF(6), c.r.Height),
                  gfx::GetFont(c.px), C(c.en ? c.fg : c.fgD), gfx::LEFT, true);
        break;
    }

    case K::Radio: {
        float bs = UF(9.5f);
        G::RectF box(c.r.X, c.r.Y + (c.r.Height - bs) / 2, bs, bs);
        G::SolidBrush bgb(C(col::InputBg));
        g.FillEllipse(&bgb, box);
        G::Pen pen(C(c.checked && c.en ? col::Red : (c.hover ? 0x888888 : 0x666666)), bw);
        g.DrawEllipse(&pen, G::RectF(box.X + bw / 2, box.Y + bw / 2, box.Width - bw, box.Height - bw));
        if (c.checked) {
            G::SolidBrush dot(C(c.en ? col::Red : col::Dim));
            float d = bs * 0.5f;
            g.FillEllipse(&dot, G::RectF(box.X + (bs - d) / 2, box.Y + (bs - d) / 2, d, d));
        }
        gfx::Text(g, c.t1, G::RectF(box.X + bs + UF(6), c.r.Y, c.r.Width - bs - UF(6), c.r.Height),
                  gfx::GetFont(c.px), C(c.en ? c.fg : c.fgD), gfx::LEFT, true);
        break;
    }

    case K::Progress: {
        gfx::FillRR(g, c.r, c.rad, C(c.bg == NOCOL ? col::BarTrack : c.bg));
        gfx::StrokeRR(g, c.r, c.rad, C(c.bd == NOCOL ? col::BarBorderC : c.bd), bw);
        float in = bw;
        G::RectF inner(c.r.X + in, c.r.Y + in, c.r.Width - in * 2, c.r.Height - in * 2);
        float fw = inner.Width * std::max(0.f, std::min(1.f, c.val));
        if (fw >= 1.f) {
            G::RectF fr(inner.X, inner.Y, fw, inner.Height);
            if (c.barGrad && c.barA != c.barB) {
                G::LinearGradientBrush lb(G::RectF(fr.X, fr.Y, std::max(fr.Width, 2.f), fr.Height),
                                          C(c.barA), C(c.barB), G::LinearGradientModeHorizontal);
                gfx::FillRRBrush(g, fr, std::max(1.f, c.rad - in), lb);
            } else {
                gfx::FillRR(g, fr, std::max(1.f, c.rad - in), C(c.barA));
            }
        }
        break;
    }

    case K::Edit: {
        gfx::FillRR(g, c.r, c.rad, C(c.en ? c.bg : c.bgD));
        gfx::StrokeRR(g, c.r, c.rad, C(c.focus ? (c.bdH != NOCOL ? c.bdH : c.bd) : c.bd), bw);
        break;
    }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Panel
// ═════════════════════════════════════════════════════════════════════════════
static LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)l;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        if (cs->lpCreateParams) ((Panel*)cs->lpCreateParams)->hwnd = h;
    }
    Panel* p = (Panel*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!p) return DefWindowProcW(h, m, w, l);
    LRESULT r = p->Proc(m, w, l);
    if (m == WM_NCDESTROY) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        p->hwnd = nullptr;
    }
    return r;
}

static LRESULT CALLBACK ListProc(HWND, UINT, WPARAM, LPARAM);

void Panel::RegisterClasses(HINSTANCE hi) {
    g_hi = hi;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = hi;
    wc.lpszClassName = kPanelClass;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    WNDCLASSEXW lc{};
    lc.cbSize = sizeof lc;
    lc.lpfnWndProc = ListProc;
    lc.hInstance = hi;
    lc.lpszClassName = kListClass;
    lc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    lc.style = CS_DROPSHADOW;
    RegisterClassExW(&lc);
}

HWND Panel::Create(HWND parent, DWORD style, DWORD ex, int x, int y, int w, int h) {
    return CreateWindowExW(ex, kPanelClass, L"RedStream", style | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, y, w, h, parent, nullptr,
                           g_hi, this);
}

Ctl* Panel::Add(K kind, const G::RectF& r) {
    auto c = std::make_unique<Ctl>();
    c->kind = kind;
    c->r = r;
    Ctl* raw = c.get();
    ctls.push_back(std::move(c));
    return raw;
}

Ctl* Panel::AddButton(const G::RectF& r, const std::wstring& text, std::function<void()> fn) {
    Ctl* c = Add(K::Button, r);
    c->t1 = text;
    c->onClick = std::move(fn);
    return c;
}

Ctl* Panel::AddLabel(const G::RectF& r, const std::wstring& text, float px, uint32_t color, int ha, bool bold) {
    Ctl* c = Add(K::Label, r);
    c->t1 = text;
    c->px = px;
    c->fg = color;
    c->ha = ha;
    c->bold = bold;
    c->hand = false;
    return c;
}

Ctl* Panel::AddPill(const G::RectF& r, const std::wstring& label, std::function<void()> fn) {
    Ctl* c = Add(K::Pill, r);
    c->t1 = label;
    c->px = 9 * 1.5f * (g_k / 1.5f);
    c->bg = col::ComboBg;  c->bgH = 0x353535;  c->bgD = 0x262626;
    c->bd = col::ComboBorder; c->bdH = 0x6a6a6a; c->bdD = 0x333333;
    c->fg = col::ComboLabel; c->fgD = col::Dim;
    c->fg2 = col::RedSoft;   c->fg2D = col::Dim;
    c->rad = UF(4);
    c->onClick = std::move(fn);
    return c;
}

Ctl* Panel::AddCheck(const G::RectF& r, const std::wstring& text, float px, bool checked, std::function<void()> fn) {
    Ctl* c = Add(K::Check, r);
    c->t1 = text;
    c->px = px;
    c->checked = checked;
    c->fg = 0xffffff;
    Ctl* self = c;
    c->onClick = [self, fn]() { self->checked = !self->checked; if (fn) fn(); };
    return c;
}

Ctl* Panel::AddEdit(const G::RectF& frame, float px, bool mono, std::function<void()> onChange, bool password,
                    const wchar_t* cue) {
    Ctl* c = Add(K::Edit, frame);
    c->px = px;
    c->mono = mono;
    c->padX = UF(9);
    c->bg = col::InputBg; c->bgD = 0x262626;
    c->bd = col::InputBorder; c->bdH = 0x6a6a6a; c->bdD = 0x333333;
    c->fg = col::InputText; c->fgD = col::Dim;
    c->rad = UF(5);
    c->onChange = std::move(onChange);
    c->hand = false;
    DWORD st = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_LEFT;
    if (password) st |= ES_PASSWORD;
    c->h = CreateWindowExW(0, L"EDIT", L"", st, 0, 0, 10, 10, hwnd, nullptr, g_hi, nullptr);
    SendMessageW(c->h, WM_SETFONT, (WPARAM)gfx::GetHFont(px, mono), TRUE);
    if (password) SendMessageW(c->h, EM_SETPASSWORDCHAR, 0x25CF, 0);
    if (cue) SendMessageW(c->h, 0x1501 /*EM_SETCUEBANNER*/, FALSE, (LPARAM)cue);
    SetWindowSubclass(c->h, EditSub, 1, 0);
    LayoutEdit(c);
    return c;
}

void Panel::LayoutEdit(Ctl* c) {
    if (!c->h) return;
    HDC dc = GetDC(c->h);
    HGDIOBJ old = SelectObject(dc, gfx::GetHFont(c->px, c->mono));
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(c->h, dc);
    int th = tm.tmHeight;
    int x = (int)std::lround(c->r.X + c->padX);
    int w = (int)std::lround(c->r.Width - c->padX * 2);
    int y = (int)std::lround(c->r.Y + (c->r.Height - th) / 2);
    SetWindowPos(c->h, nullptr, x, y, std::max(4, w), th, SWP_NOZORDER | SWP_NOACTIVATE);
}

Ctl* Panel::Find(HWND edit) {
    for (auto& c : ctls) if (c->h == edit) return c.get();
    return nullptr;
}

static bool Interactive(const Ctl& c) {
    return c.vis && c.en && c.onClick &&
           (c.kind == K::Button || c.kind == K::Pill || c.kind == K::Check || c.kind == K::Radio);
}

Ctl* Panel::HitTest(int x, int y) {
    for (auto it = ctls.rbegin(); it != ctls.rend(); ++it) {
        Ctl& c = **it;
        if (!Interactive(c)) continue;
        if (x >= c.r.X && x < c.r.X + c.r.Width && y >= c.r.Y && y < c.r.Y + c.r.Height) return &c;
    }
    return nullptr;
}

void Panel::Inval(const Ctl* c) {
    if (!hwnd) return;
    RECT r{(LONG)std::floor(c->r.X) - 2, (LONG)std::floor(c->r.Y) - 2,
           (LONG)std::ceil(c->r.X + c->r.Width) + 2, (LONG)std::ceil(c->r.Y + c->r.Height) + 2};
    InvalidateRect(hwnd, &r, FALSE);
}

void Panel::InvalAll() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }

void Panel::ShiftBody(int dy) {
    for (auto& c : ctls) {
        if (!c->body) continue;
        c->r.Y += (float)dy;
        if (c->h) LayoutEdit(c.get());
    }
    InvalAll();
}

void Panel::SetVisible(Ctl* c, bool v) {
    c->vis = v;
    if (c->h) ShowWindow(c->h, v ? SW_SHOWNA : SW_HIDE);
    InvalAll();
}

void Panel::MoveTo(Ctl* c, const G::RectF& r) {
    c->r = r;
    if (c->h) LayoutEdit(c);
    InvalAll();
}

std::wstring Panel::GetText(Ctl* c) {
    if (!c || !c->h) return L"";
    int n = GetWindowTextLengthW(c->h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(c->h, s.data(), n + 1);
    s.resize(n);
    return s;
}

void Panel::SetText(Ctl* c, const std::wstring& s, bool silent) {
    if (!c || !c->h) return;
    c->mute = silent;
    SetWindowTextW(c->h, s.c_str());
    c->mute = false;
}

void Panel::Dispose(Panel* p) {
    if (p->hwnd) ShowWindow(p->hwnd, SW_HIDE);
    PostUI([p]() {
        if (p->hwnd) DestroyWindow(p->hwnd);
        delete p;
    });
}

void Panel::PaintAll(HDC hdc, const RECT& rc) {
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    RECT cr;
    GetClientRect(hwnd, &cr);
    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mdc, bmp);
    {
        G::Graphics g(mdc);
        g.SetSmoothingMode(G::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(G::TextRenderingHintClearTypeGridFit);
        g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
        g.SetInterpolationMode(G::InterpolationModeHighQualityBicubic);
        g.TranslateTransform((float)-rc.left, (float)-rc.top);
        if (paintBg) {
            paintBg(g, cr);
        } else {
            G::SolidBrush b(C(col::Body));
            g.FillRectangle(&b, G::Rect(cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top));
        }
        for (auto& c : ctls) {
            if (!c->vis) continue;
            if (c->r.X > rc.right + 2 || c->r.X + c->r.Width < rc.left - 2 ||
                c->r.Y > rc.bottom + 2 || c->r.Y + c->r.Height < rc.top - 2) continue;
            PaintCtl(g, *c);
        }
    }
    BitBlt(hdc, rc.left, rc.top, w, h, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

LRESULT Panel::Proc(UINT m, WPARAM w, LPARAM l) {
    LRESULT out = 0;
    if (pre && pre(m, w, l, out)) return out;

    switch (m) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintAll(dc, ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (!tracking_) {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        Ctl* hit = HitTest(x, y);
        for (auto& c : ctls) {
            bool hv = (c.get() == hit);
            if (hv != c->hover) { c->hover = hv; Inval(c.get()); }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        tracking_ = false;
        for (auto& c : ctls) if (c->hover) { c->hover = false; Inval(c.get()); }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            Ctl* c = HitTest(pt.x, pt.y);
            SetCursor(LoadCursorW(nullptr, (c && c->hand) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        Ctl* c = HitTest(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (c) {
            pressed_ = c;
            c->down = true;
            Inval(c);
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        Ctl* p = pressed_;
        pressed_ = nullptr;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (p) {
            p->down = false;
            Inval(p);
            Ctl* hit = HitTest(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            if (hit == p && p->onClick) {
                auto fn = p->onClick;
                fn();          // после вызова this может быть уже недействителен
            }
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (pressed_) { pressed_->down = false; Inval(pressed_); pressed_ = nullptr; }
        return 0;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        Ctl* c = Find((HWND)l);
        if (c) {
            bool en = IsWindowEnabled((HWND)l) != 0;
            SetTextColor(dc, CR(en ? c->fg : c->fgD));
            uint32_t bgc = en ? c->bg : c->bgD;
            SetBkColor(dc, CR(bgc));
            SetBkMode(dc, OPAQUE);
            return (LRESULT)BrushFor(bgc);
        }
        break;
    }

    case WM_COMMAND: {
        if (l) {
            Ctl* c = Find((HWND)l);
            if (c) {
                int code = HIWORD(w);
                if (code == EN_CHANGE) { if (!c->mute && c->onChange) c->onChange(); }
                else if (code == EN_SETFOCUS)  { c->focus = true;  Inval(c); }
                else if (code == EN_KILLFOCUS) { c->focus = false; Inval(c); }
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, m, w, l);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Выпадающий список
// ═════════════════════════════════════════════════════════════════════════════
struct ListWin {
    HWND hwnd = nullptr;
    std::vector<std::wstring> items;
    int sel = -1, hover = -1, top = 0, rows = 0;
    float itemH = 0;
    ListStyle st;
    std::function<void(int)> pick;
    bool closing = false;
};
static ListWin* g_list = nullptr;

bool ListIsOpen() { return g_list != nullptr; }
HWND ListHwnd()   { return g_list ? g_list->hwnd : nullptr; }

void CloseList() {
    if (!g_list) return;
    ListWin* l = g_list;
    g_list = nullptr;
    l->closing = true;
    if (GetCapture() == l->hwnd) ReleaseCapture();
    DestroyWindow(l->hwnd);
    delete l;
}

static LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    ListWin* lw = g_list;
    if (!lw || lw->hwnd != h) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr; GetClientRect(h, &cr);
        int W = cr.right, H = cr.bottom;
        HDC mdc = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
        HGDIOBJ old = SelectObject(mdc, bmp);
        {
            G::Graphics g(mdc);
            g.SetSmoothingMode(G::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(G::TextRenderingHintClearTypeGridFit);
            G::SolidBrush bb(C(lw->st.bg));
            g.FillRectangle(&bb, 0, 0, W, H);
            G::Font* f = gfx::GetFont(lw->st.px);
            for (int i = 0; i < lw->rows; i++) {
                int idx = lw->top + i;
                if (idx >= (int)lw->items.size()) break;
                G::RectF row(1.f, 1.f + i * lw->itemH, (float)W - 2, lw->itemH);
                if (idx == lw->hover) {
                    G::SolidBrush hb(C(lw->st.hoverBg));
                    g.FillRectangle(&hb, row);
                }
                uint32_t fc = (idx == lw->sel) ? lw->st.selFg : lw->st.fg;
                gfx::Text(g, lw->items[idx], G::RectF(row.X + UF(8), row.Y, row.Width - UF(16), row.Height), f, C(fc),
                          gfx::LEFT, true);
            }
            // скроллбар-индикатор
            if ((int)lw->items.size() > lw->rows) {
                float trackH = (float)H - 2;
                float th = std::max(UF(12), trackH * lw->rows / lw->items.size());
                float ty = 1 + (trackH - th) * lw->top / std::max<int>(1, (int)lw->items.size() - lw->rows);
                gfx::FillRR(g, G::RectF((float)W - UF(4), ty, UF(3), th), UF(1.5f), C(0x666666));
            }
            G::Pen pen(C(lw->st.bd), 1.f);
            g.DrawRectangle(&pen, 0.5f, 0.5f, (float)W - 1, (float)H - 1);
        }
        BitBlt(dc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);
        DeleteObject(bmp);
        DeleteDC(mdc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int y = GET_Y_LPARAM(l), x = GET_X_LPARAM(l);
        RECT cr; GetClientRect(h, &cr);
        int idx = -1;
        if (x >= 0 && x < cr.right && y >= 1 && y < cr.bottom - 1)
            idx = lw->top + (int)((y - 1) / lw->itemH);
        if (idx >= (int)lw->items.size()) idx = -1;
        if (idx != lw->hover) { lw->hover = idx; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1;
        int maxTop = std::max(0, (int)lw->items.size() - lw->rows);
        lw->top = std::max(0, std::min(maxTop, lw->top + d * 2));
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        RECT cr; GetClientRect(h, &cr);
        int idx = -1;
        if (x >= 0 && x < cr.right && y >= 1 && y < cr.bottom - 1)
            idx = lw->top + (int)((y - 1) / lw->itemH);
        if (idx >= (int)lw->items.size()) idx = -1;
        auto pick = lw->pick;
        CloseList();
        if (idx >= 0 && pick) pick(idx);
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (!lw->closing) CloseList();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowList(HWND owner, const RECT& a, const std::vector<std::wstring>& items, int sel, const ListStyle& st,
              std::function<void(int)> onPick) {
    CloseList();
    if (items.empty()) return;
    auto* lw = new ListWin();
    lw->items = items;
    lw->sel = sel;
    lw->st = st;
    lw->pick = std::move(onPick);
    lw->itemH = std::round(st.px * 2.0f);
    lw->rows = std::min<int>((int)items.size(), 12);

    // ширина: не меньше якоря и самого длинного пункта
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, gfx::GetHFont(st.px, false));
    int maxw = 0;
    for (auto& s : items) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
        maxw = std::max<int>(maxw, sz.cx);
    }
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    int W = std::max<int>(a.right - a.left, maxw + (int)UF(30));
    int H = (int)(lw->rows * lw->itemH) + 2;
    int x = a.left, y = a.bottom + 1;

    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromRect(&a, MONITOR_DEFAULTTONEAREST), &mi);
    if (y + H > mi.rcWork.bottom) y = a.top - H - 1;          // вверх, если не помещается
    if (x + W > mi.rcWork.right) x = mi.rcWork.right - W;

    // прокрутка к выбранному
    if (sel >= lw->rows) lw->top = std::min(sel, (int)items.size() - lw->rows);

    lw->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kListClass, L"", WS_POPUP, x, y,
                               W, H, owner, nullptr, g_hi, nullptr);
    g_list = lw;
    ShowWindow(lw->hwnd, SW_SHOWNOACTIVATE);
    SetCapture(lw->hwnd);
}
