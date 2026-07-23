# Canon LBP-810 в Windows 11 x64

[Скачать готовый пакет для Windows 11 x64](https://github.com/jellybebra/canon-lbp810/releases/latest)

Это независимый 64-битный мост печати для Canon LBP-810. Оригинальный
драйвер из `LBP-810_R110_V110_Win_x32_EN_7.exe` остаётся нетронутым, но сама
Windows 11 его загрузить не может: все его исполняемые модули и секция INF
предназначены только для x86.

Подробная история разработки и объяснение архитектуры:
[WRITEUP.md](WRITEUP.md).

## Как это работает

Обычная очередь Windows использует встроенный подписанный
`Microsoft PS Class Driver`. Она передаёт PostScript на локальный RAW-порт
`127.0.0.1:9100`. Служба `CanonLBP810Bridge` преобразует страницу Ghostscript-ом
в монохромный растр 600 dpi, сжимает его в SCoA/CAPT v1 и отправляет через
штатный системный `usbprint.sys`.

Виртуальная машина, режим совместимости и замена USB-драйвера не нужны.

## Пакет для другого компьютера

Скачайте ZIP со страницы
[Releases](https://github.com/jellybebra/canon-lbp810/releases), полностью
распакуйте его, подключите LBP-810 по USB и запустите `Install.cmd`.
Этот вариант устанавливает локальный мост на тот компьютер и не использует
другой ПК как принт-сервер.

### Сборка ZIP из исходников

Сборка выполняется на Windows 11 x64. Понадобятся:

- PowerShell 5.1 или новее;
- [CMake](https://cmake.org/download/) 3.21 или новее, добавленный в `PATH`;
- [MSYS2](https://www.msys2.org/docs/installer/) x64 с окружением UCRT64;
- Git, если репозиторий клонируется командой `git clone`;
- около 2 ГБ свободного места для MSYS2, зависимостей и каталога сборки.

Visual Studio и оригинальный 32-битный драйвер Canon для сборки не нужны.

Установите MSYS2 в стандартный каталог `C:\msys64`. Затем откройте терминал
**MSYS2 UCRT64**, полностью обновите систему и установите зависимости:

```bash
pacman -Suy
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-binutils \
  mingw-w64-ucrt-x86_64-ghostscript
```

Если во время `pacman -Suy` терминал попросит закрыть все процессы MSYS2,
откройте **MSYS2 UCRT64** снова, повторите `pacman -Suy`, а затем установите
пакеты.

После этого в обычном PowerShell:

```powershell
git clone https://github.com/jellybebra/canon-lbp810.git
Set-Location .\canon-lbp810

$msys2Root = 'C:\msys64'
$env:PATH = "$msys2Root\ucrt64\bin;$env:PATH"

cmake -S . -B build-native -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_CXX_COMPILER=$msys2Root\ucrt64\bin\g++.exe" `
  "-DCMAKE_MAKE_PROGRAM=$msys2Root\ucrt64\bin\ninja.exe"

cmake --build build-native
.\scripts\Build-Release.ps1 -Msys2Root $msys2Root
```

`Build-Release.ps1` находит версию установленного Ghostscript, собирает
необходимые DLL и resource-файлы, добавляет установочные сценарии и лицензии,
а затем создаёт переносимый архив:

```text
dist\Canon-LBP810-Windows11-x64-1.0.0.zip
```

Параметры `-BuildDir` и `-Version` позволяют указать другой каталог CMake и
номер собираемого пакета.

## Установленное состояние

- очередь: `Canon LBP-810`;
- служба: `CanonLBP810Bridge`, автоматический запуск;
- драйвер очереди: `Microsoft PS Class Driver`;
- локальный порт: `CanonLBP810_CAPT`, `127.0.0.1:9100`;
- журнал: `C:\ProgramData\Canon LBP-810 Bridge\spool\bridge.log`.

## Общий доступ в локальной сети

Чтобы другие компьютеры могли печатать через ПК, к которому LBP-810 подключён
по USB, включите стандартный Windows Printer Sharing:

1. Откройте **Settings → Bluetooth & devices → Printers & scanners**.
2. Выберите **Canon LBP-810**.
3. Нажмите **Printer properties** — не **Printing preferences**.
4. Откройте вкладку **Sharing**.
5. Включите **Share this printer**.
6. В поле **Share name** укажите `CanonLBP810`.
7. Нажмите **Apply → OK**.

На другом компьютере нажмите `Win + R`, откройте
`\\ИМЯ-КОМПЬЮТЕРА\CanonLBP810` и дважды нажмите на принтер. То же подключение
можно выполнить из PowerShell:

```powershell
Add-Printer -ConnectionName '\\ИМЯ-КОМПЬЮТЕРА\CanonLBP810'
```

На компьютере с принтером профиль сети должен быть **Private**, а в
**Settings → Network & internet → Advanced network settings → Advanced sharing
settings → Private networks** должны быть включены **Network discovery** и
**File and printer sharing**.

### Учётные данные для общего принтера

При первом подключении Windows может показать окно **Enter network
credentials**. В нём нужно указать учётную запись компьютера, к которому
принтер подключён по USB.

Windows по умолчанию запрещает сетевой вход для локальной учётной записи с
пустым паролем. В таком случае безопаснее всего создать отдельного пользователя
только для печати:

1. На компьютере с принтером откройте
   **Settings → Accounts → Other users**.
2. Нажмите **Add account**.
3. Выберите **I don't have this person's sign-in information**.
4. Выберите **Add a user without a Microsoft account**.
5. Создайте пользователя `PrinterUser` и задайте ему пароль.

На клиентском компьютере в окне подключения введите:

```text
Username: ИМЯ-КОМПЬЮТЕРА\PrinterUser
Password: заданный пароль
```

Включите **Remember my credentials**, чтобы не вводить пароль повторно. Если
вход на компьютере с принтером выполнен через Microsoft Account, вместо этого
можно использовать `MicrosoftAccount\email` и пароль Microsoft Account.
Windows Hello PIN для сетевого подключения не подходит.

Отключать парольную защиту общего доступа и разрешать гостевой SMB-вход не
рекомендуется: это делает компьютер и принтер доступнее для посторонних
устройств в сети.

В этом режиме компьютер с USB-принтером становится принт-сервером и должен
оставаться включённым во время печати.

## Тест и обслуживание

Тестовую страницу Windows можно отправить из PowerShell:

```powershell
.\scripts\Print-TestPage.ps1
```

Проверка USB и CAPT-контроллера:

```powershell
.\build-native\bin\lbp810-bridge.exe --probe
.\build-native\bin\lbp810-bridge.exe --capt-status
.\build-native\bin\lbp810-bridge.exe --clear-error
```

Повторная установка или обновление (PowerShell от администратора):

```powershell
.\scripts\Install-Lbp810.ps1
```

Удаление созданной очереди, порта и службы:

```powershell
.\scripts\Uninstall-Lbp810.ps1
```

## Проверенный результат

После извлечения замятой бумаги и перезапуска принтера стандартная тестовая
страница Windows была успешно напечатана через установленную службу. Цепочка
проверена целиком: Windows Spooler → PostScript → Ghostscript → SCoA/CAPT v1
→ `usbprint.sys` → Canon LBP-810.

Протокольная часть основана на
[captppd](https://github.com/darkvision77/captppd) и
[libcapt](https://github.com/darkvision77/libcapt), которые заявляют поддержку
LBP-810, CAPT v1, SCoA и 600 dpi.

## Лицензия

Проект публикуется под
[GNU Affero General Public License v3 или новее](LICENSE). Сторонние компоненты
сохраняют собственные лицензии; их тексты и уведомления входят в каждый
бинарный релиз.

Canon и LASER SHOT — товарные знаки Canon Inc. Это независимый проект, не
связанный с Canon Inc. и не одобренный ею.
