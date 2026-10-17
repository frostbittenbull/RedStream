#pragma once
#include "common.h"

namespace gfx {

enum Icon {
    I_NONE = 0, I_CLOSE, I_MINUS, I_ARROW_DOWN, I_STOP, I_FOLDER, I_DOWNLOAD,
    I_CLOCK, I_GEAR, I_PLAY, I_CALENDAR, I_PLUS, I_CHECK, I_QUESTION, I_CHEVRON
};
enum HA { LEFT = 0, CENTER = 1, RIGHT = 2 };

void Init();
void Shutdown();

G::Font* GetFont(float px, bool bold = false, bool mono = false, bool underline = false);
HFONT    GetHFont(float px, bool mono = false);   // для EDIT-контролов

void AddRR(G::GraphicsPath& p, const G::RectF& r, float rad);
void FillRR(G::Graphics& g, const G::RectF& r, float rad, G::Color c);
void FillRRBrush(G::Graphics& g, const G::RectF& r, float rad, G::Brush& b);
void StrokeRR(G::Graphics& g, const G::RectF& r, float rad, G::Color c, float w = 1.f);

void  Text(G::Graphics& g, const std::wstring& s, const G::RectF& r, G::Font* f, G::Color c,
           int ha = LEFT, bool ellipsis = true, bool pathEllipsis = false);
float TextWidth(G::Graphics& g, const std::wstring& s, G::Font* f);
float TextHeight(G::Graphics& g, G::Font* f);

void DrawIcon(G::Graphics& g, int icon, const G::RectF& r, G::Color c, float stroke = 1.5f);
void Spinner(G::Graphics& g, const G::RectF& r, G::Color c, int phase, float stroke = 2.f);

// Загрузка картинок (PNG/JPG) через GDI+
std::unique_ptr<G::Bitmap> LoadBitmapFile(const std::wstring& path);
std::unique_ptr<G::Bitmap> LoadBitmapMem(const void* data, size_t n);
// Картинка, вшитая в exe (RCDATA); если не найдена — файл рядом с exe
std::unique_ptr<G::Bitmap> LoadBitmapRes(int id, const std::wstring& fallbackFile);

}  // namespace gfx
