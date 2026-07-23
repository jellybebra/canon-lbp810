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

Чтобы собрать такой же переносимый ZIP из исходников:

```powershell
.\scripts\Build-Release.ps1
```

Готовый архив появляется в `dist`.

## Установленное состояние

- очередь: `Canon LBP-810`;
- служба: `CanonLBP810Bridge`, автоматический запуск;
- драйвер очереди: `Microsoft PS Class Driver`;
- локальный порт: `CanonLBP810_CAPT`, `127.0.0.1:9100`;
- журнал: `C:\ProgramData\Canon LBP-810 Bridge\spool\bridge.log`.

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
