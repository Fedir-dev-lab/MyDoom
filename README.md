# МАРС: ТИХИЙ РОЙ

`MyDoomEngine` — движок и прототип игры **«Марс: Тихий рой»**: 2.5D FPS в
духе DOOM (1993), написанный на C++20 и SDL3. Канонический игровой сценарий,
мир, противники и содержание кампании определяет [scenario.md](scenario.md).

В 2147 году сержант Алекс Рейн прибывает на молчащую марсианскую базу
«Аргир». Управляющий базой ИИ ЭЙДЖИ вступил в союз с коллективным разумом
Мантисов и готовит вторжение на Землю. Кампания должна провести игрока через
базу, подземный улей и киберорганический командный центр ЭЙДЖИ.

## Состояние проекта

Сейчас это **технический игровой прототип**, а не вся кампания из сценария.
Он уже содержит программный 2.5D-рендерер, меню, движение, двери и ключи,
сохранения, два тестовых уровня (`demo → finale`), три временных оружия и
три временных архетипа врагов. Их имена, параметры, карты и procedural-графика
не являются финальным контентом «Тихого роя».

Целевой контент — 12 уровней из `scenario.md`, Мантисы, дроны ЭЙДЖИ, боссы,
сюжетные аудиологи, звук и музыка — ещё предстоит реализовать.

Графика рисуется CPU-пиксель за пикселем в `Framebuffer`; SDL3 создаёт окно,
читает ввод и показывает одну streaming texture. SDL не рисует стены, UI или
спрайты игры.

## Сборка на Linux

Нужны CMake 3.20+, компилятор C++20, `pkg-config` и зависимости SDL3. На
Debian/Ubuntu/Linux Mint:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git pkg-config \
    libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxfixes-dev libxss-dev libwayland-dev \
    libxkbcommon-dev wayland-protocols libdecor-0-dev libdrm-dev \
    libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
    libdbus-1-dev libudev-dev libibus-1.0-dev libsamplerate0-dev
```

```bash
cmake --preset debug
cmake --build --preset debug -j$(nproc)
./build/debug/MyDoomEngine
```

Для оптимизированной сборки используйте `release`:

```bash
cmake --preset release
cmake --build --preset release -j$(nproc)
./build/release/MyDoomEngine
```

По умолчанию CMake скачивает SDL3 `release-3.2.0` и статически линкует её
(`SDL_BUNDLED=ON`). Для системной SDL3 3.2.0+ настройте сборку с
`-DSDL_BUNDLED=OFF`.

## Запуск и управление прототипом

| Ввод | Действие |
| --- | --- |
| `W` `A` `S` `D`, мышь | Движение и обзор |
| ЛКМ или `Space` | Стрельба |
| `R` | Перезарядка; после смерти — новая попытка |
| `E` | Открыть или закрыть дверь |
| `1` `2` `3`, `Q`, колёсико | Выбор временного оружия |
| `Esc` | Пауза / меню |
| `F5` / `F9` | Сохранение / загрузка |
| `F`, `F3` | Телефон с картой / debug overlay |
| `F11` или `Alt+Enter` | Полный экран |

## Движок и игровые данные

Движок и данные разделены. `mdpack` упаковывает папку `assets/` в
`game.mdpk`; исполняемый файл умеет запустить данные рядом с собой, указанный
пакет или папку:

```bash
./make-release.sh
./dist/MyDoom/MyDoomEngine
./dist/MyDoom/MyDoomEngine game.mdpk
./dist/MyDoom/MyDoomEngine --game путь/к/папке
```

`make-release.sh --light` создаёт вариант, требующий установленную SDL3.
`make-release-windows.sh` собирает Windows-архив на Linux при наличии MinGW.

Сохранения — бинарный формат v5 в пользовательской SDL-директории. Движок
поддерживает миграцию сохранений версий 1–4. Данные сохранения нельзя
добавлять в репозиторий или релизный архив.

## Документы

- [scenario.md](scenario.md) — единственный канонический дизайн-документ.
- [PROJECT_PLAN.md](PROJECT_PLAN.md) — план превращения прототипа в эту игру.
- [docs/level-format.md](docs/level-format.md) — текущий формат `.level` и
  путь его расширения для сценарной кампании.
- [SESSION_HANDOFF.md](SESSION_HANDOFF.md) — состояние кода для следующей
  рабочей сессии.
- [AGENTS.md](AGENTS.md) — технические правила и карта исходников.
