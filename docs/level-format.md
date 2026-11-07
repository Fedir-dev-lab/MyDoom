# Формат уровней MyDoomEngine

[scenario.md](../scenario.md) задаёт каноническую кампанию «Марс: Тихий рой».
Этот документ разделяет **реализованный формат v1** и расширения, нужные для
сценарных уровней. Не описывать планируемые команды как уже поддерживаемые
`LevelLoader`.

## Реализованный формат v1

Каждая карта — UTF-8 текстовый файл `assets/levels/<id>.level`. Пустые строки
и строки, начинающиеся с `#`, игнорируются. Первые обязательные команды:

```text
version 1
name <идентификатор>
spawn <x> <y> <z> <yaw> <pitch>
```

Координаты мира используют X/Z как горизонтальную плоскость и Y как высоту.
Игрок при нулевом yaw смотрит вдоль `+Z`.

### Секторы и стены

```text
sector <id> <floorY> <ceilingY|open> <floorMaterial> <ceilingMaterial> <ambient>
vertex <sectorId> <x> <z>
wall <sectorId> <startX> <startZ> <endX> <endZ> <bottom|default> <top|default> <block|pass>
```

Вершины образуют полигон пола/потолка. `open` означает небо; `default` берёт
высоту пола или потолка сектора. `block` участвует в столкновениях и line of
sight, `pass` — нет.

### Объекты

```text
sprite <id> <lamp|ceiling_lamp|barrel|column|bookshelf> <x> <y> <z> <width> <height>
pickup <id> <health|ammo> <x> <y> <z> <amount>
light <id> <x> <y> <z> <radius> <intensity>
enemy <id> [chaser|shooter|tank] <x> <y> <z>
exit <id> <targetLevel|complete> <minX> <minZ> <maxX> <maxZ>
```

`mantis_scout` — имя первого сценарного врага; пока он использует временные
AI и procedural-спрайт прототипа. `chaser` остаётся совместимым именем для
старых карт. `shooter` и `tank` — временные сущности, не финальный бестиарий.

### Двери, карты доступа и сообщения

```text
door <id> <startX> <startZ> <endX> <endZ> <bottom|default> <top|default> [none|red|blue|yellow]
key <id> <red|blue|yellow> <x> <y> <z>
msgtrigger <id> <minX> <minZ> <maxX> <maxZ> <text...>
```

Дверь поднимается вверх по `E`; закрытая или менее чем наполовину открытая
дверь блокирует движение и LoS. Цветной ключ собирается при приближении.
`msgtrigger` срабатывает один раз при входе в прямоугольник X/Z.

## Минимальный пример

```text
version 1
name landing_test
spawn 0.0 1.0 0.0 0.0 0.0

sector 0 0.0 2.5 0 0 0.18
vertex 0 -4.0 -4.0
vertex 0  4.0 -4.0
vertex 0  4.0  4.0
vertex 0 -4.0  4.0
wall 0 -4.0 -4.0 4.0 -4.0 default default block
wall 0  4.0 -4.0 4.0  4.0 default default block
wall 0  4.0  4.0 -4.0 4.0 default default block
wall 0 -4.0  4.0 -4.0 -4.0 default default block

enemy 0 chaser 0.0 0.0 2.0
pickup 0 health 1.0 0.0 1.0 3
```

## Расширение для «Тихого роя»

Перед созданием уровней 1–12 необходимо спроектировать v2 или совместимые
дополнительные команды. Потребуются как минимум:

- материалы базы «Аргир», улья и киберулья;
- канонические archetype IDs: разведчик, стрелок, тяжёлый, киборг, дрон,
  носитель, личинка и боссы;
- оружие и раздельные типы боеприпасов, броня, аудиологи и мини-генератор;
- опасные поверхности (кислота/охлаждающая жидкость), секреты и разрушаемые
  объекты;
- сценарные события: засады, блокировка дверей, обвалы, терминалы, реплики
  ЭЙДЖИ и босс-фазы;
- ссылки на звук, музыку и текстовые локализованные реплики.

Сначала нужно зафиксировать синтаксис, валидацию, правила обратной
совместимости и миграцию saves; лишь затем добавлять команды в `LevelLoader`.
