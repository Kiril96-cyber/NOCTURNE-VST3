# Автоматическая установка NOCTURNE (Windows x64)

В комплект добавлен сценарий Inno Setup и фирменная иконка. После сборки он создаёт
`dist\NOCTURNE_Setup_x64.exe` с иконкой NOCTURNE. Установщик автоматически копирует
VST3 bundle в стандартную системную папку:

`C:\Program Files\Common Files\VST3\NOCTURNE.vst3`

## Что нужно установить один раз на компьютере разработчика
1. Visual Studio 2022 с workload **Desktop development with C++**.
2. CMake 3.22 или новее.
3. Inno Setup 6.

## Создание установщика
1. Распакуйте папку проекта.
2. Откройте **x64 Native Tools Command Prompt for VS 2022**.
3. Перейдите в папку `NOCTURNE_VST3`.
4. Запустите `build_installer_windows.bat`.
5. При успешной сборке установщик появится в `dist\NOCTURNE_Setup_x64.exe`.

Первый запуск CMake скачивает JUCE 8.0.6 из GitHub, поэтому требуется интернет.
Сам `.exe` создаётся на Windows с помощью Inno Setup; готового бинарника в исходном
архиве нет, поскольку в текущей среде отсутствуют Windows-компилятор и Inno Setup.

## Установка у пользователя
Дважды щёлкнуть `NOCTURNE_Setup_x64.exe` и подтвердить запрос Windows UAC.
Плагин устанавливается в системную папку VST3. Затем открыть FL Studio:
`Options > Manage plugins > Find installed plugins` и выполнить поиск VST3.

## Ограничение текущего прототипа
Это упаковка текущего исходного прототипа. Сам VST3 ещё необходимо успешно собрать
и проверить на Windows/FL Studio. Установщик не исправляет возможные ошибки компиляции
или особенности live MIDI routing.
