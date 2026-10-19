# Mech1989: Recoded

**Version 0.6 Beta — the first beta ready for public testing.**

This project was written with the assistance of **OpenAI Codex**.

> **100% of the code has been written with the assistance of AI. If AI-generated code is unacceptable to you, please close this page.**

This statement describes the project's own code.

[English](#english) · [Русский](#russian) · [Releases](https://github.com/madcatse/Mech1989-Recoded/releases) · [Report a bug](https://github.com/madcatse/Mech1989-Recoded/issues)

<a id="english"></a>
## English

Mech1989: Recoded is an unofficial native Windows recreation of **MechWarrior (1989)**, originally developed by Dynamix and published by Activision. It uses the graphics, fonts, maps, models, text and music from your own copy of the DOS game. The goal is to preserve the original game's look and play while making it run consistently on modern Windows hardware.

**0.6 Beta is the first public testing version.** The campaign, contracts and 3D combat are implemented, including the story ending. Bugs may still occur; please keep backups of your saves and report problems through [GitHub Issues](https://github.com/madcatse/Mech1989-Recoded/issues).

![The original blue Warhammer title screen rendered by Mech1989: Recoded](docs/images/title.png)

### What is included

- Mercenary campaign, interstellar travel, bars, NewsNet and the story ending.
- Contracts, negotiation, mission objectives, combat results and payments.
- Lance management, pilot recruitment and salaries, buying and selling BattleMechs, repairs and ammunition.
- 3D cockpit combat, weapons, targeting, heat, jump jets, lance orders, tactical and external views.
- Walkable ridges with elevation changes and smooth camera pitch.
- Campaign Save/Restore, with companion `.mwstate` files for Recoded's additional state.
- Optional mouse-look with a fixed crosshair, smooth torso control and one normal fire request per left click.
- Windowed and fullscreen display, crisp DOS-style scaling, resolution choices from 1280×800 to 4K, and saved settings.
- Emulated PC Speaker effects and MT-32 music through the bundled Munt library.

![Cockpit, instruments and 3D terrain rendered by Mech1989: Recoded](docs/images/cockpit.png)

### Quick start

1. Download the **complete Windows x64 ZIP** from [Releases](https://github.com/madcatse/Mech1989-Recoded/releases) and extract it to a writable folder. Do not run it inside the archive.
2. Copy **all files from your installed English DOS edition of MechWarrior (1989)** into the `Original` subfolder. Use your own legally obtained copy. Keep `MW_MAIN.EXE`: Recoded reads game data from it. Installation media must be installed first, for example using DOSBox.
3. For music, put your compatible `MT32_CONTROL.ROM` and `MT32_PCM.ROM` in `mt32 roms`, beside the new EXE. Without ROMs, you can still play with PC Speaker effects.
4. Start **`Mech1989.exe`**. Press a key or click to leave the welcome screen.

```text
Mech1989-Recoded/
├── Mech1989.exe
├── mt32emu-2.dll
├── msvcp140.dll
├── vcruntime140.dll
├── vcruntime140_1.dll
├── README_EN.txt
├── README_RU.txt
├── Original/                 ← add all installed DOS game files
│   ├── MW_MAIN.EXE
│   └── ...
├── mt32 roms/                ← add your ROMs for music
│   ├── MT32_CONTROL.ROM
│   └── MT32_PCM.ROM
├── licenses/
└── sources/                 ← Munt source and build instructions
```

Munt and the required Microsoft C++ runtime DLLs are included. No separate DOSBox, Munt or Visual C++ installation is needed to run the package on supported Windows 10/11 x64. Original game files and Roland ROMs are **not** included. Keep the DLLs next to `Mech1989.exe`.

Updating an earlier build? Start `Mech1989.exe`; the old `mw_main_recomp.exe` name is retired. Existing `mw1989.ini` settings and paired `.GAM` / `.GAM.mwstate` saves keep their filenames.

### Playing

Take contracts, recruit pilots, equip your lance, travel between planets and follow the story through bars and NewsNet. After each mission, review the result, repair your mechs, replenish ammunition and save. Campaign actions cost money and time; the contract objective matters as much as defeating enemies.

| Action | Default controls |
| --- | --- |
| Turn the mech / change throttle | Left–Right / Up–Down arrows |
| Fire / linked salvo | Space / Tab |
| Select a weapon / scan next target | 1–9, 0 / Enter |
| Center torso / toggle automatic weapon selection | A / W |
| Lance orders / mech status | C / G |
| Tactical map / external view | F2 / F3 |
| Fullscreen / window | Alt+Enter |
| Cheat console | Backtick / tilde (\` / ~), English keyboard layout |

Enable **MOUSELOOK: YES** in the system menu, or enter `mouselook` in the console. Move the mouse to aim without vertical inversion. Left click makes one normal fire request; hold right click for linked salvos. Movement still uses the arrow keys. Enter `mouselook` again to return to classic controls.

The system menu also contains **RESOLUTION**, **FULLSCREEN**, **SAVE GAME** and **RESTORE GAME**. Save/Restore stores campaign progress, not a mid-combat snapshot. Keep `.GAM` and `.GAM.mwstate` together when moving saves.

See [README_EN.txt](README_EN.txt) for the full controls, campaign basics, all cheats, music setup and troubleshooting.

### Requirements and beta scope

- Windows 10 (1703 or newer) or Windows 11, **x64**.
- Working OpenGL/WGL graphics driver, keyboard and mouse, and a display of at least 1280×800.
- A writable installation folder; allow about 100 MB for the game, data, saves and log.
- A normal Windows audio output device for sound; your MT-32 ROMs for music.

The tested data set is the English DOS/EGA edition. Linux, macOS, SteamOS/Proton, ARM64 and gamepads have not been validated. Supporting a Steam Deck-sized screen does not mean Steam Deck or SteamOS compatibility has been tested. Detailed requirements are in the player guide.

### Report bugs

Please report bugs in **[GitHub Issues](https://github.com/madcatse/Mech1989-Recoded/issues)** for [madcatse/Mech1989-Recoded](https://github.com/madcatse/Mech1989-Recoded). Check existing issues first. Include:

- **Version 0.6 Beta**, your Windows version and relevant display/mouse-look settings.
- Steps to reproduce, what you expected and what happened.
- `mwlog.txt` from beside the EXE, copied immediately after the problem: it is recreated on each launch.
- A screenshot if useful; the matching `.GAM` and `.GAM.mwstate` files if the problem depends on a save.

Do not attach original game files or Roland ROMs to an issue.

### Credits and rights

This is an **unofficial fan project made just for fun**, with no claim to MechWarrior, BattleTech or any other trademarks. All trademarks and original game assets belong to their respective owners. The project is not affiliated with or endorsed by those owners. The screenshots show the recreation rendering original game artwork supplied separately by the player.

The repository's source-code license is in [LICENSE](https://github.com/madcatse/Mech1989-Recoded/blob/main/LICENSE). Third-party components retain their own licenses: **Munt 2.7.2** is supplied under LGPL 2.1 or later, with copyright notices, full corresponding source and replacement/build instructions in the release. The Microsoft C++ runtime license is also included. These licenses do not grant rights to original game data or Roland ROMs.

---

<a id="russian"></a>
## Русский

**Версия 0.6 Бета — первая бета-версия, готовая к общественному тестированию.**

Проект написан с помощью **OpenAI Codex**.

> **100% кода написано при помощи AI. Если AI-генерируемый код для вас неприемлем, пожалуйста, закройте эту страницу.**

Указание относится к собственному коду проекта.

Mech1989: Recoded — неофициальное воссоздание **MechWarrior (1989)**, разработанной Dynamix и изданной Activision, для современной Windows. Игра использует графику, шрифты, карты, модели, тексты и музыку из вашей копии DOS-оригинала. Цель — сохранить его внешний вид и игровой процесс, обеспечив стабильную работу на современном оборудовании.

**0.6 Бета — первая версия для общественного тестирования.** Кампания, контракты и 3D-бои реализованы, включая сюжетный финал. Ошибки ещё возможны: сохраняйте резервные копии сохранений и сообщайте о проблемах через [GitHub Issues](https://github.com/madcatse/Mech1989-Recoded/issues).

### Что уже работает

- Кампания наёмника, межзвёздные перелёты, бары, NewsNet и сюжетный финал.
- Контракты, переговоры, задачи миссий, результаты боёв и выплаты.
- Управление отрядом, найм пилотов и зарплаты, покупка и продажа мехов, ремонт и боеприпасы.
- 3D-бои из кабины, оружие, прицеливание, нагрев, прыжковые двигатели, приказы отряду, тактический и внешний виды.
- Проходимые гряды с подъёмом и спуском меха и плавным наклоном камеры.
- Сохранение и восстановление кампании; файлы `.mwstate` хранят дополнительное состояние Recoded.
- Необязательный mouselook: неподвижный прицел, плавное управление торсом и один обычный запрос стрельбы на левый щелчок.
- Оконный и полноэкранный режимы, чёткое масштабирование с учётом DOS-пропорций, разрешения от 1280×800 до 4K и сохранение настроек.
- Эмуляция PC Speaker и музыка MT-32 через включённую в комплект библиотеку Munt.

На двух изображениях выше — заставка и кабина с 3D-ландшафтом, отрисованные Mech1989: Recoded.

### Быстрый запуск

1. Скачайте **полный ZIP для Windows x64** из [Releases](https://github.com/madcatse/Mech1989-Recoded/releases) и распакуйте в папку с правом записи. Не запускайте игру внутри архива.
2. В подпапку `Original` скопируйте **все файлы установленной английской DOS-версии MechWarrior (1989)** из вашей законно полученной копии. Не удаляйте `MW_MAIN.EXE`: Recoded читает из него данные. Если у вас установочные носители, сначала установите DOS-игру, например в DOSBox.
3. Для музыки положите совместимые `MT32_CONTROL.ROM` и `MT32_PCM.ROM` в папку `mt32 roms` рядом с новым EXE. Без ROM можно играть со звуками PC Speaker, без музыки.
4. Запустите **`Mech1989.exe`**. На приветствии нажмите клавишу или щёлкните мышью.

Структура папок показана в английском разделе выше. Munt и необходимые DLL среды Microsoft C++ уже включены: отдельно устанавливать DOSBox, Munt или Visual C++ на поддерживаемой Windows 10/11 x64 не требуется. Файлы оригинальной игры и Roland ROM **не включены**. DLL должны оставаться рядом с `Mech1989.exe`.

При обновлении прежней сборки запускайте `Mech1989.exe`: старое имя `mw_main_recomp.exe` больше не используется. Файл настроек `mw1989.ini` и пары сохранений `.GAM` / `.GAM.mwstate` сохраняют свои имена.

### Как играть

Берите контракты, нанимайте пилотов, оснащайте отряд, путешествуйте между планетами и ищите сюжетные сведения в барах и NewsNet. После миссии изучите результат, отремонтируйте мехов, пополните боеприпасы и сохранитесь. Действия в кампании расходуют время и деньги; выполнение задачи контракта важно не меньше уничтожения противников.

| Действие | Управление по умолчанию |
| --- | --- |
| Поворот меха / изменение скорости | Стрелки влево–вправо / вверх–вниз |
| Стрельба / связанный залп | Space / Tab |
| Выбор оружия / следующая сканируемая цель | 1–9, 0 / Enter |
| Центрировать торс / переключить автовыбор оружия | A / W |
| Приказы отряду / состояние меха | C / G |
| Тактический / внешний вид | F2 / F3 |
| Полный экран / окно | Alt+Enter |
| Чит-консоль | Клавиша \` / ~, английская раскладка |

Включите **MOUSELOOK: YES** в системном меню или введите `mouselook` в консоли. Движение мыши управляет прицеливанием без вертикальной инверсии. Левый щелчок даёт один обычный запрос стрельбы, удержание правой кнопки — связанные залпы. Для движения остаются стрелки. Повторный ввод `mouselook` возвращает классическое управление.

В системном меню также находятся **RESOLUTION**, **FULLSCREEN**, **SAVE GAME** и **RESTORE GAME**. Сохраняется состояние кампании, а не снимок боя. При переносе сохранений держите `.GAM` и `.GAM.mwstate` вместе.

Полные клавиши, основы кампании, все читы, настройка музыки и решение проблем описаны в [README_RU.txt](README_RU.txt).

### Требования и границы бета-тестирования

- Windows 10 (1703 или новее) либо Windows 11, **x64**.
- Рабочий видеодрайвер OpenGL/WGL, клавиатура, мышь и экран от 1280×800.
- Папка с правом записи; около 100 МБ для игры, ресурсов, сохранений и журнала.
- Обычное звуковое устройство Windows; для музыки — ваши ROM от MT-32.

Проверена английская DOS/EGA-версия ресурсов. Linux, macOS, SteamOS/Proton, ARM64 и геймпады не проверены. Поддержка разрешения экрана Steam Deck не означает проверку самой консоли или SteamOS. Подробные требования приведены в инструкции к игре.

### Сообщения об ошибках

О найденных багах пишите в **[GitHub Issues](https://github.com/madcatse/Mech1989-Recoded/issues)** проекта [madcatse/Mech1989-Recoded](https://github.com/madcatse/Mech1989-Recoded). Сначала проверьте, нет ли уже такой ошибки. Приложите:

- **Версию 0.6 Бета**, версию Windows и относящиеся к проблеме настройки экрана/mouselook.
- Шаги повторения, ожидаемый и фактический результат.
- `mwlog.txt` рядом с EXE, скопированный сразу после ошибки: при следующем запуске журнал создаётся заново.
- При необходимости — снимок экрана и соответствующую пару `.GAM` / `.GAM.mwstate`.

Не прикладывайте к сообщениям оригинальные файлы игры и Roland ROM.

### Авторы и права

Это **неофициальный фанатский проект, созданный ради удовольствия**. Мы не претендуем на MechWarrior, BattleTech и другие торговые марки. Все марки и оригинальные игровые материалы принадлежат соответствующим владельцам. Проект не связан с ними и не является одобренным ими продуктом. На изображениях показано воссоздание игры, использующее оригинальную графику, которую игрок добавляет отдельно.

Лицензия исходного кода репозитория находится в [LICENSE](https://github.com/madcatse/Mech1989-Recoded/blob/main/LICENSE). Сторонние компоненты сохраняют собственные лицензии: **Munt 2.7.2** включён по LGPL 2.1 или новее, вместе со сведениями о правах, полными соответствующими исходниками и инструкцией по сборке/замене. Лицензия среды Microsoft C++ также включена в релиз. Эти лицензии не предоставляют прав на оригинальные игровые данные и Roland ROM.
