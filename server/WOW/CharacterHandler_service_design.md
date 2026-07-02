# CharacterHandler -> Service/Coroutine Design

## Topology

```text
SessionServiceContext
  -> WowSessionAgent
  -> call(CharacterServiceContext)
       -> co_await Db/Login/Redis as needed
       -> call(MapInstanceServiceContext)
            -> bind player to map shard / instance mailbox
            -> call(CombatServiceContext) for actor bootstrap
  -> resume session coroutine
  -> send CHAR_ENTER / snapshots to client

steady tick
  -> MapInstanceServiceContext (movement/visibility/instance state)
  -> CombatServiceContext (cooldowns, attack resolution, death/release)
```

## TrinityCore function split

| TrinityCore function | Concern | In your architecture |
|---|---|---|
| `HandleCharEnum` | character roster query result assembly | `CharacterServiceContext::CharacterEnumRequest -> CharacterEnumResult` |
| `HandleCharEnumOpcode` | client packet entry for roster | `WowSessionAgent` builds `WowRuntimeOp::CharacterEnumRequest`, `SessionServiceContext` `co_await call(CharacterServiceContext)` |
| `HandleCharUndeleteEnumOpcode` | undelete roster listing | later `CharacterServiceContext` sub-flow, likely `CharacterUndeleteService` or extra op in CharacterService |
| `HandleCharCreateOpcode` | create character validation + persistence | `CharacterServiceContext::CharacterCreateRequest`, optionally `co_await Db/Redis` |
| `HandleCharDeleteOpcode` | delete character | `CharacterServiceContext::CharacterDeleteRequest` |
| `HandlePlayerLoginOpcode` | start login pipeline | `WowSessionAgent` sends `CharacterLoginBegin`; `SessionServiceContext` suspends on reply |
| `HandleContinuePlayerLogin` | continue after async DB/map work | session coroutine resume point after `co_await call(CharacterServiceContext)` |
| `HandleLoadScreenOpcode` | client transition ack | session-side state only; may trigger `MapSnapshotRequest` |
| `HandlePlayerLogin(LoginQueryHolder const&)` | finalize loaded player into world | split across `CharacterServiceContext` + `MapInstanceServiceContext` + `CombatServiceContext` |
| `SendFeatureSystemStatus` | capability snapshot | session/UI response helper, not world authority |
| `HandleSetFactionAtWar` | reputation bit mutation | future `CharacterServiceContext` or `PlayerProfileService` |
| `HandleSetFactionNotAtWar` | reputation bit mutation | same as above |
| `HandleTutorialFlag` | account/character progress flag | `CharacterServiceContext` + persistence |
| `HandleSetWatchedFactionOpcode` | watched faction | `CharacterServiceContext` |
| `HandleSetFactionInactiveOpcode` | faction visibility state | `CharacterServiceContext` |
| `HandleCheckCharacterNameAvailability` | name validation | `CharacterServiceContext` |
| `HandleCharRenameOpcode` | rename request | `CharacterServiceContext` |
| `HandleCharRenameCallBack` | async DB callback resume | `CharacterServiceContext` coroutine resume after `co_await Db` |
| `HandleSetPlayerDeclinedNames` | localized names | `CharacterServiceContext` |
| `HandleAlterAppearance` | barber shop mutation | `CharacterServiceContext`, maybe debit currency via inventory/profile component |
| `HandleCharCustomizeOpcode` | customization request | `CharacterServiceContext` |
| `HandleCharCustomizeCallback` | async customization finish | `CharacterServiceContext` coroutine resume |
| `HandleEquipmentSetSave` | save equipment preset | `CharacterServiceContext` or inventory/profile subservice |
| `HandleDeleteEquipmentSet` | delete preset | same |
| `HandleUseEquipmentSet` | apply preset to equipped items | `CharacterServiceContext` issuing inventory mutations |
| `HandleCharRaceOrFactionChangeOpcode` | paid race/faction change | `CharacterServiceContext` |
| `HandleCharRaceOrFactionChangeCallback` | async DB result | `CharacterServiceContext` coroutine resume |
| `HandleRandomizeCharNameOpcode` | generated name | `CharacterServiceContext` utility op |
| `HandleReorderCharacters` | roster ordering | `CharacterServiceContext` |
| `HandleOpeningCinematic` | intro state | session/profile state, not map authority |
| `HandleGetUndeleteCooldownStatus` | undelete cooldown query | `CharacterServiceContext` |
| `HandleUndeleteCooldownStatusCallback` | async callback | `CharacterServiceContext` coroutine resume |
| `HandleCharUndeleteOpcode` | undelete | `CharacterServiceContext` |
| `HandleSavePersonalEmblem` | customization persistence | `CharacterServiceContext` |
| `SendCharCreate` | response marshal | `WowSessionAgent` / session response helper |
| `SendCharDelete` | response marshal | same |
| `SendCharRename` | response marshal | same |
| `SendCharCustomize` | response marshal | same |
| `SendCharFactionChange` | response marshal | same |
| `SendSetPlayerDeclinedNamesResult` | response marshal | same |
| `SendUndeleteCooldownStatusResponse` | response marshal | same |
| `SendUndeleteCharacterResponse` | response marshal | same |

## Service split

### CharacterServiceContext

Owns:
- account -> character roster
- create/delete/rename/customize
- login bootstrap
- profile persistence orchestration

Coroutine pattern:

```text
session -> call(CharacterService)
CharacterService:
  validate
  co_await call(DbService)
  co_await call(MapInstanceService)
  co_await call(CombatService)
  reply to session
```

### MapInstanceServiceContext

Owns:
- map shard / instance membership
- enter/leave instance
- movement state
- visibility snapshots
- map-local broadcasts

### CombatServiceContext

Owns:
- target selection
- auto attack / cast / cooldown
- death / release / resurrect
- combat-local authoritative state

## Migration order

1. Character select/login path: `ENUM/CREATE/LOGIN`
2. Map enter/leave + movement snapshot
3. Combat target/attack/cast
4. Inventory/quest/social split from current monolithic `WowGameWorld`
