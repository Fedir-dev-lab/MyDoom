# AGENTS.md — MyDoomEngine / «Марс: Тихий рой»

Прочитать перед изменениями. Канонический дизайн и контент определяет
[scenario.md](scenario.md). Он описывает целевую 12-уровневую игру про Алекса
Рейна, базу «Аргир», Мантисов и ИИ ЭЙДЖИ. Текущий C++ код — технический
прототип, поэтому не выдавать его временные карты, врагов или оружие за
готовый канон.

## Технологии и сборка

- C++20, CMake 3.20+, SDL3 на Linux.
- Рендеринг — software rasterizer в `Framebuffer`; SDL3 разрешён только для
  окна, ввода, загрузки одной streaming texture и её presentation.
- `SDL_BUNDLED=ON` по умолчанию загружает SDL3 `release-3.2.0` и линкует её
  статически. `SDL_BUNDLED=OFF` требует системную SDL3 3.2.0+.

```bash
cmake --preset debug
cmake --build --preset debug -j$(nproc)
./build/debug/MyDoomEngine
```

## Фактическая структура

- `src/game/Game.cpp` — SDL lifecycle, Input → Update → Render, UI-state,
  загрузка уровней и presentation.
- `src/renderer/` — framebuffer, PPM/procedural textures, lighting, стены,
  плоскости, небо и billboard-спрайты.
- `src/world/` — схема/загрузчик `.level`, runtime-состояние дверей и ключей,
  `Map` с collision и LoS; `DemoLevel` — fallback.
- `src/player/`, `src/weapons/`, `src/enemies/` — текущая временная игровая
  модель. Канонические оружие и Мантисы ещё не реализованы.
- `src/ui/` — HUD, меню, debug overlay и телефон-карта.
- `src/game/SaveSystem.*` — raw binary saves v5 с миграцией v1–v4.
- `src/game/GamePack.*` и `tools/mdpack.cpp` — упаковка `assets/` в
  `game.mdpk` и безопасная распаковка cache-пакета.
- `assets/levels/landing.level` — первая сценарная карта «Посадочная
  площадка» на procedural-заглушках; `demo.level` и `finale.level` остаются
  legacy-тестами. Остальные карты должны следовать уровням 2–12 из сценария.
- `assets/textures/wall.ppm` — единственный file-backed texture; остальное
  пока procedural. `assets/sounds`, `assets/music`, `assets/sprites` и
  `src/audio` пока не содержат реализации.
- `samples/` — независимые HTML/Three.js-эксперименты, не часть сборки игры.

## Правила изменения кода

- Держать X/Z как горизонтальную плоскость, Y как высоту; yaw 0 смотрит в
  `+Z`.
- Упаковывать цвета только `Framebuffer::MakeColor`; формат RGBA8888.
- Очищать framebuffer/depth один раз за кадр и сохранять порядок renderer-ов.
- При смене разрешения пересоздавать весь `RenderContext`.
- `Map` остаётся владельцем статической геометрии, collision и LoS; не
  дублировать тесты стен в Player/Enemy/Weapon.
- Любое несовместимое изменение `SaveData` требует новой версии, валидации и
  миграции. Save-файлы — пользовательские данные, не исходники.
- Новая система начинается с небольшого проверяемого инкремента. После правок
  собрать проект или выполнить эквивалентную проверку.

## Направление контента

При реализации ориентироваться на `scenario.md`: сначала завершать вертикаль
уровня 1 «Посадочная площадка», а не добавлять изолированные случайные
функции. Новые текстуры, сущности, звук, триггеры и формат уровней должны
служить базе «Аргир», улью и киберулью. План работ — в
[PROJECT_PLAN.md](PROJECT_PLAN.md), текущий handoff — в
[SESSION_HANDOFF.md](SESSION_HANDOFF.md).
