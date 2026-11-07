# Handoff: «Марс: Тихий рой»

## Канон і пріоритет

[scenario.md](scenario.md) — єдине джерело істини. Мета — 12 рівнів про
сержанта Алекса Рейна, базу «Аргір», Мантисів та ІА ЕДЖІ.

## Що реально працює

- C++20, SDL3, CPU software renderer через `Framebuffer`.
- Оптимізований рендерер: TerrainRenderer (адаптивний крок, LOD 0/1/2, early-out),
  PlaneRenderer (scanline span table замість per-pixel polygon test),
  SkyRenderer (dot-product spoke approximation замість atan2).
- Формат рівнів **v2**: `audiolog`, `armor`, `hazard` повністю реалізовано
  у LevelLoader, Map і підключено у game loop (Game.cpp).
- `SaveSystem` **v6**: `playerArmor`, `audioCollectedMask`, `armorCollectedMask`;
  міграція v1–v5 збережена.
- Гравець має броню: `AddArmor()`, `Armor()`, абсорбція 1/3 урону.
- Бестіарій за сценарієм: MantisScout HP=40 dmg=10, Shooter HP=60 dmg=15,
  Tank HP=180 dmg=30. Гравець HP=100.
- Зброя за сценарієм: **Стерх** (точний пістолет 9мм, dmg=8, mag=12),
  **Каратель** (помповик, 7 pellets×4 dmg, range=14),
  **Рейнджер** (штурмова гвинтівка, dmg=6, auto, mag=25).
- Рівень 1 «Посадочна площадка» (`landing.level`): 6 msgtrigger, перший
  audiolog ЕДЖІ, exit → `level2`.
- Рівень 2 «Житловий модуль» (`level2.level`): 3 кімнати (коридор,
  їдальня, каюти), 2 аудіологи (доктор Корвін + нашивка Денні), броня,
  2 mantis_scout, exit → `demo` (placeholder поки рівня 3 немає).

## Чого немає

- Рівні 3–12 (Реакторний блок, Лабораторія, Тунелі, Шахта, Зал Рою,
  Гніздо Матки, Командний центр, Склад, Космодром, Ядро ЕДЖІ).
- Фінальна піксель-арт графіка: текстури бази/вулика, спрайти Мантисів.
- Аудіо (`src/audio`, `assets/sounds`, `assets/music` — порожні).
- Броня не відображається в HUD (тільки здоров'я).
- Аудіологи показуються в HUD-банері, але Phone-карта їх не відображає.
- Боси, секрети, небезпечні поверхні на реальних рівнях.

## Наступний інкремент

Найлогічніший наступний крок — **HUD броня** (показати поточну броню поруч
зі здоров'ям) + **Phone аудіологи** (список зібраних на Phone-екрані).
Або перейти одразу до **Рівня 3 «Реакторний блок»** — перший закритий
interior-рівень бази «Аргір».

## Не зламати

- SDL тільки для вікна, вводу і показу framebuffer texture.
- `Map` — єдиний власник геометрії, collision і LoS.
- Будь-яка зміна `SaveData` → нова версія + міграція (зараз v6).
- `DemoLevel.cpp`: при розширенні `LevelData` явно ініціалізувати нові поля.
- `game.mdpk` збирається з `assets/`; save-файли не в репо.

## Перевірка

```bash
cmake --build --preset debug -j$(nproc)
./build/debug/MyDoomEngine
```

Остання перевірка: debug build 100% чистий (0 errors, 0 warnings).
