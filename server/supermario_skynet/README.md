# Super Mario Skynet Server

Run from the repository root:

```bash
./skynet/skynet ./server/supermario_skynet/config.lua
```

Default listen address:

- host: `127.0.0.1`
- port: `7779`

Protocol:

- `INPUT seq vx vy`
- `MOVE dx dy`
- `POS x y`
- `ATTACK playerId`
- `COIN coinId`
- `STATE`
- `PING`
- `QUIT`

The server broadcasts authoritative snapshots periodically and also replies to
explicit `STATE` requests, so the existing `client/super_mario_client.cpp`
stays compatible.
