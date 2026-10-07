#pragma once
#include "gfx.h"

constexpr uint32_t NOCOL = 0xFFFFFFFFu;   // «без заливки/без рамки»

enum class K { Button, Label, Pill, Check, Radio, Progress, Edit, Custom };

struct Ctl {
    K    kind = K::Button;
    int  id = 0;
    G::RectF r;                       // физические пиксели, координаты клиентской области панели
    bool vis = true, en = true, hover = false, down = false, body = true;

    std::wstring t1, t2;              // кнопка/метка: t1; пилюля: t1 = подпись, t2 = значение
    float px = 10;                    // размер шрифта (физические px)
    bool  bold = false, ul = false, mono = false, wrap = false;
    int   ha = gfx::CENTER;

    uint32_t fg = col::Text, fgH = NOCOL, fgD = col::Dim;
    uint32_t fg2 = col::RedSoft, fg2D = col::Dim;
    uint32_t bg = NOCOL, bgH = NOCOL, bgD = NOCOL;
    uint32_t bd = NOCOL, bdH = NOCOL, bdD = NOCOL;
    float rad = 5;

    int   icon = 0;                   // gfx::Icon слева от текста кнопки
    float iconPx = 0;
    uint32_t iconCol = NOCOL;

    bool  checked = false;            // Check/Radio
    float val = 0;                    // Progress 0..1
    uint32_t barA = col::Red, barB = col::Orange;
    bool  barGrad = true;
    bool  hand = true;
    int   spin = -1;                  // >=0 — вместо иконки рисуется спиннер (фаза)

    std::function<void()> onClick;
    std::function<void()> onChange;   // Edit: EN_CHANGE
    std::function<void(G::Graphics&, Ctl&)> draw;   // Custom

    // Edit
    HWND h = nullptr;
    bool focus = false, mute = false;
    float padX = 0;

    // Pill
    std::vector<std::wstring> items;
    int sel = 0;
};

class Panel {
public:
    HWND hwnd = nullptr;
    std::vector<std::unique_ptr<Ctl>> ctls;
    std::function<void(G::Graphics&, const RECT&)> paintBg;
    // предобработчик: вернуть true, если сообщение обработано (результат в out)
    std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)> pre;

    static void RegisterClasses(HINSTANCE hi);
    HWND Create(HWND parent, DWORD style, DWORD ex, int x, int y, int w, int h);

    Ctl* Add(K kind, const G::RectF& r);
    Ctl* AddButton(const G::RectF& r, const std::wstring& text, std::function<void()> fn);
    Ctl* AddLabel(const G::RectF& r, const std::wstring& text, float px, uint32_t color,
                  int ha = gfx::LEFT, bool bold = false);
    Ctl* AddPill(const G::RectF& r, const std::wstring& label, std::function<void()> fn);
    Ctl* AddEdit(const G::RectF& frame, float px, bool mono, std::function<void()> onChange,
                 bool password = false, const wchar_t* cue = nullptr);
    Ctl* AddCheck(const G::RectF& r, const std::wstring& text, float px, bool checked, std::function<void()> fn);

    Ctl* Find(HWND edit);
    Ctl* HitTest(int x, int y);
    void Inval(const Ctl* c);
    void InvalAll();
    void ShiftBody(int dy);
    void LayoutEdit(Ctl* c);
    void SetVisible(Ctl* c, bool v);
    void MoveTo(Ctl* c, const G::RectF& r);

    std::wstring GetText(Ctl* c);
    void SetText(Ctl* c, const std::wstring& s, bool silent = true);

    // Отложенное уничтожение окна и самой панели (безопасно вызывать из обработчика)
    static void Dispose(Panel* p);

    LRESULT Proc(UINT m, WPARAM w, LPARAM l);

private:
    Ctl* pressed_ = nullptr;
    bool tracking_ = false;
    void PaintAll(HDC hdc, const RECT& rc);
};

// Рисование одного контрола (используется и самой панелью)
void PaintCtl(G::Graphics& g, Ctl& c);

// ── Выпадающий список (отдельное всплывающее окно с захватом мыши) ────────────
struct ListStyle {
    uint32_t bg = 0x2a2a2a, bd = 0x555555, hoverBg = 0x444444, fg = 0xdddddd, selFg = col::RedSoft;
    float px = 13.5f;
};
void ShowList(HWND owner, const RECT& anchorScreen, const std::vector<std::wstring>& items, int sel,
              const ListStyle& st, std::function<void(int)> onPick);
void CloseList();
bool ListIsOpen();
HWND ListHwnd();
