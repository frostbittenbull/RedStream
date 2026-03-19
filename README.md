# RedStream: Video Downloader
![Preview](Preview.png)
## 📥 Простой и удобный загрузчик видео с популярных сайтов
**RedStream** — десктопное приложение для Windows с минималистичным интерфейсом, позволяющее скачивать видео и аудио с YouTube, Instagram, TikTok и сотен других платформ. Построено на базе [yt-dlp](https://github.com/yt-dlp/yt-dlp), [ffmpeg](https://github.com/ffmpeg/ffmpeg), [CustomTkinter](https://github.com/TomSchimansky/CustomTkinter) и [Pillow](https://github.com/python-pillow/Pillow).

---

## ✨ Возможности:
- Поддержка YouTube, Instagram, TikTok и [многих других сайтов](https://github.com/yt-dlp/yt-dlp/blob/master/supportedsites.md)
- Скачивание видео в форматах **MKV** и **MP4**
- Скачивание аудио в форматах **OPUS**, **M4A**, **MP3**
- Динамический выбор разрешения, кодека и FPS на основе реальных форматов видео
- Выбор видеокодека: **AV1**, **VP9**, **H.264** и другие (зависит от платформы)
- Умный подбор AV1: если недоступен — автоматически выбирается лучшая альтернатива
- Выбор аудиокодека: **OPUS**, **AAC** и другие
- Предпросмотр видео с превью, названием и длительностью перед скачиванием
- История последних 5 скачанных ссылок
- Авторизация через браузер для видео с возрастными ограничениями
- Обновление `yt-dlp` и `ffmpeg` внутри приложения
- Прогрессбар с отображением процента, размера, типа потока и скорости загрузки
- Уведомление Windows по завершении скачивания
- Сворачивание в системный трей
- Кастомный заголовок окна и сплэш-экран при запуске

---

## ⚙️ Установка:
### Вариант 1 — готовый `.exe` (рекомендуется):
1. Скачать последний релиз из раздела [Releases](https://github.com/frostbittenbull/RedStream/releases)
2. Запустить установщик `RedStream_Setup_x64.exe`

### Вариант 2 — из исходников:
```bash
git clone https://github.com/frostbittenbull/RedStream.git
cd RedStream
curl -L https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe -o yt-dlp.exe
curl -L https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-win64-gpl.zip -o ffmpeg.zip
tar -xf ffmpeg.zip
for /d %i in (ffmpeg-*-git-*) do move /y "%i\bin\ffmpeg.exe" .
for /d %i in (ffmpeg-*-git-*) do rmdir /s /q "%i"
del ffmpeg.zip
pip install -r requirements.txt
python main.py
```

> **Зависимости:** `customtkinter`, `Pillow`, `winotify`, `pystray`
> **Требуется:** `yt-dlp.exe` и `ffmpeg.exe` в папке с приложением (или в `PATH`)

---

## 🖼️ Скриншот:
![Preview](Screenshot.png)

---

## 📁 Куда сохраняются файлы:
Все загруженные файлы сохраняются в папку `RedStream Downloader` внутри вашей папки «Загрузки». Папку можно изменить прямо в интерфейсе.

---

## 🔐 Авторизация через браузер:
Для скачивания видео с возрастными ограничениями выберите ваш браузер в выпадающем списке.

⚠️ **Перед скачиванием браузер должен быть закрыт.**

Если видео не имеет ограничений — выберите «Без авторизации».

---

## 🛠️ Стек технологий:
- Python 3.x
- [yt-dlp](https://github.com/yt-dlp/yt-dlp)
- [ffmpeg](https://github.com/ffmpeg/ffmpeg)
- [7zipExtra](https://github.com/ip7z/7zip)
- [upx](https://github.com/upx/upx)
- [CustomTkinter](https://github.com/TomSchimansky/CustomTkinter)
- [Pillow](https://github.com/python-pillow/Pillow)
- [winotify](https://github.com/versa-syahptr/winotify)
- [pystray](https://github.com/moses-palmer/pystray)
