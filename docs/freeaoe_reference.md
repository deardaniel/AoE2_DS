# FreeAoE Architecture Reference

Reference document for the freeaoe open-source AoE2 engine at `/home/daniel/freeaoe/`.

## Directory Structure
- `src/core/` — types (MapPos, ScreenPos), constants, logging
- `src/mechanics/` — game entities (Unit, Building, Map, UnitManager, GameState)
- `src/actions/` — unit action implementations (Move, Attack, Gather, Build, DropOff)
- `src/ai/` — AI scripting (AiPlayer, AiScript, AiRule with condition/action pairs)
- `src/render/` — SFML rendering (MapRenderer, UnitsRenderer, GraphicRender, Camera)
- `src/resource/` — asset loading (AssetManager, DRS/SLP files via genieutils)
- `src/ui/` — user interface
- `src/audio/` — audio playback
- `src/client/` + `src/server/` + `src/communication/` — multiplayer

## Entity Hierarchy
```
Entity (base: position, map, renderer)
├── StaticEntity
│   └── DecayingEntity (corpses, smoke)
├── Unit (mobile entities + buildings)
│   └── Building (production queue, garrison, rally point)
│       └── Farm (terrain cycling)
└── Missile (projectiles: trajectory, blast type/radius)
```

## Game Loop (Engine::start)
1. Poll SFML events
2. `GameState::update(time)` — tick all entities via UnitManager
3. Render map (MapRenderer) then entities (UnitsRenderer, Y-sorted)
4. UI updates

## Action System
- **UnitActionHandler** per unit: current action + queue (deque)
- Actions: Move, Build, Gather, DropOff, Attack, Fly, Garrison
- Each action returns: Updated / Completed / Failed / NotUpdated
- Tasks from DAT file define what actions a unit can perform

## Combat
- **ActionAttack**: handles melee + ranged
- **Missile**: projectiles with elevation, gravity, trajectory
  - Blast types: DamageTargetOnly, DamageNearby, DamageTrees, DamageResources
  - Damage uses genie::unit::AttackOrArmor from DAT
- Ranged units call `spawnMissiles()` when in range

## Pathfinding (ActionMove)
- A* with terrain multipliers (grass faster than sand)
- **Threaded**: background std::thread to avoid frame stalls
- Passability cached in bitset
- TerrainRestriction ID per unit type
- Heuristic weight: 10

## Economy
- **ResourceMap**: Food, Wood, Stone, Gold per player
- Gathering: ActionGather → walk to resource → work → ActionDropOff at nearest drop site
- Carry capacity from DAT unit data
- Trading: Market buy/sell with supply/demand price adjustment

## Technology System (Civilization)
- `creatableUnits(buildingId)` — what a building can train
- `researchAvailableAt(buildingId)` — available techs
- `applyTechEffect()` / `applyUnitAttributeModifier()` — modify unit stats
- Modifiable: HP, Attack, Armor, Speed, Range, CarryCapacity, TrainTime, etc.

## AI System
- **AiScript**: rule-based (parsed from .per files via bison/flex)
- **AiRule**: condition list + action list
- Conditions: ResourceCondition, PopulationCondition, TechResearched, etc.
- Actions: TrainUnit, BuildTech, SetStrategicNumber, BuyCommodity, etc.
- Strategic numbers control AI behavior parameters
- Escrow system: reserve resources for strategic goals

## Rendering
- SFML with isometric projection
- Tile: 96px horizontal, 48px vertical, 24px per elevation
- **GraphicRender** per entity: sprite, animation frames, player color overlay
- RenderTypes: Shadow, Base, BuildingAlpha, Outline, InTheShadows
- Y-sorted depth ordering for correct overlap

## Map
- Up to 255×255 tiles, each 48×48px
- Per-tile: terrain type, elevation, slope, blending
- Entity tracking per tile (`m_tileUnits`)
- Elevation interpolation for smooth height

## Constants
- TILE_SIZE = 48, TILE_SIZE_HORIZONTAL = 96, TILE_SIZE_VERTICAL = 48
- MAP_MAX_SIZE = 255

## Key Patterns
- shared_ptr/weak_ptr for entity lifecycle (avoids circular refs)
- Action queue pattern (current + pending actions per unit)
- Signal/slot events (unit died, building completed, etc.)
- Factory pattern (UnitManager creates entities)
- Command pattern (AiRule conditions/actions)
