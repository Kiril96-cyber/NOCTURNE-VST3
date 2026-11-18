# Сборка настоящего NOCTURNE_Setup_x64.exe через Windows CI

В проект добавлен GitHub Actions workflow: `.github/workflows/build-windows-installer.yml`.
Он запускает сборку на Windows Server 2022, собирает VST3, создаёт Inno Setup installer
и прикрепляет `NOCTURNE_Setup_x64.exe` как артефакт запуска.

## Что нужно сделать
1. Создать репозиторий на GitHub (можно Private).
2. Загрузить в него всё содержимое папки проекта NOCTURNE_VST3, включая скрытую папку `.github`.
3. Открыть вкладку **Actions** и разрешить GitHub Actions, если GitHub спросит.
4. Запустить workflow **Build NOCTURNE Windows VST3 installer** кнопкой **Run workflow**.
   Также он автоматически запускается при push в ветку `main`.
5. Дождаться завершения. Внизу страницы успешного запуска в разделе **Artifacts**
   появится `NOCTURNE-Setup-Windows-x64`. Скачать ZIP артефакта и извлечь из него
   `NOCTURNE_Setup_x64.exe`.

Сборка требует интернет для загрузки JUCE из GitHub. Если компилятор обнаружит ошибки
в исходном прототипе, workflow завершится ошибкой и покажет лог; в таком случае нужно
исправить ошибки и повторить запуск. Этот процесс создаёт установщик, но не заменяет
тестирование плагина в реальной FL Studio.
