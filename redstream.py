import sys
import os
import subprocess
import threading
import re
import time
import ctypes
import winreg
import winsound
import winotify
import tkinter.filedialog as filedialog

import customtkinter as ctk
from PIL import Image

def resource_path(name):
    base = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, name)

APP_VERSION = "2.1"
GITHUB_RELEASES_URL = "https://github.com/frostbittenbull/RedStream/releases/latest"
GITHUB_API_URL      = "https://api.github.com/repos/frostbittenbull/RedStream/releases/latest"

def play_sound(kind):
    sounds = {
        "success": r"C:\Windows\Media\Windows Background.wav",
        "error":   r"C:\Windows\Media\Windows Foreground.wav",
    }
    path = sounds.get(kind, "")
    def _play():
        try:
            if path and __import__("os").path.exists(path):
                winsound.PlaySound(path, winsound.SND_FILENAME | winsound.SND_NODEFAULT)
            else:
                beep = winsound.MB_ICONASTERISK if kind == "success" else winsound.MB_ICONHAND
                winsound.MessageBeep(beep)
        except Exception:
            pass
    __import__("threading").Thread(target=_play, daemon=True).start()

def show_toast(title, message):
    def _worker():
        try:
            from winotify import Notification
            icon = resource_path("icon.ico")
            toast = Notification(
                app_id="RedStream",
                title=title,
                msg=message,
                icon=icon,
            )
            toast.show()
        except Exception:
            pass
    threading.Thread(target=_worker, daemon=True).start()

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

def get_settings_path():
    base = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, "settings.txt")

def load_dest_folder():
    path = get_settings_path()
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                folder = f.read().strip()
            if folder:
                return folder
        except Exception:
            pass
    folder = os.path.join(get_downloads_folder(), "RedStream Downloader")
    save_dest_folder(folder)
    return folder

def save_dest_folder(folder):
    try:
        with open(get_settings_path(), "w", encoding="utf-8") as f:
            f.write(folder)
    except Exception:
        pass

def get_history_path():
    base = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, "history.txt")

def load_history():
    try:
        with open(get_history_path(), "r", encoding="utf-8") as f:
            return [l.strip() for l in f.readlines() if l.strip()]
    except Exception:
        return []

def save_history(url):
    history = load_history()
    if url in history:
        history.remove(url)
    history.insert(0, url)
    try:
        with open(get_history_path(), "w", encoding="utf-8") as f:
            f.write("\n".join(history))
    except Exception:
        pass

def load_history_combo():
    return load_history()[:5]

def force_taskbar(hwnd):
    try:
        GWL_EXSTYLE      = -20
        WS_EX_APPWINDOW  = 0x00040000
        WS_EX_TOOLWINDOW = 0x00000080
        style = ctypes.windll.user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
        style = (style & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW
        ctypes.windll.user32.SetWindowLongW(hwnd, GWL_EXSTYLE, style)
        ctypes.windll.user32.ShowWindow(hwnd, 0)
        ctypes.windll.user32.ShowWindow(hwnd, 9)
    except Exception:
        pass

def set_window_icon(hwnd):
    try:
        icon_path = resource_path("icon.ico")
        IMAGE_ICON   = 1
        LR_LOADFROMFILE  = 0x00000010
        LR_DEFAULTSIZE   = 0x00000040
        WM_SETICON   = 0x0080
        ICON_SMALL   = 0
        ICON_BIG     = 1

        hicon = ctypes.windll.user32.LoadImageW(
            None, icon_path, IMAGE_ICON,
            0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE,
        )
        if hicon:
            ctypes.windll.user32.SendMessageW(hwnd, WM_SETICON, ICON_BIG,   hicon)
            ctypes.windll.user32.SendMessageW(hwnd, WM_SETICON, ICON_SMALL, hicon)
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
        state="normal",
        command=_on_select,
    )

    def _on_enter(e):
        if combo.cget("state") != "disabled":
            combo.configure(button_color="#666666")

    def _on_leave(e):
        if combo.cget("state") != "disabled":
            combo.configure(button_color="#555555")

    try:
        combo._canvas.bind("<Enter>", _on_enter, add="+")
        combo._canvas.bind("<Leave>", _on_leave, add="+")
        if hasattr(combo, "_entry"):
            combo._entry.bind("<Enter>", _on_enter, add="+")
            combo._entry.bind("<Leave>", _on_leave, add="+")
    except Exception: pass

    # Блокируем ручной ввод, курсор палец
    try:
        if hasattr(combo, "_entry"):
            combo._entry.configure(cursor="hand2")
            combo._entry.bind("<Key>", lambda e: "break")
        if hasattr(combo, "_canvas"):
            combo._canvas.configure(cursor="hand2")
    except Exception: pass

    if values and default in values:
        combo.set(default)
    elif values:
        combo.set(values[0])
    return combo

class CustomTitleBar(ctk.CTkFrame):
    H = 26

    def __init__(self, master, title, on_close, on_minimize, on_about, on_update, on_tray=None, **kw):
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

        if on_tray:
            ctk.CTkButton(
                self, text="-", width=30, height=self.H,
                fg_color="transparent", hover_color="#3a3a3a",
                text_color="#aaaaaa", font=("Segoe UI", 11),
                corner_radius=0, command=on_tray,
            ).pack(side="right")

        ctk.CTkButton(
            self, text="?", width=34, height=self.H,
            fg_color="transparent", hover_color="#3a3a3a",
            text_color="#007aff", font=("Segoe UI", 13, "bold"),
            corner_radius=0, command=on_about,
        ).pack(side="right")

        ctk.CTkButton(
            self, text="↓", width=30, height=self.H,
            fg_color="transparent", hover_color="#3a3a3a",
            text_color="#00bf00", font=("Segoe UI", 13, "bold"),
            corner_radius=0, command=on_update,
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
        ("Версия:",    "2.1",                "#aaaaaa", None),
        ("Сборка:",    "21.03.2025",         "#aaaaaa", None),
        ("Платформа:", "Windows 10/11",      "#aaaaaa", None),
    ]
    meta_frame = ctk.CTkFrame(popup, fg_color="transparent")
    meta_frame.pack(padx=24, pady=(0, 16))
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
        popup, text="✕", width=28, height=18,
        fg_color="transparent", hover_color="#c42b1c",
        text_color="#bbbbbb", font=("Segoe UI", 12),
        corner_radius=0, command=_close,
    ).place(relx=1.0, rely=0.0, anchor="ne", x=-1, y=1)

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

def show_settings(toolbar_btn, app_root):
    import winreg, subprocess
    REG_KEY  = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
    REG_NAME = "RedStream"
    EXE_PATH = r"C:\Program Files\RedStream\redstream.exe"

    def _is_autostart():
        try:
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, REG_KEY, 0, winreg.KEY_READ)
            winreg.QueryValueEx(key, REG_NAME)
            winreg.CloseKey(key)
            return True
        except Exception:
            return False

    def _set_autostart(enable):
        try:
            if enable:
                subprocess.run(
                    ["reg", "add", f"HKEY_LOCAL_MACHINE\\{REG_KEY}",
                     "/v", REG_NAME, "/t", "REG_SZ", "/d", EXE_PATH, "/f"],
                    creationflags=subprocess.CREATE_NO_WINDOW, check=True
                )
            else:
                subprocess.run(
                    ["reg", "delete", f"HKEY_LOCAL_MACHINE\\{REG_KEY}",
                     "/v", REG_NAME, "/f"],
                    creationflags=subprocess.CREATE_NO_WINDOW, check=True
                )
        except Exception:
            pass

    existing = getattr(app_root, "_settings_dropdown", None)
    if existing:
        try:
            existing.place_forget()
            existing.destroy()
        except Exception:
            pass
        app_root._settings_dropdown = None
        return

    dropdown = ctk.CTkFrame(app_root, fg_color="#2a2a2a", corner_radius=0,
                             border_width=1, border_color="#555555")
    app_root._settings_dropdown = dropdown

    def _close_dropdown(e=None):
        try:
            dropdown.place_forget()
            dropdown.destroy()
        except Exception:
            pass
        app_root._settings_dropdown = None

    var = ctk.IntVar(value=1 if _is_autostart() else 0)

    def _on_toggle():
        _set_autostart(var.get() == 1)

    ctk.CTkCheckBox(
        dropdown, text="Запускать RedStream при запуске компьютера",
        variable=var, command=_on_toggle,
        text_color="white", font=("Segoe UI", 11),
        fg_color="#ff0000", hover_color="#bf0000",
        checkbox_width=12, checkbox_height=12, corner_radius=0,
        border_color="#555555",
    ).pack(padx=10, pady=(6, 0), anchor="w")

    history_path = get_history_path()
    history_exists = os.path.exists(history_path)
    ctk.CTkButton(
        dropdown,
        text="Открыть файл истории",
        fg_color="transparent" if history_exists else "#2a2a2a",
        hover_color="#3a3a3a" if history_exists else "#2a2a2a",
        text_color="white" if history_exists else "#555555",
        font=("Segoe UI", 11), anchor="w", height=26, corner_radius=0,
        state="normal" if history_exists else "disabled",
        command=lambda: os.startfile(history_path) if history_exists else None,
    ).pack(fill="x", padx=6, pady=(0, 0))

    def _clear_fields():
        _close_dropdown()
        app_root.url_entry.set("")
        app_root._preview_data = None
        app_root._raw_formats = []
        app_root._stop_spinner()
        app_root._gear_btn.configure(state="disabled", text="⚙", fg_color="#444444", hover_color="#444444")
        for cb in (app_root.vcodec_combo, app_root.acodec_combo, app_root.res_combo, app_root.fps_combo):
            app_root._set_combo_state(cb, "disabled")
            cb.configure(values=[""])
            cb.set("")
        app_root.filesize_label.configure(state="normal")
        app_root.filesize_label.delete(0, "end")
        app_root.filesize_label.configure(state="disabled")
        app_root.filesize_warn_label.configure(text="")
        app_root.progress_bar.set(0)
        app_root.progress_bar.configure(progress_color="#333333")
        app_root.progress_label.configure(text="Вставьте ссылку на видео или плейлист, и нажмите «СКАЧАТЬ».", text_color="#888888")
        app_root.open_folder_btn.configure(state="disabled", fg_color="#333333", hover_color="#333333", text_color="#666666")

    ctk.CTkButton(
        dropdown, text="Очистить поля",
        fg_color="transparent", hover_color="#3a3a3a",
        text_color="white", font=("Segoe UI", 11),
        anchor="w", height=26, corner_radius=0,
        command=_clear_fields,
    ).pack(fill="x", padx=6, pady=(0, 0))

    ctk.CTkButton(
        dropdown, text="Выход",
        fg_color="transparent", hover_color="#3a3a3a",
        text_color="#ff4444", font=("Segoe UI", 11),
        anchor="w", height=26, corner_radius=0,
        command=app_root.destroy,
    ).pack(fill="x", padx=6, pady=(0, 4))

    dropdown.update_idletasks()
    toolbar_btn.update_idletasks()
    bx = toolbar_btn.winfo_rootx() - app_root.winfo_rootx()
    by = toolbar_btn.winfo_rooty() - app_root.winfo_rooty() + toolbar_btn.winfo_height()
    dropdown.place(x=bx, y=by)
    dropdown.lift()

    app_root.bind("<Button-1>", lambda e: _close_dropdown() if not (
        dropdown.winfo_rootx() <= e.x_root <= dropdown.winfo_rootx() + dropdown.winfo_width() and
        dropdown.winfo_rooty() <= e.y_root <= dropdown.winfo_rooty() + dropdown.winfo_height()
    ) else None, add="+")


def show_updater(parent, app):
    import urllib.request, shutil
    popup = ctk.CTkFrame(parent, fg_color="#2a2a2a", corner_radius=0,
                         border_width=1, border_color="#555555")

    _state = {"running": False, "cancel": False, "proc": None}

    def _close():
        if _state["running"]: return
        popup.place_forget()
        popup.destroy()

    def _reset_buttons():
        btn_ffmpeg.configure(state="normal", text="Обновить ffmpeg",
                             fg_color="#ff0000", hover_color="#bf0000",
                             command=_update_ffmpeg)
        btn_ytdlp.configure(state="normal", text="Обновить yt-dlp",
                            fg_color="#ff0000", hover_color="#bf0000",
                            command=_update_ytdlp)
        close_btn.configure(state="normal")
        _state["running"] = False
        _state["cancel"]  = False
        _state["proc"]    = None

    def _finish(text, status="success"):
        color = "#00bf00" if status == "success" else "#ff0000"
        try:
            upd_progress.configure(progress_color=color)
            upd_progress.set(1.0)
            upd_label.configure(text=text)
            _reset_buttons()
            play_sound(status)
        except Exception:
            pass

    def _cancel():
        _state["cancel"] = True
        if _state["proc"]:
            try: _state["proc"].terminate()
            except Exception: pass
        upd_label.configure(text="Отмена…")

    ctk.CTkLabel(popup, text="Обновление компонентов",
                 text_color="#ff0000", font=("Segoe UI", 14, "bold")).pack(pady=(16, 8))
    ctk.CTkFrame(popup, height=1, fg_color="#444444", corner_radius=0).pack(fill="x", padx=20, pady=(0, 12))
    ctk.CTkLabel(
        popup, text="Перед обновлением убедитесь,\nчто нет активных загрузок.",
        text_color="#cccccc", font=("Segoe UI", 12), justify="center",
    ).pack(pady=(0, 12))
    ctk.CTkFrame(popup, height=1, fg_color="#444444", corner_radius=0).pack(fill="x", padx=20, pady=(0, 12))

    btn_ffmpeg = ctk.CTkButton(
        popup, text="Обновить ffmpeg", fg_color="#ff0000", hover_color="#bf0000",
        text_color="white", font=("Segoe UI", 12, "bold"), corner_radius=8, height=36,
        command=lambda: _update_ffmpeg(),
    )
    btn_ffmpeg.pack(fill="x", padx=20, pady=(0, 8))

    btn_ytdlp = ctk.CTkButton(
        popup, text="Обновить yt-dlp", fg_color="#ff0000", hover_color="#bf0000",
        text_color="white", font=("Segoe UI", 12, "bold"), corner_radius=8, height=36,
        command=lambda: _update_ytdlp(),
    )
    btn_ytdlp.pack(fill="x", padx=20, pady=(0, 12))

    ctk.CTkFrame(popup, height=1, fg_color="#444444", corner_radius=0).pack(fill="x", padx=20, pady=(0, 8))

    progress_frame = ctk.CTkFrame(popup, fg_color="transparent")
    upd_progress = ctk.CTkProgressBar(
        progress_frame, fg_color="#333333", progress_color="#333333",
        border_color="#555555", border_width=1, height=18, corner_radius=5, width=205,
    )
    upd_progress.set(0)
    upd_progress.pack(fill="x", pady=(0, 4))
    upd_label = ctk.CTkLabel(progress_frame, text="Ожидание…", text_color="white", font=("Segoe UI", 11))
    upd_label.pack()
    progress_frame.pack(fill="x", padx=20, pady=(0, 16))

    close_btn = ctk.CTkButton(
        popup, text="✕", width=28, height=18,
        fg_color="transparent", hover_color="#c42b1c",
        text_color="#bbbbbb", font=("Segoe UI", 12),
        corner_radius=0, command=_close,
    )
    close_btn.place(relx=1.0, rely=0.0, anchor="ne", x=-2, y=1)

    def _update_ffmpeg():
        _state["running"] = True
        _state["cancel"]  = False
        btn_ffmpeg.configure(text="Отмена", fg_color="#555555", hover_color="#666666", command=_cancel)
        btn_ytdlp.configure(state="disabled", fg_color="#444444", hover_color="#444444")
        close_btn.configure(state="disabled")
        upd_progress.configure(progress_color="#ffaa00")
        upd_progress.set(0)
        upd_label.configure(text="Подключение…")

        URL      = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-git-essentials.7z"
        base     = resource_path("")
        out_file = os.path.join(base, "ffmpeg-git-essentials.7z")

        def _worker():
            try:
                req   = urllib.request.urlopen(URL, timeout=60)
                total = int(req.headers.get("Content-Length", 0))
                done  = 0
                chunk = 65536
                with open(out_file, "wb") as f:
                    while True:
                        if _state["cancel"]:
                            req.close()
                            try: os.remove(out_file)
                            except Exception: pass
                            popup.after(0, lambda: _finish("Отменено.", "error"))
                            return
                        buf = req.read(chunk)
                        if not buf: break
                        f.write(buf)
                        done += len(buf)
                        if total > 0:
                            pct     = done / total
                            done_mb = done  / 1024 / 1024
                            tot_mb  = total / 1024 / 1024
                            txt = f"Скачивание: {pct*100:.1f}%  ({done_mb:.1f} МБ из {tot_mb:.1f} МБ)"
                        else:
                            pct = 0
                            txt = f"Скачивание: {done/1024/1024:.1f} МБ…"
                        _p, _t = pct, txt
                        popup.after(0, lambda p=_p, t=_t: (upd_progress.set(p), upd_label.configure(text=t)))

                if _state["cancel"]:
                    try: os.remove(out_file)
                    except Exception: pass
                    popup.after(0, lambda: _finish("Отменено.", "error"))
                    return

                popup.after(0, lambda: upd_label.configure(text="Распаковка архива…"))
                sza = resource_path("7za.exe")
                subprocess.run([sza, "x", out_file, f"-o{base}", "-y"],
                               cwd=base, creationflags=subprocess.CREATE_NO_WINDOW, check=True)

                popup.after(0, lambda: upd_label.configure(text="Установка ffmpeg.exe…"))
                for d in os.listdir(base):
                    full = os.path.join(base, d)
                    if os.path.isdir(full) and d.startswith("ffmpeg-") and "-git-" in d:
                        src = os.path.join(full, "bin", "ffmpeg.exe")
                        dst = os.path.join(base, "ffmpeg.exe")
                        if os.path.exists(src):
                            shutil.move(src, dst)
                        shutil.rmtree(full, ignore_errors=True)
                        break
                try: os.remove(out_file)
                except Exception: pass

                popup.after(0, lambda: _finish("Успешно завершено!", "success"))
            except Exception as e:
                try: os.remove(out_file)
                except Exception: pass
                msg = f"Ошибка: {e}"
                popup.after(0, lambda m=msg: _finish(m, "error"))

        threading.Thread(target=_worker, daemon=True).start()

    def _update_ytdlp():
        _state["running"] = True
        _state["cancel"]  = False
        btn_ytdlp.configure(text="Отмена", fg_color="#555555", hover_color="#666666", command=_cancel)
        btn_ffmpeg.configure(state="disabled", fg_color="#444444", hover_color="#444444")
        close_btn.configure(state="disabled")
        upd_progress.configure(progress_color="#ffaa00")
        upd_progress.set(0)
        upd_label.configure(text="Проверка обновлений…")

        def _pulse(i=0):
            if not _state["running"]: return
            try:
                upd_progress.set((i % 20) / 20)
                popup.after(80, lambda: _pulse(i + 1))
            except Exception: pass
        popup.after(80, _pulse)

        def _worker():
            try:
                ytdlp = resource_path("yt-dlp.exe")
                proc  = subprocess.Popen(
                    [ytdlp, "-U"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    cwd=resource_path(""), creationflags=subprocess.CREATE_NO_WINDOW,
                    text=True, encoding="utf-8", errors="replace",
                )
                _state["proc"] = proc
                output = []
                for line in proc.stdout:
                    if _state["cancel"]:
                        proc.terminate()
                        popup.after(0, lambda: _finish("Отменено.", "error"))
                        return
                    line = line.strip()
                    if line: output.append(line)
                proc.wait()

                full = "\n".join(output).lower()
                if "up to date" in full or "последняя версия" in full:
                    popup.after(0, lambda: _finish("У вас установлена последняя версия.", "success"))
                elif proc.returncode == 0:
                    popup.after(0, lambda: _finish("Успешно обновлено!", "success"))
                else:
                    popup.after(0, lambda: _finish("Ошибка при обновлении.", "error"))

            except Exception as e:
                msg = f"Ошибка: {e}"
                popup.after(0, lambda m=msg: _finish(m, "error"))

        threading.Thread(target=_worker, daemon=True).start()

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

        self.title("RedStream: Video Downloader")
        self.withdraw()
        self.configure(fg_color="#1e1e1e")
        self.resizable(False, False)

        W, H = 820, 609
        sw, sh = self.winfo_screenwidth(), self.winfo_screenheight()
        self.geometry(f"{W}x{H}+{(sw - W) // 2}+{(sh - H) // 2}")

        try:
            self.iconbitmap(resource_path("icon.ico"))
        except Exception:
            pass

        self._toolbar = ctk.CTkFrame(self, fg_color="#151515", corner_radius=0, height=26)
        self._toolbar.pack(fill="x")
        self._toolbar.pack_propagate(False)
        self._settings_btn = ctk.CTkButton(
            self._toolbar, text="Настройки", width=79, height=24,
            fg_color="transparent", hover_color="#2a2a2a", text_color="#aaaaaa",
            font=("Segoe UI", 11), corner_radius=0,
            command=lambda: show_settings(self._settings_btn, self),
        )
        self._settings_btn.pack(side="left")
        ctk.CTkButton(
            self._toolbar, text="Обновить компоненты", width=146, height=24,
            fg_color="transparent", hover_color="#2a2a2a", text_color="#aaaaaa",
            font=("Segoe UI", 11), corner_radius=0,
            command=lambda: show_updater(self._root_frame, self),
        ).pack(side="left", padx=(0, 0))
        ctk.CTkButton(
            self._toolbar, text="О программе…", width=103, height=24,
            fg_color="transparent", hover_color="#2a2a2a", text_color="#aaaaaa",
            font=("Segoe UI", 11), corner_radius=0,
            command=lambda: show_about(self._root_frame),
        ).pack(side="left")
        ctk.CTkButton(
            self._toolbar, text="Свернуть приложение в трей", width=178, height=24,
            fg_color="transparent", hover_color="#2a2a2a", text_color="#aaaaaa",
            font=("Segoe UI", 11), corner_radius=0,
            command=self._minimize_to_tray,
        ).pack(side="right")
        ctk.CTkFrame(self, height=1, fg_color="#333333", corner_radius=0).pack(fill="x")

        self._root_frame = ctk.CTkFrame(self, fg_color="#1e1e1e", corner_radius=0)
        self._root_frame.pack(fill="both", expand=True)

        self._content = ctk.CTkFrame(self._root_frame, fg_color="#1e1e1e", corner_radius=0)
        self._content.pack(fill="both", expand=True)

        self._process  = None
        self._timer_id = None
        self._log_file  = os.path.join(os.environ.get("TEMP", "/tmp"), "yt_progress.txt")
        self._done_file = os.path.join(os.environ.get("TEMP", "/tmp"), "yt_done.txt")

        self._raw_formats = []

        self.VC_DISP = {"av01": "AV1", "vp9": "VP9", "avc1": "H.264", "h264": "H.264", "H264": "H.264", "hvc1": "H.265", "hev1": "H.265", "h265": "H.265", "H265": "H.265", "bytevc1": "H.265"}
        self.VC_RAW  = {"AV1": "av01", "VP9": "vp9", "H.264": "avc1", "H.265": "hvc1"}
        
        self.AC_DISP = {"opus": "OPUS", "mp4a": "AAC"}
        self.AC_RAW  = {"OPUS": "opus", "AAC": "mp4a"}

        self._dest_folder = load_dest_folder()
        self._downloading_video_stream = True

        self._build_ui()

        def _late_binds():
            inner = self.url_entry._entry

            def _ctrl_key(e):
                kc = e.keycode
                if kc == 65:
                    inner.selection_range(0, "end")
                    return "break"
                elif kc == 67:
                    if inner.selection_present():
                        self.clipboard_clear()
                        self.clipboard_append(inner.selection_get())
                    return "break"
                elif kc == 86:
                    self._paste_url()
                    return "break"
                elif kc == 88:
                    if inner.selection_present():
                        self.clipboard_clear()
                        self.clipboard_append(inner.selection_get())
                        inner.delete("sel.first", "sel.last")
                    return "break"
                elif kc == 90:
                    try: inner.event_generate("<<Undo>>")
                    except Exception: pass
                    return "break"

            inner.bind("<Control-KeyPress>", _ctrl_key, add="+")

            import tkinter as tk
            ctx = tk.Menu(inner, tearoff=0, bg="#2a2a2a", fg="white",
                          activebackground="#555555", activeforeground="white", bd=0, relief="flat")
            def _ctx_select_all():
                inner.selection_range(0, "end")
                inner.focus_set()
            def _ctx_copy():
                if inner.selection_present():
                    self.clipboard_clear()
                    self.clipboard_append(inner.selection_get())
            def _ctx_paste():
                self._paste_url()
            def _ctx_cut():
                if inner.selection_present():
                    self.clipboard_clear()
                    self.clipboard_append(inner.selection_get())
                    inner.delete("sel.first", "sel.last")

            ctx.add_command(label="Выделить всё",  command=_ctx_select_all)
            ctx.add_separator()
            ctx.add_command(label="Вырезать",      command=_ctx_cut)
            ctx.add_command(label="Копировать",    command=_ctx_copy)
            ctx.add_command(label="Вставить",      command=_ctx_paste)

            def _show_ctx(e):
                has_sel = inner.selection_present()
                ctx.entryconfigure("Вырезать",   state="normal" if has_sel else "disabled")
                ctx.entryconfigure("Копировать", state="normal" if has_sel else "disabled")
                try: ctx.tk_popup(e.x_root, e.y_root)
                finally: ctx.grab_release()

            inner.bind("<Button-3>", _show_ctx, add="+")

            _prev_url = [""]
            def _on_url_change(*_):
                url = self.url_entry.get().strip()

                if not url or url == _prev_url[0]: 
                    return

                if not url.startswith(("http://", "https://")):
                    if "." in url:
                        url = "https://" + url
                        self.url_entry.set(url)

                _prev_url[0] = url

                if url.startswith("http"):
                    self._gear_btn.configure(state="disabled", text="⚙", fg_color="#444444", hover_color="#444444")
                    self._preview_data = None
                    self._start_spinner()
                    self.after(100, lambda u=url: self._fetch_preview(u))
                else:
                    self._stop_spinner()
                    self._preview_data = None
                    self._gear_btn.configure(state="disabled", text="⚙", fg_color="#444444", hover_color="#444444")
            inner.bind("<KeyRelease>", _on_url_change, add="+")

            for cb in (self.browser_combo, self.format_combo,
                       self.vcodec_combo, self.acodec_combo, self.res_combo, self.fps_combo):
                self._bind_combo_anywhere(cb)

        self.after(200, _late_binds)

        def _show_main():
            self.deiconify()
            self.attributes("-topmost", True)
            self.after(400, lambda: self.attributes("-topmost", False))
            self.after(1500, self._check_update)

        show_splash_on_app(self, _show_main)

    def _set_combo_state(self, combo, state="readonly", values=None):
        if values is not None:
            combo.configure(values=values)
            if values:
                combo.set(values[0])
            else:
                combo.set("")

        if state == "disabled":
            combo.configure(
                state="disabled", fg_color="#252525", text_color="#666666",
                border_color="#333333", button_color="#333333", button_hover_color="#333333"
            )
            try:
                if hasattr(combo, "_canvas"): combo._canvas.configure(cursor="arrow")
                if hasattr(combo, "_entry"):  combo._entry.configure(cursor="arrow")
            except Exception: pass
        else:
            combo.configure(
                state="normal", fg_color="#333333", text_color="white",
                border_color="#555555", button_color="#555555", button_hover_color="#666666"
            )
            try:
                if hasattr(combo, "_canvas"): combo._canvas.configure(cursor="hand2")
                if hasattr(combo, "_entry"):  combo._entry.configure(cursor="hand2")
            except Exception: pass

    def _check_update(self):
        import urllib.request, json
        def _worker():
            try:
                req = urllib.request.Request(GITHUB_API_URL, headers={"User-Agent": "RedStream"})
                with urllib.request.urlopen(req, timeout=5) as r:
                    tag = json.loads(r.read().decode()).get("tag_name", "").lstrip("v")
                if tag and tag != APP_VERSION:
                    self.after(0, lambda t=tag: self._show_update_banner(t))
            except Exception: pass
            self.after(0, self._check_ytdlp_update)
        threading.Thread(target=_worker, daemon=True).start()

    def _check_ytdlp_update(self):
        def _worker():
            try:
                import urllib.request, json
                ytdlp = resource_path("yt-dlp.exe")
                result = subprocess.run(
                    [ytdlp, "--version"],
                    capture_output=True, text=True, encoding="utf-8",
                    creationflags=subprocess.CREATE_NO_WINDOW, timeout=10,
                )
                current = result.stdout.strip()
                if not current: return
                req = urllib.request.Request(
                    "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest",
                    headers={"User-Agent": "RedStream"}
                )
                with urllib.request.urlopen(req, timeout=10) as r:
                    latest = json.loads(r.read().decode()).get("tag_name", "").strip()
                if latest and latest != current:
                    self.after(0, lambda: self._show_ytdlp_banner(current, latest))
            except Exception: pass
            self.after(0, self._check_ffmpeg_update)
        threading.Thread(target=_worker, daemon=True).start()

    def _check_ffmpeg_update(self):
        def _worker():
            try:
                import urllib.request, re as _re
                ffmpeg = resource_path("ffmpeg.exe")

                result = subprocess.run(
                    [ffmpeg, "-version"],
                    capture_output=True, text=True, encoding="utf-8",
                    creationflags=subprocess.CREATE_NO_WINDOW, timeout=10,
                )
                output = result.stdout or result.stderr

                m_local = _re.search(r"(\d{4}-\d{2}-\d{2})", output)
                if not m_local: return
                current = m_local.group(1)

                req = urllib.request.Request(
                    "https://www.gyan.dev/ffmpeg/builds/ffmpeg-git-essentials.7z.ver",
                    headers={"User-Agent": "RedStream"}
                )
                with urllib.request.urlopen(req, timeout=10) as r:
                    online_ver = r.read().decode("utf-8").strip()

                m_online = _re.search(r"(\d{4}-\d{2}-\d{2})", online_ver)
                if not m_online: return
                latest = m_online.group(1)

                if latest and latest != current:
                    self.after(0, lambda c=current, l=latest: self._show_ffmpeg_banner(c, l))
            except Exception: pass
        threading.Thread(target=_worker, daemon=True).start()

    def _next_banner_pos(self):
        banners_on_root = sum(1 for b in [
            getattr(self, "_app_update_banner", None),
            getattr(self, "_ytdlp_update_banner", None),
            getattr(self, "_ffmpeg_update_banner", None),
        ] if b and b.winfo_exists() and b.master == self._root_frame)
        if banners_on_root > 0:
            return self._root_frame, banners_on_root * 26
        toolbar_full = any(b and b.winfo_exists() and b.master == self._toolbar for b in [
            getattr(self, "_app_update_banner", None),
            getattr(self, "_ytdlp_update_banner", None),
            getattr(self, "_ffmpeg_update_banner", None),
        ])
        if toolbar_full:
            return self._root_frame, 0
        return self._toolbar, 0

    def _show_ffmpeg_banner(self, current, latest):
        parent, y_off = self._next_banner_pos()
        banner = ctk.CTkFrame(parent, height=26, fg_color="#1a5a8a", corner_radius=0)
        banner.place(x=0, y=y_off, relwidth=1.0)
        self._ffmpeg_update_banner = banner
        row = ctk.CTkFrame(banner, fg_color="transparent")
        row.place(relx=0.5, rely=0.5, anchor="center")
        ctk.CTkLabel(row, text=f"ffmpeg устарел ({current} \u2192 {latest}). Нажмите ",
                     text_color="#ffffff", font=("Segoe UI", 11), fg_color="transparent").pack(side="left")
        ctk.CTkButton(row, text="обновить", fg_color="transparent", hover_color="#0d3d63",
                      text_color="#88ddff", font=("Segoe UI", 11, "underline", "bold"),
                      height=20, width=58, cursor="hand2",
                      command=lambda: [banner.destroy(), show_updater(self._root_frame, self)],
                      ).pack(side="left")
        ctk.CTkLabel(row, text=", чтобы продолжить.", text_color="#ffffff",
                     font=("Segoe UI", 11), fg_color="transparent").pack(side="left")

    def _show_ytdlp_banner(self, current, latest):
        parent, y_off = self._next_banner_pos()
        banner = ctk.CTkFrame(parent, height=26, fg_color="#1a6b1a", corner_radius=0)
        banner.place(x=0, y=y_off, relwidth=1.0)
        self._ytdlp_update_banner = banner
        row = ctk.CTkFrame(banner, fg_color="transparent")
        row.place(relx=0.5, rely=0.5, anchor="center")
        ctk.CTkLabel(row, text=f"yt-dlp устарел ({current} → {latest}). Нажмите ",
                     text_color="#ffffff", font=("Segoe UI", 11), fg_color="transparent").pack(side="left")
        ctk.CTkButton(row, text="обновить", fg_color="transparent", hover_color="#145214",
                      text_color="#00ff88", font=("Segoe UI", 11, "underline", "bold"),
                      height=20, width=58, cursor="hand2",
                      command=lambda: [banner.destroy(), show_updater(self._root_frame, self)],
                      ).pack(side="left")
        ctk.CTkLabel(row, text=", чтобы продолжить.", text_color="#ffffff",
                     font=("Segoe UI", 11), fg_color="transparent").pack(side="left")

    def _show_update_banner(self, new_version):
        import webbrowser
        parent, y_off = self._next_banner_pos()
        banner = ctk.CTkFrame(parent, height=26, fg_color="#ffaa00", corner_radius=0)
        banner.place(x=0, y=y_off, relwidth=1.0)
        self._app_update_banner = banner
        row = ctk.CTkFrame(banner, fg_color="transparent")
        row.place(relx=0.5, rely=0.5, anchor="center")
        ctk.CTkLabel(row, text=f"Вышла новая версия «{new_version}». Нажмите ", text_color="#1e1e1e", font=("Segoe UI", 11), fg_color="transparent").pack(side="left")
        ctk.CTkButton(row, text="сюда", fg_color="transparent", hover_color="#e59900", text_color="#ff0000", font=("Segoe UI", 11, "underline", "bold"), height=20, width=36, cursor="hand2", command=lambda: webbrowser.open(GITHUB_RELEASES_URL)).pack(side="left")
        ctk.CTkLabel(row, text=", чтобы обновить.", text_color="#1e1e1e", font=("Segoe UI", 11), fg_color="transparent").pack(side="left")

    def _minimize(self):
        self.iconify()

    def _minimize_to_tray(self):
        try:
            import pystray
            from PIL import Image as PilImage
        except ImportError:
            self._show_error("Для трея нужен модуль pystray.\nУстановите: pip install pystray")
            return

        self.withdraw()

        def _restore(icon, item):
            icon.stop()
            self.after(0, self._show_from_tray)

        def _open_updater(icon, item):
            icon.stop()
            self.after(0, self._show_from_tray)
            self.after(200, lambda: show_updater(self._root_frame, self))

        def _open_about(icon, item):
            icon.stop()
            self.after(0, self._show_from_tray)
            self.after(200, lambda: show_about(self._root_frame))

        def _quit(icon, item):
            icon.stop()
            self.after(0, self.destroy)

        try:
            img = PilImage.open(resource_path("icon.ico")).resize((64, 64))
        except Exception:
            img = PilImage.new("RGBA", (64, 64), "#ff0000")

        menu = pystray.Menu(
            pystray.MenuItem("Открыть RedStream", _restore, default=True),
            pystray.MenuItem("Обновление компонентов", _open_updater),
            pystray.MenuItem("О программе…", _open_about),
            pystray.MenuItem("Выход", _quit),
        )
        self._tray_icon = pystray.Icon("RedStream", img, "RedStream", menu)
        threading.Thread(target=self._tray_icon.run, daemon=True).start()

    def _show_from_tray(self):
        self.deiconify()
        self.lift()
        self.focus_force()

    def _build_ui(self):
        p = self._content

        ctk.CTkLabel(
            p, text="RedStream: Video Downloader",
            text_color="#ff0000", font=("Segoe UI", 16, "bold")
        ).pack(pady=(10, 6), padx=20)

        columns = ctk.CTkFrame(p, fg_color="transparent")
        columns.pack(fill="x", padx=10, pady=(0, 6))
        columns.columnconfigure(0, weight=1)
        columns.columnconfigure(1, weight=0)
        columns.columnconfigure(2, weight=1)

        left = ctk.CTkFrame(columns, fg_color="transparent")
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 6))

        ctk.CTkFrame(columns, fg_color="#333333", width=1).grid(row=0, column=1, sticky="ns", pady=4)

        right = ctk.CTkFrame(columns, fg_color="transparent")
        right.grid(row=0, column=2, sticky="nsew", padx=(6, 0))

        ctk.CTkLabel(left, text="Ссылка на видео/плейлист:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        url_row = ctk.CTkFrame(left, fg_color="transparent")
        url_row.pack(fill="x", pady=(2, 8))
        url_row.columnconfigure(0, weight=1)
        _history = load_history_combo()
        self.url_entry = ctk.CTkComboBox(
            url_row, values=_history if _history else [""],
            fg_color="#333333", border_color="#555555", text_color="white", font=("Segoe UI", 12),
            button_color="#555555", button_hover_color="#666666", dropdown_fg_color="#2a2a2a",
            dropdown_text_color="white", dropdown_hover_color="#444444", state="normal",
            command=self._on_history_select,
        )
        self.url_entry.set("")
        self.url_entry.grid(row=0, column=0, sticky="ew")
        paste_btn = ctk.CTkButton(
            url_row, text="\u29be", width=28, height=28, fg_color="#555555", hover_color="#666666",
            corner_radius=5, font=("Segoe UI", 14), command=self._paste_url,
        )
        paste_btn.grid(row=0, column=1, padx=(4, 0))
        self._add_tooltip(paste_btn, "Вставить ссылку\nс буфера обмена")
        self._gear_btn = ctk.CTkButton(
            url_row, text="\u2699", width=28, height=28, fg_color="#444444", hover_color="#444444",
            corner_radius=5, font=("Segoe UI", 12), state="disabled",
            command=self._show_download_settings,
        )
        self._gear_btn.grid(row=0, column=2, padx=(4, 0))
        self._add_tooltip(self._gear_btn, "Настройки скачивания")

        ctk.CTkLabel(left, text="Браузер для авторизации:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        ctk.CTkLabel(left, text="Это позволяет загружать видео с возрастными ограничениями.",
                     text_color="#808080", font=("Segoe UI", 10), anchor="w", height=14).pack(fill="x")
        self.browser_combo = make_combo(
            left,
            values=["Без авторизации", "Chrome", "Chromium", "Opera", "Opera GX",
                    "Edge", "Firefox", "Brave", "Vivaldi", "Whale"],
            default="Без авторизации",
        )
        self.browser_combo.pack(fill="x", pady=(2, 2))
        ctk.CTkLabel(left, text="Перед скачиванием браузер должен быть закрыт.",
                     text_color="#ff0000", font=("Segoe UI", 10), anchor="w", height=14).pack(fill="x")
        ctk.CTkLabel(left, text="Если видео не имеет возрастные ограничения, выберите «Без авторизации».",
                     text_color="#00bf00", font=("Segoe UI", 10), anchor="w", height=14).pack(fill="x")

        ctk.CTkFrame(left, fg_color="#333333", height=1).pack(fill="x", pady=(7, 8))
        ctk.CTkLabel(left, text="Контейнер/Формат:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        self._format_map = {
            "Видео: MKV": "mkv",
            "Видео: MP4": "mp4",
            "Аудио: OPUS": "opus",
            "Аудио: M4A (с аудиокодеком AAC)": "m4a",
            "Аудио: MP3 (конвертация с аудиокодека OPUS)": "mp3",
        }
        self.format_combo = make_combo(
            left,
            values=list(self._format_map.keys()),
            default="Видео: MP4",
            command=self._on_format_change,
        )
        self.format_combo.pack(fill="x", pady=(2, 2))
        ctk.CTkLabel(left, text="Обратите внимание: в процессе конвертации размер файла может измениться по сравнению со скачанным!",
                     text_color="#ff0000", font=("Segoe UI", 10), anchor="w", height=14).pack(fill="x")

        ctk.CTkLabel(right, text="Видеокодек:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        self.vcodec_combo = make_combo(right, values=[""], default="", command=self._calc_filesize)
        self._set_combo_state(self.vcodec_combo, "disabled")
        self.vcodec_combo.pack(fill="x", pady=(2, 8))

        ctk.CTkLabel(right, text="Аудиокодек:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        self.acodec_combo = make_combo(right, values=[""], default="", command=self._calc_filesize)
        self._set_combo_state(self.acodec_combo, "disabled")
        self.acodec_combo.pack(fill="x", pady=(2, 8))

        ctk.CTkLabel(right, text="Разрешение:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        self.res_combo = make_combo(right, values=[""], default="", command=self._calc_filesize)
        self._set_combo_state(self.res_combo, "disabled")
        self.res_combo.pack(fill="x", pady=(2, 8))

        ctk.CTkLabel(right, text="Кадры в секунду:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).pack(fill="x")
        self.fps_combo = make_combo(right, values=[""], default="", command=self._calc_filesize)
        self._set_combo_state(self.fps_combo, "disabled")
        self.fps_combo.pack(fill="x", pady=(2, 8))

        ctk.CTkFrame(p, fg_color="#333333", height=1).pack(fill="x", padx=10)

        bottom = ctk.CTkFrame(p, fg_color="transparent")
        bottom.pack(fill="x", padx=10, pady=(8, 0))
        bottom.columnconfigure(0, weight=1)

        pill_row = ctk.CTkFrame(bottom, fg_color="transparent")
        pill_row.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 2))
        pill_row.columnconfigure(1, weight=1)
        pill_left = ctk.CTkEntry(
            pill_row, fg_color="#3a3a3a", border_color="#555555",
            text_color="#aaaaaa", font=("Segoe UI", 11, "bold"), state="normal",
            justify="left", width=154,
        )
        pill_left.insert(0, "Итоговый размер файла:")
        pill_left.configure(state="disabled")
        pill_left._entry.configure(cursor="arrow")
        pill_left.pack(side="left", padx=(0, 4))
        self.filesize_label = ctk.CTkEntry(
            pill_row, fg_color="#333333", border_color="#555555",
            text_color="#00bf00", font=("Segoe UI", 11, "bold"), state="normal",
            justify="left",
        )
        self.filesize_label.configure(state="disabled")
        self.filesize_label._entry.configure(cursor="arrow")
        self.filesize_label.pack(side="left", fill="x", expand=True)
        self.filesize_warn_label = ctk.CTkLabel(
            bottom, text="",
            text_color="#ff0000", font=("Segoe UI", 10), anchor="w", height=14,
        )
        self.filesize_warn_label.grid(row=1, column=0, columnspan=2, sticky="w")

        ctk.CTkLabel(bottom, text="Папка для сохранения:",
                     text_color="#aaaaaa", font=("Segoe UI", 11), anchor="w", height=14).grid(
                         row=2, column=0, columnspan=2, sticky="w")
        folder_row = ctk.CTkFrame(bottom, fg_color="transparent")
        folder_row.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(2, 8))
        folder_row.columnconfigure(0, weight=1)
        self.folder_entry = ctk.CTkEntry(
            folder_row, fg_color="#333333", border_color="#555555",
            text_color="#cccccc", font=("Segoe UI", 11), state="disabled",
        )
        self.folder_entry.grid(row=0, column=0, sticky="ew")
        self.folder_entry.configure(state="normal")
        self.folder_entry.insert(0, self._dest_folder)
        self.folder_entry.configure(state="disabled")
        self.folder_entry._entry.configure(cursor="arrow")
        browse_btn = ctk.CTkButton(
            folder_row, text="📁", width=28, height=28, fg_color="#555555", hover_color="#666666",
            corner_radius=5, font=("Segoe UI", 13), command=self._browse_folder,
        )
        browse_btn.grid(row=0, column=1, padx=(4, 0))
        self._add_tooltip(browse_btn, "Выбрать папку\nдля сохранения")

        self.download_btn = ctk.CTkButton(
            p, text="СКАЧАТЬ", font=("Segoe UI", 14, "bold"),
            fg_color="#ff0000", hover_color="#bf0000",
            corner_radius=10, height=44, command=self._run_download,
        )
        self.download_btn.pack(fill="x", padx=10, pady=(16, 4))

        ctk.CTkLabel(
            p, text="Если программа не отвечает на 100%, идёт финальная обработка файла.",
            text_color="#808080", font=("Segoe UI", 10), height=14, justify="center", wraplength=780,
        ).pack(pady=(0, 0), padx=10)

        self.progress_frame = ctk.CTkFrame(p, fg_color="transparent")
        self.progress_bar = ctk.CTkProgressBar(
            self.progress_frame, fg_color="#333333", progress_color="#333333",
            border_color="#555555", border_width=1, height=18, corner_radius=5,
        )
        self.progress_bar.set(0)
        self.progress_bar.pack(fill="x", pady=(8, 0))
        self.progress_label = ctk.CTkLabel(
            self.progress_frame, text="Вставьте ссылку на видео или плейлист, и нажмите «СКАЧАТЬ».", text_color="#888888", font=("Segoe UI", 11),
        )
        self.progress_label.pack()

        self.progress_frame.pack(fill="x", padx=10, pady=(8, 0))

        self.open_folder_btn = ctk.CTkButton(
            p, text="ОТКРЫТЬ ПАПКУ СКАЧАННОГО ФАЙЛА", font=("Segoe UI", 12, "bold"),
            fg_color="#333333", hover_color="#333333", text_color="#666666",
            corner_radius=10, height=44, state="disabled",
            command=self._open_folder,
        )
        self.open_folder_btn.pack(fill="x", padx=10, pady=(8, 0))

    def _show_download_settings(self):
        existing = getattr(self, "_dl_settings_popup", None)
        if existing:
            try: existing.place_forget(); existing.destroy()
            except Exception: pass
            self._dl_settings_popup = None
            return

        popup = ctk.CTkFrame(self._root_frame, fg_color="#2a2a2a", corner_radius=0,
                             border_width=1, border_color="#555555")
        self._dl_settings_popup = popup

        def _close():
            try: popup.place_forget(); popup.destroy()
            except Exception: pass
            self._dl_settings_popup = None

        ctk.CTkButton(popup, text="✕", width=28, height=18,
                      fg_color="transparent", hover_color="#c42b1c",
                      text_color="#bbbbbb", font=("Segoe UI", 12),
                      corner_radius=0, command=_close,
                      ).place(relx=1.0, rely=0.0, anchor="ne", x=-1, y=1)

        ctk.CTkLabel(popup, text="Настройки скачивания",
                     text_color="#ff0000", font=("Segoe UI", 13, "bold")).pack(pady=(14, 8), padx=20)
        ctk.CTkFrame(popup, height=1, fg_color="#444444", corner_radius=0).pack(fill="x", padx=20, pady=(0, 10))

        import urllib.request as _ureq
        from io import BytesIO as _BytesIO
        d = getattr(self, "_preview_data", None)
        if d:
            thumb_url = d.get("thumb_url", "")
            title     = d.get("title", "")
            duration  = d.get("duration")
            if thumb_url:
                def _load_thumb():
                    try:
                        req = _ureq.Request(thumb_url, headers={"User-Agent": "Mozilla/5.0"})
                        with _ureq.urlopen(req, timeout=8) as r:
                            img_data = r.read()
                        img = Image.open(_BytesIO(img_data))
                        img.thumbnail((280, 158))
                        ctk_img = ctk.CTkImage(light_image=img, dark_image=img, size=img.size)
                        self.after(0, lambda: thumb_label.configure(image=ctk_img, text=""))
                        self.after(0, lambda: setattr(thumb_label, "_img_ref", ctk_img))
                    except Exception: pass
                thumb_label = ctk.CTkLabel(popup, text="", fg_color="transparent")
                thumb_label.pack(padx=16, pady=(0, 6))
                threading.Thread(target=_load_thumb, daemon=True).start()
            if title:
                short = title if len(title) <= 46 else title[:43] + "…"
                ctk.CTkLabel(popup, text=short, text_color="white",
                             font=("Segoe UI", 11, "bold"), wraplength=268, justify="center").pack(padx=16, pady=(0, 2))
            if duration:
                mins, secs = divmod(int(duration), 60)
                hrs,  mins = divmod(mins, 60)
                dur_str = f"{hrs}:{mins:02d}:{secs:02d}" if hrs else f"{mins}:{secs:02d}"
                ctk.CTkLabel(popup, text=f"⏱ {dur_str}", text_color="#aaaaaa",
                             font=("Segoe UI", 11)).pack(pady=(0, 0))
            ctk.CTkFrame(popup, height=1, fg_color="#444444", corner_radius=0).pack(fill="x", padx=20, pady=(0, 0))

        ctk.CTkLabel(popup, text="Обрезка видео:",
                     text_color="#aaaaaa", font=("Segoe UI", 11, "bold"), anchor="w").pack(fill="x", padx=20)
        ctk.CTkLabel(popup, text="Оставьте пустым чтобы скачать целиком.",
                     text_color="#666666", font=("Segoe UI", 10), anchor="w").pack(fill="x", padx=20, pady=(0, 6))

        def _make_timecode_widget(parent):
            frame = ctk.CTkFrame(parent, fg_color="#333333", border_color="#555555",
                                 border_width=2, corner_radius=6)
            fields = []
            maxlens = [2, 2, 2]
            for i, (ml, ph) in enumerate(zip(maxlens, ["ЧЧ", "ММ", "СС"])):
                var = ctk.StringVar()
                e = ctk.CTkEntry(frame, textvariable=var, width=28, fg_color="transparent",
                                 border_width=0, text_color="white", font=("Segoe UI", 11),
                                 justify="center", placeholder_text=ph,
                                 placeholder_text_color="#555555")
                e.pack(side="left", padx=(4 if i == 0 else 0, 0))
                fields.append((e, var, ml))
                if i < 2:
                    ctk.CTkLabel(frame, text=":", text_color="#aaaaaa",
                                 font=("Segoe UI", 11), width=8).pack(side="left")

            def _validate(idx, sv, ml):
                v = sv.get()
                digits = "".join(c for c in v if c.isdigit())
                if len(digits) > ml:
                    digits = digits[:ml]
                if digits != v:
                    sv.set(digits)
                if len(digits) == ml and idx < len(fields) - 1:
                    fields[idx + 1][0].focus_set()

            for idx, (e, var, ml) in enumerate(fields):
                var.trace_add("write", lambda *_, i=idx, sv=var, m=ml: _validate(i, sv, m))

            def get_value():
                parts = [var.get().zfill(2) for _, var, _ in fields]
                if any(p != "00" for p in parts):
                    return ":".join(parts)
                return ""

            def clear():
                for _, var, _ in fields:
                    var.set("")

            frame.get_value = get_value
            frame.clear = clear
            frame._fields = fields
            return frame

        trim_row = ctk.CTkFrame(popup, fg_color="transparent")
        trim_row.pack(fill="x", padx=20, pady=(0, 8))

        ctk.CTkLabel(trim_row, text="Начало:", text_color="#aaaaaa",
                     font=("Segoe UI", 11), width=52, anchor="w").pack(side="left")
        self._trim_start = _make_timecode_widget(trim_row)
        self._trim_start.pack(side="left", padx=(0, 12))

        ctk.CTkLabel(trim_row, text="Конец:", text_color="#aaaaaa",
                     font=("Segoe UI", 11), width=48, anchor="w").pack(side="left")
        self._trim_end = _make_timecode_widget(trim_row)
        self._trim_end.pack(side="left")

        def _restore_val(widget, val):
            if not val: return
            parts = val.split(":")
            for (e, var, _), part in zip(widget._fields, parts):
                var.set(part.lstrip("0") or "")
        _restore_val(self._trim_start, getattr(self, "_trim_start_val", ""))
        _restore_val(self._trim_end,   getattr(self, "_trim_end_val",   ""))

        orig_close = _close
        def _close_and_save():
            self._trim_start_val = self._trim_start.get_value()
            self._trim_end_val   = self._trim_end.get_value()
            orig_close()
        for w in popup.winfo_children():
            if isinstance(w, ctk.CTkButton) and w.cget("text") == "✕":
                w.configure(command=_close_and_save)
                break

        def _reset_trim():
            self._trim_start.clear()
            self._trim_end.clear()
            self._trim_start_val = ""
            self._trim_end_val   = ""
        ctk.CTkButton(popup, text="✕  Сбросить обрезку",
                      fg_color="transparent", hover_color="#3a3a3a",
                      text_color="#aaaaaa", font=("Segoe UI", 10),
                      anchor="center", height=22, corner_radius=0,
                      command=_reset_trim,
                      ).pack(pady=(0, 14))

        popup.update_idletasks()
        pw = self._root_frame.winfo_width()
        ph = self._root_frame.winfo_height()
        ww = popup.winfo_reqwidth()
        wh = popup.winfo_reqheight()
        popup.place(x=(pw - ww) // 2, y=(ph - wh) // 2)
        popup.lift()

    def _section(self, parent):
        outer = ctk.CTkFrame(parent, fg_color="transparent")
        outer.pack(fill="x", padx=10)
        inner = ctk.CTkFrame(outer, fg_color="transparent")
        inner.pack(fill="x", pady=(0, 6))
        ctk.CTkFrame(outer, fg_color="#333333", height=1).pack(fill="x")
        ctk.CTkFrame(outer, fg_color="transparent", height=6).pack()
        return inner

    def _add_tooltip(self, widget, text):
        tip_win = [None]
        after_id = [None]

        def show(e):
            if tip_win[0]: return
            if after_id[0]:
                try: widget.after_cancel(after_id[0])
                except Exception: pass
            def _create():
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
            after_id[0] = widget.after(400, _create)

        def hide(e):
            if after_id[0]:
                try: widget.after_cancel(after_id[0])
                except Exception: pass
                after_id[0] = None
            if tip_win[0]:
                try: tip_win[0].destroy()
                except Exception: pass
                tip_win[0] = None

        widget.bind("<Enter>", show, add="+")
        widget.bind("<Leave>", hide, add="+")
        widget.bind("<Button-1>", hide, add="+")
        for w in widget.winfo_children():
            try:
                w.bind("<Enter>", show, add="+")
                w.bind("<Leave>", hide, add="+")
                w.bind("<Button-1>", hide, add="+")
            except Exception: pass

    def _bind_combo_anywhere(self, combo):
        def _open(e=None):
            if combo.cget("state") == "disabled": return
            combo._open_dropdown_menu()
        try:
            if hasattr(combo, "_entry") and hasattr(combo._entry, "bind"):
                combo._entry.bind("<Button-1>", _open, add="+")
            for w in combo.winfo_children():
                w.bind("<Button-1>", _open, add="+")
        except Exception: pass

    def _on_history_select(self, url):
        if url and url.startswith("http"):
            self._gear_btn.configure(state="disabled", text="⚙", fg_color="#444444", hover_color="#444444")
            self._preview_data = None
            self._start_spinner()
            self.after(100, lambda u=url: self._fetch_preview(u))

    def _paste_url(self):
        try: text = self.clipboard_get()
        except Exception: return
        url = text.strip()
        if url and not (url.startswith("http://") or url.startswith("https://")):
            url = "https://" + url
            text = url
        inner = self.url_entry._entry
        inner.delete(0, "end")
        inner.insert(0, text)
        self.url_entry.focus_set()
        if url.startswith("http"):
            self._preview_data = None
            self._gear_btn.configure(state="disabled", text="⚙", fg_color="#444444", hover_color="#444444")
            self._start_spinner()
            self.after(100, lambda: self._fetch_preview(url))

    def _start_spinner(self):
        self._spinner_frames = ["⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"]
        self._spinner_idx    = 0
        self._spinner_running = True
        self._spin_step()

    def _spin_step(self):
        if not getattr(self, "_spinner_running", False): return
        self._gear_btn.configure(text=self._spinner_frames[self._spinner_idx % len(self._spinner_frames)])
        self._spinner_idx += 1
        self._spinner_id = self.after(100, self._spin_step)

    def _stop_spinner(self):
        self._spinner_running = False
        if hasattr(self, "_spinner_id"):
            try: self.after_cancel(self._spinner_id)
            except Exception: pass
        try: self._gear_btn.configure(text="⚙")
        except Exception: pass

    def _fetch_preview(self, url):
        def _worker():
            try:
                ytdlp = resource_path("yt-dlp.exe")
                result = subprocess.run(
                    [ytdlp, "--dump-json", "--no-playlist", url],
                    capture_output=True, text=True, encoding="utf-8",
                    errors="replace", creationflags=subprocess.CREATE_NO_WINDOW,
                    timeout=15,
                )
                if result.returncode != 0:
                    self.after(0, self._stop_spinner)
                    self.after(0, lambda: self._gear_btn.configure(state="disabled", text="⚙", fg_color="#444444", hover_color="#444444"))
                    return
                import json
                data = json.loads(result.stdout)
                
                self._preview_data = {
                    "title":     data.get("title", ""),
                    "duration":  data.get("duration"),
                    "thumb_url": data.get("thumbnail", ""),
                }

                self._raw_formats = data.get("formats", [])
                exts = set()
                vcodecs = set()
                acodecs = set()
                resolutions = set()
                fps_set = set()

                for f in self._raw_formats:
                    if f.get("vcodec") == "images": continue
                    if f.get("vcodec") == "none" and f.get("acodec") == "none": continue

                    e = f.get("ext")
                    if e: exts.add(e)

                    vc = f.get("vcodec")
                    if vc and vc != "none": vcodecs.add(vc.split(".")[0])

                    ac = f.get("acodec")
                    if ac and ac != "none": acodecs.add(ac.split(".")[0])

                    res = f.get("resolution")
                    h = f.get("height")
                    w = f.get("width")
                    if h and h > 0:
                        resolutions.add(f"{w}x{h}" if w else f"{h}p")
                    elif res and res not in ("audio only", "x"):
                        resolutions.add(res)

                    fps = f.get("fps")
                    if fps: fps_set.add(int(fps))

                ext_list = list(exts)
                for extra in ["mkv", "mp3", "opus"]:
                    if extra not in ext_list: ext_list.append(extra)
                ext_list = sorted(list(set(ext_list)))

                V_ORDER = {"av01": 0, "vp9": 1, "avc1": 2}
                A_ORDER = {"opus": 0, "mp4a": 1}

                v_raw_list = sorted(list(vcodecs), key=lambda x: V_ORDER.get(x, 999)) if vcodecs else []
                a_raw_list = sorted(list(acodecs), key=lambda x: A_ORDER.get(x, 999)) if acodecs else []

                vcodecs_list = [self.VC_DISP.get(vc, vc.upper()) for vc in v_raw_list] if v_raw_list else ["-"]
                if "AV1" not in vcodecs_list:
                    vcodecs_list.insert(0, "AV1")
                acodecs_list = [self.AC_DISP.get(ac, ac.upper()) for ac in a_raw_list] if a_raw_list else ["-"]

                def res_key(r):
                    m = re.search(r'x(\d+)', r)
                    if m: return int(m.group(1))
                    m = re.search(r'\d+', r)
                    if m: return int(m.group(0))
                    return 0
                resolutions_list = sorted(list(resolutions), key=res_key, reverse=True) if resolutions else ["-"]
                fps_list = [str(x) for x in sorted(list(fps_set), reverse=True)] if fps_set else ["-"]

                self.after(0, lambda: self._update_format_combos(ext_list, vcodecs_list, acodecs_list, resolutions_list, fps_list))
                
                self.after(0, self._stop_spinner)
                self.after(0, lambda: self._gear_btn.configure(state="normal", text="⚙", fg_color="#555555", hover_color="#666666"))
            except Exception:
                self.after(0, self._stop_spinner)
                self.after(0, lambda: self._gear_btn.configure(state="disabled", text="⚙", fg_color="#444444", hover_color="#444444"))
        threading.Thread(target=_worker, daemon=True).start()

    def _update_format_combos(self, exts, vcs, acs, ress, fpss):
        self._saved_vcs  = vcs
        self._saved_acs  = acs
        self._saved_ress = ress
        self._saved_fpss = fpss

        for cb in (self.vcodec_combo, self.acodec_combo, self.res_combo, self.fps_combo):
            self._set_combo_state(cb, "readonly")

        self.vcodec_combo.configure(values=vcs);  self.vcodec_combo.set(vcs[0])
        self.acodec_combo.configure(values=acs);  self.acodec_combo.set(acs[0])
        self.res_combo.configure(values=ress);    self.res_combo.set(ress[0])
        self.fps_combo.configure(values=fpss);    self.fps_combo.set(fpss[0])

        self._on_format_change()

    def _browse_folder(self):
        initial = self._dest_folder if os.path.isdir(self._dest_folder) else os.path.dirname(self._dest_folder)
        chosen = filedialog.askdirectory(title="Выберите папку для сохранения", initialdir=initial, parent=self)
        if chosen:
            self._dest_folder = chosen
            save_dest_folder(chosen)
            self.folder_entry.configure(state="normal")
            self.folder_entry.delete(0, "end")
            self.folder_entry.insert(0, self._dest_folder)
            self.folder_entry.configure(state="disabled")

    def _on_format_change(self, *_):
        fmt = self._format_map.get(self.format_combo.get(), "mp4")
        has_data = bool(getattr(self, "_raw_formats", None))
        is_audio = fmt in ("mp3", "m4a", "opus")

        if is_audio:
            for cb in (self.vcodec_combo, self.res_combo, self.fps_combo):
                self._set_combo_state(cb, "disabled", values=[""])
            
            ac_val_raw = {"opus": "opus", "mp3": "opus", "m4a": "mp4a"}.get(fmt, "opus")
            ac_val_disp = self.AC_DISP.get(ac_val_raw, ac_val_raw.upper())
            
            self._set_combo_state(self.acodec_combo, "disabled", values=[ac_val_disp])
        else:
            if has_data:
                vcs  = getattr(self, "_saved_vcs",  None)
                acs  = getattr(self, "_saved_acs",  None)
                ress = getattr(self, "_saved_ress", None)
                fpss = getattr(self, "_saved_fpss", None)
                
                for cb in (self.vcodec_combo, self.acodec_combo, self.res_combo, self.fps_combo):
                    self._set_combo_state(cb, "readonly")
                    
                if vcs:  self.vcodec_combo.configure(values=vcs);  self.vcodec_combo.set(vcs[0])
                if acs:  self.acodec_combo.configure(values=acs);  self.acodec_combo.set(acs[0])
                if ress: self.res_combo.configure(values=ress);    self.res_combo.set(ress[0])
                if fpss: self.fps_combo.configure(values=fpss);    self.fps_combo.set(fpss[0])
            else:
                for cb in (self.vcodec_combo, self.acodec_combo, self.res_combo, self.fps_combo):
                    self._set_combo_state(cb, "disabled")

        self._calc_filesize()

    def _calc_filesize(self, *_):
        if not getattr(self, "_raw_formats", None):
            self.filesize_label.configure(state="normal"); self.filesize_label.delete(0, "end"); self.filesize_label.configure(state="disabled")
            return

        sel_ext = self._format_map.get(self.format_combo.get(), "mp4")
        
        sel_vc_disp = self.vcodec_combo.get()
        sel_ac_disp = self.acodec_combo.get()
        sel_res = self.res_combo.get()
        sel_fps = self.fps_combo.get()

        sel_vc = self.VC_RAW.get(sel_vc_disp, sel_vc_disp.lower()) if sel_vc_disp != "-" else "-"
        sel_ac = self.AC_RAW.get(sel_ac_disp, sel_ac_disp.lower()) if sel_ac_disp != "-" else "-"

        is_audio_only = sel_ext in ("mp3", "m4a", "opus")
        
        v_size = 0
        a_size = 0
        best_v = None
        best_a = None

        def get_weight(f):
            return f.get('tbr') or f.get('vbr') or f.get('abr') or f.get('filesize') or f.get('filesize_approx') or 0

        if not is_audio_only:
            valid_v = []
            for f in self._raw_formats:
                vc = f.get('vcodec', 'none')
                if vc == 'none' or vc == 'images': continue
                
                res = f.get('resolution', '')
                fps = str(f.get('fps', ''))

                VC_ALIASES = {"avc1": ["avc1", "h264", "h.264"], "hvc1": ["hvc1", "hev1", "h265", "h.265", "bytevc1"], "vp9": ["vp9"], "av01": ["av01"]}
                vc_variants = VC_ALIASES.get(sel_vc, [sel_vc])
                vc_match = any(alias in vc.lower() for alias in vc_variants)
                f_h = f.get("height", 0)
                try:
                    import re as _re
                    m = _re.search(r"x(\d+)", sel_res) or _re.search(r"(\d+)", sel_res)
                    res_h = int(m.group(1)) if m and sel_res not in ("-", "") else 0
                except Exception:
                    res_h = 0
                res_match = (res_h == 0 or f_h == res_h)
                try:
                    fps_match = sel_fps in ("-", "", None) or int(float(fps)) == int(sel_fps)
                except Exception:
                    fps_match = True
                if vc_match and res_match and fps_match:
                    valid_v.append(f)
            
            if valid_v:
                best_v = max(valid_v, key=get_weight)
                v_size = best_v.get('filesize') or best_v.get('filesize_approx') or 0

        valid_a = []
        for f in self._raw_formats:
            ac = f.get('acodec', 'none')
            vc = f.get('vcodec', 'none')
            if vc == 'none' and ac != 'none':
                if sel_ac in ac:
                    valid_a.append(f)
        
        if valid_a:
            best_a = max(valid_a, key=get_weight)
            a_size = best_a.get('filesize') or best_a.get('filesize_approx') or 0

        total_size = 0
        if is_audio_only:
            total_size = a_size
        else:
            if best_v and best_v.get('acodec') != 'none':
                total_size = v_size
            else:
                total_size = v_size + a_size

        av1_warn = ""
        if not is_audio_only and sel_vc == "av01":
            av1_available = any(
                f.get("vcodec", "").startswith("av01")
                for f in self._raw_formats
            )
            if not av1_available and not valid_v:
                V_PREF = ["vp9", "avc1", "hvc1", "hev1"]
                fallback_v = [
                    f for f in self._raw_formats
                    if f.get("vcodec", "none") not in ("none", "images")
                    and sel_res in f.get("resolution", "")
                    and (not sel_fps or sel_fps == "-" or str(f.get("fps", "")) == sel_fps)
                ]
                if fallback_v:
                    best_v = max(fallback_v, key=lambda f: (
                        -V_PREF.index(f.get("vcodec", "").split(".")[0])
                        if f.get("vcodec", "").split(".")[0] in V_PREF else -999,
                        get_weight(f)
                    ))
                    v_size = best_v.get("filesize") or best_v.get("filesize_approx") or 0
                    if best_v.get("acodec") != "none":
                        total_size = v_size
                    else:
                        total_size = v_size + a_size
                    actual_vc = best_v.get("vcodec", "").split(".")[0]
                    actual_name = self.VC_DISP.get(actual_vc, actual_vc.upper())
                    av1_warn = f"AV1 недоступен, вместо этого будет скачан {actual_name}"

        if total_size > 0:
            mb = total_size / (1024 * 1024)
            self.filesize_label.configure(state="normal"); self.filesize_label.delete(0, "end"); self.filesize_label.insert(0, f"~{mb:.2f} МБ"); self.filesize_label.configure(state="disabled")
        else:
            self.filesize_label.configure(state="normal"); self.filesize_label.delete(0, "end"); self.filesize_label.configure(state="disabled")

        audio_warn = ""
        if is_audio_only and sel_ext in ("opus", "mp3"):
            A_PREF = {"opus": ["opus"], "mp4a": ["mp4a", "aac"]}
            wanted_ac = sel_ac if sel_ac and sel_ac != "-" else "opus"
            prefixes = A_PREF.get(wanted_ac, [wanted_ac])
            ac_available = any(
                any(f.get("acodec", "").startswith(p) for p in prefixes)
                for f in self._raw_formats
                if f.get("vcodec", "none") == "none"
            )
            if not ac_available:
                real_ac = ""
                for f in self._raw_formats:
                    ac = f.get("acodec", "none")
                    if ac != "none":
                        real_ac = self.AC_DISP.get(ac.split(".")[0], ac.split(".")[0].upper())
                        break
                ac_name = self.AC_DISP.get(wanted_ac, wanted_ac.upper())
                audio_warn = f"{ac_name} недоступен, вместо этого будет использован {real_ac}" if real_ac else f"{ac_name} недоступен"

        self.filesize_warn_label.configure(text=av1_warn or audio_warn)

    def _restore_download_btn(self):
        self.download_btn.configure(text="СКАЧАТЬ", fg_color="#ff0000", hover_color="#bf0000", command=self._run_download)

    def _cancel_download(self):
        if self._process:
            try:
                subprocess.run(["taskkill", "/F", "/IM", "yt-dlp.exe", "/T"], creationflags=subprocess.CREATE_NO_WINDOW)
                subprocess.run(["taskkill", "/F", "/IM", "ffmpeg.exe", "/T"], creationflags=subprocess.CREATE_NO_WINDOW)
            except Exception: pass
            self._process = None
        if self._timer_id:
            self.after_cancel(self._timer_id)
            self._timer_id = None
        self._restore_download_btn()
        self.progress_bar.configure(progress_color="#ffaa00")
        self.progress_label.configure(text="Загрузка отменена.")
        play_sound("error")

    def _open_folder(self):
        os.makedirs(self._dest_folder, exist_ok=True)
        os.startfile(self._dest_folder)

    def _run_download(self):
        url = self.url_entry.get().strip()
        if not url:
            self._show_error("Вставьте ссылку на видео или плейлист!")
            return

        self.download_btn.configure(text="ОТМЕНИТЬ", fg_color="#555555", hover_color="#666666", command=self._cancel_download)
        self.open_folder_btn.configure(state="disabled", fg_color="#333333", hover_color="#333333", text_color="#666666")

        browser_label = self.browser_combo.get()
        browser_key = {
            "Без авторизации": "none", "Chrome": "chrome", "Chromium": "chromium", "Opera": "opera",
            "Opera GX": "opera_gx", "Edge": "edge", "Firefox": "firefox", "Brave": "brave",
            "Vivaldi": "vivaldi", "Whale": "whale",
        }.get(browser_label, "none")

        cookie_opt = []
        if browser_key != "none":
            if browser_key == "opera_gx":
                opera_path = os.path.join(os.environ.get("APPDATA", ""), "Opera Software", "Opera GX Stable")
                cookie_opt = ["--cookies-from-browser", f"opera:{opera_path}"]
            else:
                cookie_opt = ["--cookies-from-browser", browser_key]

        sel_ext = self._format_map.get(self.format_combo.get(), "mp4")
        
        sel_vc_disp = self.vcodec_combo.get()
        sel_ac_disp = self.acodec_combo.get()
        sel_res = self.res_combo.get()
        sel_fps = self.fps_combo.get()

        sel_vc = self.VC_RAW.get(sel_vc_disp, sel_vc_disp.lower()) if sel_vc_disp != "-" else "-"
        sel_ac = self.AC_RAW.get(sel_ac_disp, sel_ac_disp.lower()) if sel_ac_disp != "-" else "-"

        is_audio = sel_ext in ("mp3", "m4a", "opus")

        fmt_opts = []
        if is_audio:
            A_PREF = {"opus": ["opus"], "mp4a": ["mp4a", "aac"]}
            wanted_ac = sel_ac if sel_ac and sel_ac != "-" else "opus"
            prefixes = A_PREF.get(wanted_ac, [wanted_ac])
            ac_available = any(
                any(f.get("acodec", "").startswith(p) for p in prefixes)
                for f in getattr(self, "_raw_formats", [])
                if f.get("vcodec", "none") == "none"
            )
            if not ac_available:
                fmt_opts = ["-f", "bestaudio/best", "-x", "--audio-format", sel_ext]
                real_ac = ""
                for f in getattr(self, "_raw_formats", []):
                    ac = f.get("acodec", "none")
                    if ac != "none":
                        real_ac = self.AC_DISP.get(ac.split(".")[0], ac.split(".")[0].upper())
                        break
                ac_name = self.AC_DISP.get(wanted_ac, wanted_ac.upper())
                warn = f"{ac_name} недоступен, вместо этого будет использован {real_ac}" if real_ac else f"{ac_name} недоступен"
            else:
                ac_cond = f"[acodec^={sel_ac}]" if sel_ac and sel_ac != "-" else ""
                fmt_opts = ["-f", f"bestaudio{ac_cond}/bestaudio", "-x", "--audio-format", sel_ext]
            if sel_ext == "mp3":
                fmt_opts += ["--audio-quality", "0"]
        else:
            v_cond = []
            if sel_res and sel_res != "-":
                m = re.search(r'x(\d+)', sel_res)
                if m:
                    v_cond.append(f"height<={m.group(1)}")
                else:
                    m = re.search(r'\d+', sel_res)
                    if m: v_cond.append(f"height<={m.group(0)}")

            if sel_fps and sel_fps != "-":
                v_cond.append(f"fps<={sel_fps}")

            a_cond = f"[acodec^={sel_ac}]" if sel_ac and sel_ac != "-" else ""

            av1_available = any(
                f.get("vcodec", "").startswith("av01")
                for f in getattr(self, "_raw_formats", [])
            )

            if sel_vc == "av01" and not av1_available:
                v_str = "[" + "][".join(v_cond) + "]" if v_cond else ""
                fmt_string = f"bestvideo{v_str}+bestaudio{a_cond}/bestvideo+bestaudio/best"
                fmt_opts = ["-f", fmt_string, "-S", "vcodec:av01"]
            else:
                if sel_vc and sel_vc != "-":
                    v_cond.append(f"vcodec^={sel_vc}")
                v_str = "[" + "][".join(v_cond) + "]" if v_cond else ""
                fmt_string = f"bestvideo{v_str}+bestaudio{a_cond}/bestvideo+bestaudio/best"
                fmt_opts = ["-f", fmt_string]

            if sel_ext and sel_ext != "-":
                fmt_opts += ["--merge-output-format", sel_ext]

        os.makedirs(self._dest_folder, exist_ok=True)
        dest  = os.path.join(self._dest_folder, "%(title)s.%(ext)s")
        ytdlp = resource_path("yt-dlp.exe")
        # Обрезка видео
        trim_opts = []
        trim_start = getattr(self, "_trim_start", None)
        trim_end   = getattr(self, "_trim_end", None)
        ts = getattr(self, "_trim_start_val", "") or ""
        te = getattr(self, "_trim_end_val",   "") or ""
        if ts or te:
            section = f"*{ts}-{te}" if ts and te else (f"*{ts}-inf" if ts else f"*0-{te}")
            trim_opts = ["--download-sections", section,
                         "--force-keyframes-at-cuts"]

        cmd   = [ytdlp, "--newline", "--no-colors"] + cookie_opt + fmt_opts + trim_opts + ["-o", dest, url]

        for f in (self._log_file, self._done_file):
            try: os.remove(f)
            except FileNotFoundError: pass

        self.progress_bar.set(0)
        self.progress_bar.configure(progress_color="#ffaa00")
        self.progress_label.configure(text="Подключение и анализ…", text_color="white")

        def run():
            with open(self._log_file, "w", encoding="utf-8") as log:
                proc = subprocess.Popen(cmd, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
                self._process = proc
                proc.wait()
            with open(self._done_file, "w") as f: f.write("DONE")

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
                        self.progress_bar.configure(progress_color="#ff0000")
                        error_found = True
                        break
                    elif "[download]" in line and "%" in line:
                        last_status = line
                        break
                    elif "[Merger]" in line or "[ExtractAudio]" in line:
                        last_status = "Финальная обработка файла…"
                        break
                if "%" in last_status and "Ошибка" not in last_status:
                    sel_ext = self._format_map.get(self.format_combo.get(), "mp4")
                    is_audio_fmt = sel_ext in ("mp3", "m4a", "opus")
                    if not is_audio_fmt:
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
                        unit_to_mb = {"KIB": 1/1024, "MIB": 1, "GIB": 1024, "KB": 1/1000, "MB": 1, "GB": 1000}
                        total_mb = total_val * unit_to_mb.get(total_unit, 1)
                        done_mb  = total_mb * pct
                        size_str = f"({done_mb:,.2f} МБ из {total_mb:,.2f} МБ)".replace(",", " ").replace(".", ",")
                        speed_m = re.search(r"at\s+([\d.]+)(\w+/s)", last_status)
                        if speed_m:
                            spd_val  = float(speed_m.group(1))
                            spd_unit = speed_m.group(2).upper()
                            conv = {"KIB/S": 1/1024, "MIB/S": 1, "GIB/S": 1024, "KB/S": 1/1000, "MB/S": 1, "GB/S": 1000}
                            spd_mb = spd_val * conv.get(spd_unit, 1)
                            spd_str = f"  {spd_mb:.1f} МБ/с"
                        else:
                            spd_str = ""
                        if not is_audio_fmt:
                            label = "Загрузка видеопотока" if self._downloading_video_stream else "Загрузка аудиопотока"
                        else:
                            label = "Загрузка"
                        self.progress_label.configure(text=f"{label}: {m.group(1)}% {size_str}{spd_str}")
                elif last_status:
                    self.progress_label.configure(text=last_status)
            except Exception: pass

        if os.path.exists(self._done_file):
            if self._timer_id:
                self.after_cancel(self._timer_id)
                self._timer_id = None
            self._restore_download_btn()
            if not error_found:
                self.progress_bar.configure(progress_color="#00bf00")
                self.progress_bar.set(1.0)
                self.progress_label.configure(text="Успешно завершено!")
                self.open_folder_btn.configure(state="normal", fg_color="#555555", hover_color="#666666", text_color="white")
                play_sound("success")
                _url = self.url_entry.get().strip()
                if _url:
                    save_history(_url)
                    self.url_entry.configure(values=load_history_combo())
                _preview = getattr(self, "_preview_data", None)
                _video_title = _preview.get("title", "") if _preview else ""
                if not _video_title:
                    try:
                        with open(self._log_file, "r", encoding="utf-8", errors="replace") as _lf:
                            for _line in _lf:
                                _m = re.search(r"\[download\] Destination: .+?[/\\](.+?)(\.\w+)?(\.\w+)?$", _line.rstrip())
                                if _m: _video_title = _m.group(1)
                    except Exception: pass
                show_toast("RedStream", f"{_video_title}\nСкачивание завершено!" if _video_title else "Скачивание завершено!")
            else:
                play_sound("error")
            for f in (self._log_file, self._done_file):
                try: os.remove(f)
                except Exception: pass
            return

        self._timer_id = self.after(500, self._check_progress)

    def _show_error(self, msg):
        play_sound("error")
        win = ctk.CTkToplevel(self)
        win.overrideredirect(True)
        win.configure(fg_color="#2a2a2a")
        win.resizable(False, False)
        win.grab_set()
        frame = ctk.CTkFrame(win, fg_color="#2a2a2a", corner_radius=0, border_width=1, border_color="#555555")
        frame.pack(padx=0, pady=0)
        ctk.CTkLabel(frame, text="⚠  Внимание!", text_color="#ff4444", font=("Segoe UI", 13, "bold")).pack(padx=24, pady=(16, 4))
        ctk.CTkLabel(frame, text=msg, text_color="white", font=("Segoe UI", 12), wraplength=260).pack(padx=24, pady=(0, 12))
        ctk.CTkButton(frame, text="ОК", fg_color="#ff0000", hover_color="#bf0000", corner_radius=8, width=100, command=win.destroy).pack(pady=(0, 16))
        def _center():
            win.update_idletasks()
            ww, wh = win.winfo_width(), win.winfo_height()
            x = self.winfo_x() + (self.winfo_width() - ww) // 2
            y = self.winfo_y() + (self.winfo_height() - wh) // 2
            win.geometry(f"+{x}+{y}")
            win.lift()
        win.after(10, _center)

if __name__ == "__main__":
    ctk.set_appearance_mode("dark")
    ctk.set_default_color_theme("dark-blue")
    app = RedStreamApp()
    app.mainloop()