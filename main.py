import sys
import os
import subprocess
import threading
import re
import time
import ctypes
import winreg
import tkinter.filedialog as filedialog

import customtkinter as ctk
from PIL import Image

def resource_path(name):
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, name)

def get_downloads_folder():
    try:
        key = winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders",
        )
        value, _ = winreg.QueryValueEx(key, "{374DE290-123F-4565-9164-39C4925E467B}")
        winreg.CloseKey(key)
        if value and os.path.isdir(value):
            return value
    except Exception:
        pass
    return os.path.join(os.path.expanduser("~"), "Downloads")

def force_taskbar(hwnd):
    try:
        GWL_EXSTYLE    = -20
        WS_EX_APPWINDOW = 0x00040000
        WS_EX_TOOLWINDOW = 0x00000080
        style = ctypes.windll.user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
        style = (style & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW
        ctypes.windll.user32.SetWindowLongW(hwnd, GWL_EXSTYLE, style)
        ctypes.windll.user32.ShowWindow(hwnd, 0)
        ctypes.windll.user32.ShowWindow(hwnd, 9)
    except Exception:
        pass

def show_splash_on_app(app, on_done):
    try:
        img = Image.open(resource_path("banner.png"))
    except Exception:
        on_done()
        return

    splash = ctk.CTkToplevel(app)
    splash.overrideredirect(True)
    splash.configure(fg_color="#1e1e1e")
    splash.wm_attributes("-toolwindow", True)
    splash.wm_attributes("-topmost", True)

    sw, sh = splash.winfo_screenwidth(), splash.winfo_screenheight()
    iw, ih = img.size
    splash.geometry(f"{iw}x{ih}+{(sw - iw) // 2}+{(sh - ih) // 2}")

    ctk_img = ctk.CTkImage(light_image=img, dark_image=img, size=(iw, ih))
    ctk.CTkLabel(splash, image=ctk_img, text="").pack()

    steps   = 30
    step_ms = int(3.0 * 0.3 / steps * 1000)
    hold_ms = int(3.0 * 0.4 * 1000)

    def fade_in(i=0):
        try: splash.attributes("-alpha", i / steps)
        except Exception: pass
        if i < steps:
            splash.after(step_ms, lambda: fade_in(i + 1))
        else:
            splash.after(hold_ms, fade_out)

    def fade_out(i=0):
        try: splash.attributes("-alpha", 1.0 - i / steps)
        except Exception: pass
        if i < steps:
            splash.after(step_ms, lambda: fade_out(i + 1))
        else:
            try: splash.destroy()
            except Exception: pass
            on_done()

    splash.attributes("-alpha", 0)
    splash.after(50, fade_in)

def make_combo(master, values, default, command=None):
    def _on_select(choice):
        try: combo._close_dropdown_menu()
        except Exception: pass
        if command:
            command(choice)

    combo = ctk.CTkComboBox(
        master,
        values=values,
        fg_color="#333333",
        border_color="#555555",
        text_color="white",
        button_color="#555555",
        button_hover_color="#666666",
        dropdown_fg_color="#333333",
        dropdown_text_color="white",
        dropdown_hover_color="#555555",
        font=("Segoe UI", 12),
        state="readonly",
        command=_on_select,
    )
    combo.set(default)
    return combo

class CustomTitleBar(ctk.CTkFrame):
    H = 26

    def __init__(self, master, title, on_close, on_minimize, on_about, **kw):
        super().__init__(master, height=self.H, fg_color="#151515", corner_radius=0, **kw)
        self.pack_propagate(False)

        try:
            ico = Image.open(resource_path("icon.ico")).resize((16, 16), Image.LANCZOS)
            self._ico = ctk.CTkImage(light_image=ico, dark_image=ico, size=(16, 16))
            ctk.CTkLabel(self, image=self._ico, text="",
                         fg_color="transparent", width=16, height=self.H).pack(side="left", padx=(6, 0))
        except Exception:
            pass

        ctk.CTkLabel(
            self, text=title, text_color="#cccccc",
            font=("Segoe UI", 11), fg_color="transparent", height=self.H,
        ).pack(side="left", padx=(5, 0))

        ctk.CTkButton(
            self, text="✕", width=40, height=self.H,
            fg_color="transparent", hover_color="#c42b1c",
            text_color="#bbbbbb", font=("Segoe UI", 12),
            corner_radius=0, command=on_close,
        ).pack(side="right")

        ctk.CTkButton(
            self, text="—", width=40, height=self.H,
            fg_color="transparent", hover_color="#3a3a3a",
            text_color="#bbbbbb", font=("Segoe UI", 13),
            corner_radius=0, command=on_minimize,
        ).pack(side="right")

        ctk.CTkButton(
            self, text="?", width=34, height=self.H,
            fg_color="transparent", hover_color="#3a3a3a",
            text_color="#888888", font=("Segoe UI", 13, "bold"),
            corner_radius=0, command=on_about,
        ).pack(side="right")

        self._ox = self._oy = 0
        self.bind("<ButtonPress-1>", self._start)
        self.bind("<B1-Motion>",     self._move)

    def _start(self, e):
        w = self.winfo_toplevel()
        self._ox = e.x_root - w.winfo_x()
        self._oy = e.y_root - w.winfo_y()

    def _move(self, e):
        self.winfo_toplevel().geometry(f"+{e.x_root - self._ox}+{e.y_root - self._oy}")

def show_about(parent):
    popup = ctk.CTkFrame(parent, fg_color="#2a2a2a", corner_radius=0,
                         border_width=1, border_color="#555555")

    def _close():
        popup.place_forget()
        popup.destroy()

    try:
        logo = Image.open(resource_path("logo.png"))
        lh = int(130 * logo.height / logo.width)
        lctk = ctk.CTkImage(light_image=logo, dark_image=logo, size=(130, lh))
        ctk.CTkLabel(popup, image=lctk, text="").pack(pady=(16, 4))
    except Exception:
        pass

    ctk.CTkLabel(popup, text="RedStream: Video Downloader",
                 text_color="#ff0000", font=("Segoe UI", 14, "bold")).pack(pady=(0, 6))

    ctk.CTkFrame(popup, height=1, fg_color="#444444", corner_radius=0).pack(fill="x", padx=20, pady=(0, 8))

    ctk.CTkLabel(
        popup,
        text=(
            "С помощью этой утилиты\n"
            "Вы можете скачивать видео\n"
            "с популярных сайтов, таких как:\n"
            "YouTube, Instagram, Tik-Tok,\n"
            "и многих других."
        ),
        text_color="#cccccc", font=("Segoe UI", 12), justify="center",
    ).pack(pady=(0, 8))

    ctk.CTkFrame(popup, height=1, fg_color="#444444", corner_radius=0).pack(fill="x", padx=20, pady=(0, 8))

    GITHUB_URL = "https://github.com/frostbittenbull/RedStream"
    meta_rows = [
        ("Автор:",     "#frostbittenbull",   "#ffffff", None),
        ("Сайт:",      "github.com",         "#4ea8de", GITHUB_URL),
        ("Версия:",    "1.1",                "#aaaaaa", None),
        ("Сборка:",    "09.03.2025",         "#aaaaaa", None),
        ("Платформа:", "Windows 10/11",      "#aaaaaa", None),
    ]
    meta_frame = ctk.CTkFrame(popup, fg_color="transparent")
    meta_frame.pack(padx=24, pady=(0, 4))
    for i, (lbl_text, val, col, url) in enumerate(meta_rows):
        ctk.CTkLabel(meta_frame, text=lbl_text, text_color="#777777",
                     font=("Segoe UI", 12), height=20, anchor="w").grid(
                         row=i, column=0, sticky="w", padx=(0, 0), pady=0)
        if url:
            import webbrowser
            lnk = ctk.CTkButton(
                meta_frame, text=val, text_color=col,
                font=("Segoe UI", 12, "underline"),
                fg_color="transparent", hover_color="#3a3a3a",
                height=20, anchor="w", cursor="hand2",
                command=lambda u=url: webbrowser.open(u),
            )
            lnk.grid(row=i, column=1, sticky="w", padx=(4, 0), pady=0)
        else:
            ctk.CTkLabel(meta_frame, text=val, text_color=col,
                         font=("Segoe UI", 12), height=20, anchor="w").grid(
                             row=i, column=1, sticky="w", padx=(8, 0), pady=0)

    ctk.CTkButton(
        popup, text="ОК", fg_color="#ff0000", hover_color="#bf0000",
        corner_radius=8, width=100, command=_close,
    ).pack(pady=(10, 16))

    popup.place(x=-9999, y=-9999)

    def _place():
        popup.update_idletasks()
        pw, ph = parent.winfo_width(), parent.winfo_height()
        ww = popup.winfo_reqwidth()
        wh = popup.winfo_reqheight()
        x = (pw - ww) // 2
        y = (ph - wh) // 2
        popup.place(x=x, y=y)
        popup.lift()
    parent.after(15, _place)

class RedStreamApp(ctk.CTk):
    def __init__(self):
        super().__init__()

        self.withdraw()
        self.overrideredirect(True)
        self.configure(fg_color="#444444")

        W, H = 450, 744
        sw, sh = self.winfo_screenwidth(), self.winfo_screenheight()
        self.geometry(f"{W}x{H}+{(sw - W) // 2}+{(sh - H) // 2}")

        try:
            self.iconbitmap(resource_path("icon.ico"))
        except Exception:
            pass

        self._root_frame = ctk.CTkFrame(self, fg_color="#1e1e1e", corner_radius=0)
        self._root_frame.pack(fill="both", expand=True, padx=1, pady=1)

        self._titlebar = CustomTitleBar(
            self._root_frame,
            title="RedStream: Video Downloader",
            on_close=self.destroy,
            on_minimize=self._minimize,
            on_about=lambda: show_about(self._root_frame),
        )
        self._titlebar.pack(fill="x")
        ctk.CTkFrame(self._root_frame, height=1, fg_color="#333333", corner_radius=0).pack(fill="x")

        self._content = ctk.CTkFrame(self._root_frame, fg_color="#1e1e1e", corner_radius=0)
        self._content.pack(fill="both", expand=True)

        self._process  = None
        self._timer_id = None
        self._log_file  = os.path.join(os.environ.get("TEMP", "/tmp"), "yt_progress.txt")
        self._done_file = os.path.join(os.environ.get("TEMP", "/tmp"), "yt_done.txt")

        downloads = get_downloads_folder()
        self._dest_folder = os.path.join(downloads, "RedStream Downloader")

        self._downloading_video_stream = True

        self._build_ui()
        self._toggle_settings()

        def _late_binds():
            inner = self.url_entry._entry

            def _ctrl_key(e):
                kc = e.keycode
                if kc == 86:
                    self._paste_url()
                    return "break"
                elif kc == 65:
                    inner.selection_range(0, "end")
                    return "break"
                elif kc == 67:
                    if inner.selection_present():
                        self.clipboard_clear()
                        self.clipboard_append(inner.selection_get())
                    return "break"
                elif kc == 88:
                    if inner.selection_present():
                        self.clipboard_clear()
                        self.clipboard_append(inner.selection_get())
                        inner.delete("sel.first", "sel.last")
                    return "break"
                elif kc == 90:
                    try:
                        inner.event_generate("<<Undo>>")
                    except Exception:
                        pass
                    return "break"

            inner.bind("<Control-KeyPress>", _ctrl_key, add="+")

            for cb in (self.browser_combo, self.format_combo,
                       self.vcodec_combo, self.acodec_combo, self.res_combo):
                self._bind_combo_anywhere(cb)
        self.after(200, _late_binds)

        def _show_main():
            self.deiconify()
            self.update_idletasks()
            hwnd = ctypes.windll.user32.GetParent(self.winfo_id())
            if not hwnd:
                hwnd = self.winfo_id()
            force_taskbar(hwnd)
            self.attributes("-topmost", True)
            self.after(400, lambda: self.attributes("-topmost", False))

        show_splash_on_app(self, _show_main)

    def _minimize(self):
        self.overrideredirect(False)
        self.iconify()
        def _restore(e=None):
            if self.state() == "normal":
                self.overrideredirect(True)
                self.update_idletasks()
                hwnd = ctypes.windll.user32.GetParent(self.winfo_id())
                if not hwnd:
                    hwnd = self.winfo_id()
                force_taskbar(hwnd)
                self.after(10, lambda: self.attributes("-topmost", False))
                self.unbind("<Map>")
        self.bind("<Map>", _restore)

    def _build_ui(self):
        p = self._content

        ctk.CTkLabel(
            p, text="RedStream: Video Downloader",
            text_color="#ff0000", font=("Segoe UI", 18, "bold")
        ).pack(pady=(20, 10), padx=20)

        sec1 = self._section(p)
        ctk.CTkLabel(sec1, text="Ссылка на видео/плейлист:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        url_row = ctk.CTkFrame(sec1, fg_color="transparent")
        url_row.pack(fill="x", pady=(4, 0))
        url_row.columnconfigure(0, weight=1)
        self.url_entry = ctk.CTkEntry(
            url_row,
            placeholder_text="https://www.example.com/…",
            fg_color="#333333", border_color="#555555",
            text_color="white", font=("Segoe UI", 12),
        )
        self.url_entry.grid(row=0, column=0, sticky="ew")
        paste_btn = ctk.CTkButton(
            url_row, text="\u29be", width=28, height=28,
            fg_color="#555555", hover_color="#666666",
            corner_radius=4, font=("Segoe UI", 14),
            command=self._paste_url,
        )
        paste_btn.grid(row=0, column=1, padx=(4, 0))
        self._add_tooltip(paste_btn, "Вставить ссылку\nс буфера обмена")

        sec2 = self._section(p)
        ctk.CTkLabel(sec2, text="Выберите Ваш браузер для авторизации:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        ctk.CTkLabel(sec2, text="Это позволяет загружать видео с возрастными ограничениями.",
                     text_color="#808080", font=("Segoe UI", 10), anchor="w", height=14).pack(fill="x")
        self.browser_combo = make_combo(
            sec2,
            values=["Без авторизации", "Chrome", "Chromium", "Opera", "Opera GX",
                    "Edge", "Firefox", "Brave", "Vivaldi", "Whale"],
            default="Без авторизации",
        )
        self.browser_combo.pack(fill="x", pady=(6, 6))
        ctk.CTkLabel(sec2, text="Перед скачиванием браузер должен быть закрыт.",
                     text_color="#ff0000", font=("Segoe UI", 10), anchor="w", height=14).pack(fill="x")
        ctk.CTkLabel(sec2,
                     text="Если видео не имеет возрастные ограничения, выберите \"Без авторизации\".",
                     text_color="#00bf00", font=("Segoe UI", 10), anchor="w", height=14).pack(fill="x")

        sec3 = self._section(p)
        ctk.CTkLabel(sec3, text="Контейнер/Формат:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        self._format_map = {
            "Видео: MKV": "mkv",
            "Видео: MP4": "mp4",
            "Аудио: OPUS": "opus",
            "Аудио: M4A (с аудиокодеком AAC)": "m4a",
            "Аудио: MP3 (конвертация с аудиокодека OPUS)": "mp3",
        }
        self.format_combo = make_combo(
            sec3,
            values=list(self._format_map.keys()),
            default="Видео: MP4",
            command=lambda _: self._toggle_settings(),
        )
        self.format_combo.pack(fill="x", pady=(4, 0))

        sec4 = self._section(p)
        ctk.CTkLabel(sec4, text="Настройки видео (кодеки и качество):",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        self._vcodec_map = {"AV1": "vcodec:av01", "VP9": "vcodec:vp9", "H.264": "vcodec:h264"}
        self.vcodec_combo = make_combo(sec4, values=list(self._vcodec_map.keys()), default="AV1")
        self.vcodec_combo.pack(fill="x", pady=(4, 4))
        self._acodec_map = {"OPUS": "acodec:opus", "AAC": "acodec:aac"}
        self.acodec_combo = make_combo(sec4, values=list(self._acodec_map.keys()), default="OPUS")
        self.acodec_combo.pack(fill="x", pady=(0, 4))
        self._res_map = {
            "8K (4320p)": "4320", "4K (2160p)": "2160", "2K (1440p)": "1440",
            "FullHD (1080p)": "1080", "HD (720p)": "720", "SD (480p)": "480",
        }
        self.res_combo = make_combo(sec4, values=list(self._res_map.keys()), default="FullHD (1080p)")
        self.res_combo.pack(fill="x")

        sec_folder = self._section(p)
        ctk.CTkLabel(sec_folder, text="Папка для сохранения:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")

        folder_row = ctk.CTkFrame(sec_folder, fg_color="transparent")
        folder_row.pack(fill="x", pady=(4, 0))
        folder_row.columnconfigure(0, weight=1)

        self.folder_entry = ctk.CTkEntry(
            folder_row,
            fg_color="#333333", border_color="#555555",
            text_color="#cccccc", font=("Segoe UI", 11),
            state="disabled",
        )
        self.folder_entry.grid(row=0, column=0, sticky="ew")

        self.folder_entry.configure(state="normal")
        self.folder_entry.insert(0, self._dest_folder)
        self.folder_entry.configure(state="disabled")

        browse_btn = ctk.CTkButton(
            folder_row, text="📁", width=28, height=28,
            fg_color="#555555", hover_color="#666666",
            corner_radius=4, font=("Segoe UI", 13),
            command=self._browse_folder,
        )
        browse_btn.grid(row=0, column=1, padx=(4, 0))
        self._add_tooltip(browse_btn, "Выбрать папку\nдля сохранения")

        self.download_btn = ctk.CTkButton(
            p, text="СКАЧАТЬ", font=("Segoe UI", 14, "bold"),
            fg_color="#ff0000", hover_color="#bf0000",
            corner_radius=10, height=44,
            command=self._run_download,
        )
        self.download_btn.pack(fill="x", padx=20, pady=(10, 0))

        ctk.CTkLabel(
            p,
            text="Если программа не отвечает на 100%, идет финальная обработка файла.",
            text_color="#808080", font=("Segoe UI", 10),
            justify="center", wraplength=410,
        ).pack(pady=(0, 0), padx=20)

        self.progress_frame = ctk.CTkFrame(p, fg_color="transparent")
        self.progress_bar = ctk.CTkProgressBar(
            self.progress_frame,
            fg_color="#333333", progress_color="#ff0000",
            border_color="#555555", border_width=1,
            height=18, corner_radius=5,
        )
        self.progress_bar.set(0)
        self.progress_bar.pack(fill="x", pady=(12, 0))
        self.progress_label = ctk.CTkLabel(
            self.progress_frame, text="Ожидание…",
            text_color="white", font=("Segoe UI", 11),
        )
        self.progress_label.pack()

        self.open_folder_btn = ctk.CTkButton(
            p, text="ОТКРЫТЬ ПАПКУ СКАЧАННОГО ФАЙЛА",
            font=("Segoe UI", 12, "bold"),
            fg_color="#555555", hover_color="#666666",
            corner_radius=10, height=44,
            command=self._open_folder,
        )

    def _section(self, parent):
        outer = ctk.CTkFrame(parent, fg_color="transparent")
        outer.pack(fill="x", padx=10)
        inner = ctk.CTkFrame(outer, fg_color="transparent")
        inner.pack(fill="x", pady=(0, 8))
        ctk.CTkFrame(outer, fg_color="#333333", height=1).pack(fill="x")
        ctk.CTkFrame(outer, fg_color="transparent", height=8).pack()
        return inner

    def _add_tooltip(self, widget, text):
        tip_win = [None]
        def show(e):
            if tip_win[0]: return
            x = widget.winfo_rootx() + widget.winfo_width() // 2
            y = widget.winfo_rooty() + widget.winfo_height() + 4
            tw = ctk.CTkToplevel(widget)
            tw.overrideredirect(True)
            tw.configure(fg_color="#3a3a3a")
            ctk.CTkLabel(tw, text=text, text_color="white", font=("Segoe UI", 10),
                         fg_color="#3a3a3a", justify="center", corner_radius=4).pack(padx=6, pady=4)
            tw.update_idletasks()
            tw.geometry(f"+{x - tw.winfo_width()//2}+{y}")
            tw.lift()
            tip_win[0] = tw
        def hide(e):
            if tip_win[0]:
                try: tip_win[0].destroy()
                except Exception: pass
                tip_win[0] = None
        widget.bind("<Enter>", show, add="+")
        widget.bind("<Leave>", hide, add="+")
        for w in widget.winfo_children():
            try:
                w.bind("<Enter>", show, add="+")
                w.bind("<Leave>", hide, add="+")
            except Exception: pass

    def _bind_combo_anywhere(self, combo):
        def _open(e=None):
            if combo.cget("state") == "disabled": return
            combo._open_dropdown_menu()
        try:
            for w in combo.winfo_children():
                w.bind("<Button-1>", _open, add="+")
        except Exception: pass
        combo.bind("<Button-1>", _open, add="+")

    def _paste_url(self):
        try: text = self.clipboard_get()
        except Exception: return
        inner = self.url_entry._entry
        try: inner.delete("sel.first", "sel.last")
        except Exception: pass
        inner.insert("insert", text)
        self.url_entry.focus_set()

    def _browse_folder(self):
        initial = self._dest_folder if os.path.isdir(self._dest_folder) else os.path.dirname(self._dest_folder)
        chosen = filedialog.askdirectory(
            title="Выберите папку для сохранения",
            initialdir=initial,
            parent=self,
        )
        if chosen:
            self._dest_folder = chosen
            self.folder_entry.configure(state="normal")
            self.folder_entry.delete(0, "end")
            self.folder_entry.insert(0, self._dest_folder)
            self.folder_entry.configure(state="disabled")

    def _toggle_settings(self):
        fmt = self._format_map.get(self.format_combo.get(), "mp4")
        is_audio = fmt in ("opus", "m4a", "mp3")
        state = "disabled" if is_audio else "readonly"
        for cb in (self.vcodec_combo, self.acodec_combo, self.res_combo):
            cb.configure(state=state)
            if is_audio:
                cb.configure(fg_color="#252525", text_color="#666666",
                             border_color="#333333", button_color="#333333")
            else:
                cb.configure(fg_color="#333333", text_color="white",
                             border_color="#555555", button_color="#555555")

    def _restore_download_btn(self):
        self.download_btn.configure(text="СКАЧАТЬ", fg_color="#ff0000",
                                    hover_color="#bf0000", command=self._run_download)

    def _cancel_download(self):
        if self._process:
            try:
                subprocess.run(["taskkill", "/F", "/IM", "yt-dlp.exe", "/T"],
                               creationflags=subprocess.CREATE_NO_WINDOW)
                subprocess.run(["taskkill", "/F", "/IM", "ffmpeg.exe", "/T"],
                               creationflags=subprocess.CREATE_NO_WINDOW)
            except Exception: pass
            self._process = None
        if self._timer_id:
            self.after_cancel(self._timer_id)
            self._timer_id = None
        self._restore_download_btn()
        self.progress_bar.configure(progress_color="#ffaa00")
        self.progress_label.configure(text="Загрузка отменена.")

    def _open_folder(self):
        os.makedirs(self._dest_folder, exist_ok=True)
        os.startfile(self._dest_folder)

    def _run_download(self):
        url = self.url_entry.get().strip()
        if not url:
            self._show_error("Вставьте ссылку на видео или плейлист!")
            return

        self.download_btn.configure(text="ОТМЕНИТЬ", fg_color="#555555",
                                    hover_color="#666666", command=self._cancel_download)
        self.open_folder_btn.pack_forget()

        browser_label = self.browser_combo.get()
        browser_key = {
            "Без авторизации": "none", "Chrome": "chrome", "Chromium": "chromium",
            "Opera": "opera", "Opera GX": "opera_gx", "Edge": "edge",
            "Firefox": "firefox", "Brave": "brave", "Vivaldi": "vivaldi", "Whale": "whale",
        }.get(browser_label, "none")

        cookie_opt = []
        if browser_key != "none":
            if browser_key == "opera_gx":
                opera_path = os.path.join(os.environ.get("APPDATA", ""),
                                          "Opera Software", "Opera GX Stable")
                cookie_opt = ["--cookies-from-browser", f"opera:{opera_path}"]
            else:
                cookie_opt = ["--cookies-from-browser", browser_key]

        fmt    = self._format_map.get(self.format_combo.get(), "mp4")
        res    = self._res_map.get(self.res_combo.get(), "1080")
        vcodec = self._vcodec_map.get(self.vcodec_combo.get(), "vcodec:av01")
        acodec = self._acodec_map.get(self.acodec_combo.get(), "acodec:opus")

        if fmt in ("mkv", "mp4"):
            fmt_opts = [
                "-f", f"bestvideo[height<={res}]+bestaudio/best[height<={res}]",
                "--merge-output-format", fmt, "--remux-video", fmt,
                "-S", f"{vcodec},{acodec}",
            ]
        elif fmt == "opus":
            fmt_opts = ["-x", "--audio-format", "opus"]
        elif fmt == "m4a":
            fmt_opts = ["-x", "--audio-format", "m4a"]
        elif fmt == "mp3":
            fmt_opts = ["-x", "--audio-format", "mp3", "--audio-quality", "0"]
        else:
            fmt_opts = []

        os.makedirs(self._dest_folder, exist_ok=True)
        dest  = os.path.join(self._dest_folder, "%(title)s.%(ext)s")
        ytdlp = resource_path("yt-dlp.exe")
        cmd   = [ytdlp, "--newline", "--no-colors"] + cookie_opt + fmt_opts + ["-o", dest, url]

        for f in (self._log_file, self._done_file):
            try: os.remove(f)
            except FileNotFoundError: pass

        self.progress_frame.pack(fill="x", padx=20, pady=(8, 0))
        self.progress_bar.set(0)
        self.progress_bar.configure(progress_color="#ff0000")
        self.progress_label.configure(text="Подключение и анализ…")

        def run():
            with open(self._log_file, "w", encoding="utf-8") as log:
                proc = subprocess.Popen(
                    cmd, stdout=log, stderr=log,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                self._process = proc
                proc.wait()
            with open(self._done_file, "w") as f:
                f.write("DONE")

        threading.Thread(target=run, daemon=True).start()
        self._timer_id = self.after(500, self._check_progress)

    def _check_progress(self):
        error_found = False
        if os.path.exists(self._log_file):
            try:
                with open(self._log_file, "r", encoding="utf-8", errors="replace") as f:
                    content = f.read()
                lines = content.splitlines()
                last_status = ""
                for line in reversed(lines):
                    if "ERROR:" in line:
                        last_status = "Ошибка! Проверьте ссылку или закройте браузер."
                        self.progress_bar.configure(progress_color="#ffaa00")
                        error_found = True
                        break
                    elif "[download]" in line and "%" in line:
                        last_status = line
                        break
                    elif "[Merger]" in line or "[ExtractAudio]" in line:
                        last_status = "Финальная обработка файла…"
                        break
                if "%" in last_status and "Ошибка" not in last_status:
                    fmt = self._format_map.get(self.format_combo.get(), "mp4")
                    is_video_fmt = fmt in ("mkv", "mp4")
                    if is_video_fmt:
                        video_exts = (".mp4", ".mkv", ".webm", ".m4v", ".avi", ".mov", ".ts", ".part")
                        audio_exts = (".opus", ".m4a", ".mp3", ".aac", ".ogg", ".wav", ".part")
                        for scan_line in reversed(lines):
                            if "[download] Destination:" in scan_line:
                                lower = scan_line.lower().replace(".part", "")
                                if any(lower.endswith(e) for e in video_exts):
                                    self._downloading_video_stream = True
                                elif any(lower.endswith(e) for e in audio_exts):
                                    self._downloading_video_stream = False
                                break

                    m = re.search(r"\[download\]\s+([\d.]+)%\s+of\s+([\d.]+)(\w+)", last_status)
                    if m:
                        pct = float(m.group(1)) / 100
                        self.progress_bar.set(pct)
                        total_val  = float(m.group(2))
                        total_unit = m.group(3).upper()
                        unit_to_mb = {"KIB": 1/1024, "MIB": 1, "GIB": 1024,
                                      "KB": 1/1000, "MB": 1, "GB": 1000}
                        total_mb = total_val * unit_to_mb.get(total_unit, 1)
                        done_mb  = total_mb * pct
                        size_str = (
                            f"({done_mb:,.2f} МБ из {total_mb:,.2f} МБ)"
                            .replace(",", " ").replace(".", ",")
                        )
                        if is_video_fmt:
                            label = "Загрузка видеопотока" if self._downloading_video_stream else "Загрузка аудиопотока"
                        else:
                            label = "Загрузка"
                        self.progress_label.configure(text=f"{label}: {m.group(1)}% {size_str}")
                elif last_status:
                    self.progress_label.configure(text=last_status)
            except Exception:
                pass

        if os.path.exists(self._done_file):
            if self._timer_id:
                self.after_cancel(self._timer_id)
                self._timer_id = None
            self._restore_download_btn()
            if not error_found:
                self.progress_bar.set(1.0)
                self.progress_label.configure(text="Успешно завершено!")
                self.open_folder_btn.pack(fill="x", padx=20, pady=(8, 0))
            for f in (self._log_file, self._done_file):
                try: os.remove(f)
                except Exception: pass
            return

        self._timer_id = self.after(500, self._check_progress)

    def _show_error(self, msg):
        win = ctk.CTkToplevel(self)
        win.overrideredirect(True)
        win.configure(fg_color="#2a2a2a")
        win.resizable(False, False)
        win.grab_set()
        frame = ctk.CTkFrame(win, fg_color="#2a2a2a", corner_radius=0,
                             border_width=1, border_color="#555555")
        frame.pack(padx=0, pady=0)
        ctk.CTkLabel(frame, text="⚠  Внимание!", text_color="#ff4444",
                     font=("Segoe UI", 13, "bold")).pack(padx=24, pady=(16, 4))
        ctk.CTkLabel(frame, text=msg, text_color="white",
                     font=("Segoe UI", 12), wraplength=260).pack(padx=24, pady=(0, 12))
        ctk.CTkButton(frame, text="ОК", fg_color="#ff0000", hover_color="#bf0000",
                      corner_radius=8, width=100,
                      command=win.destroy).pack(pady=(0, 16))
        def _center():
            win.update_idletasks()
            ww, wh = win.winfo_width(), win.winfo_height()
            x = self.winfo_x() + (450 - ww) // 2
            y = self.winfo_y() + (744 - wh) // 2
            win.geometry(f"+{x}+{y}")
            win.lift()
        win.after(10, _center)

if __name__ == "__main__":
    ctk.set_appearance_mode("dark")
    ctk.set_default_color_theme("dark-blue")
    app = RedStreamApp()
    app.mainloop()