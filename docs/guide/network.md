# Network play

The original's network game (DirectPlay, up to 8 players) runs over a **gasm-relay**, a small server
that passes the players' messages along. Everyone who plays together connects to the same relay and
the same room; the desktop version and the browser player can be mixed. The network game is still
being tested ([Status](/guide/status)): reports of how it plays are welcome.

## What you need

- Each player: their own copy of Colin McRae Rally 2.0 (see [Game data](/guide/game-data)) and OpenCMR2
  on the desktop or in the [browser](/play/).
- One relay that every player can reach. One of you runs it, on your own computer for a local network,
  or on a server for play over the internet.
- A room name you agree on (letters, digits, `-` or `_`; the default is `opencmr2`). A room holds one
  game session; a relay can hold many rooms, so groups playing at the same time each use their own.

## Running the relay

`gasm-relay` comes with gasm: it is in the gasm release bundles for macOS, Linux and Windows
([gasm releases](https://github.com/emdzej/gasm/releases), 0.16.1 or newer), and as a container image.

```sh
gasm-relay 0.0.0.0:9000 --max-peers 8        # port 9000 on every network interface, up to 8 players a room
docker run -p 9000:9000 ghcr.io/emdzej/gasm-relay 0.0.0.0:9000 --max-peers 8
```

`--max-peers` matters: the relay lets only **2 players** into a room unless told otherwise, and the
game takes up to 8.

The players then use the relay's address:

- the same computer: `ws://127.0.0.1:9000`;
- a local network: `ws://<the relay computer's address>:9000`, for example `ws://192.168.1.20:9000`
  (allow incoming connections on the port in that computer's firewall);
- over the internet: a server, or a port forwarded on your router to the relay computer.

**Browser players need `wss://`** (an encrypted connection) when the relay is not on their own
computer: the site is served over HTTPS, and browsers block unencrypted connections from it. Run the
relay with a certificate for its host name:

```sh
gasm-relay 0.0.0.0:9443 --max-peers 8 --tls-cert fullchain.pem --tls-key privkey.pem
```

and use `wss://relay.example.com:9443`. Desktop players can use either.

## Joining from the desktop

Start OpenCMR2 with the network allowed and the relay and room given; they go after the game data (or
on their own, once the launcher remembers the data):

```sh
# Linux
./opencmr2.sh --allow-net --param relay=ws://192.168.1.20:9000 --param room=friday

# macOS
open "Colin McRae Rally 2.0 (gasm).app" --args --allow-net --param relay=ws://192.168.1.20:9000 --param room=friday

# Windows
OpenCMR2.cmd --allow-net --param relay=ws://192.168.1.20:9000 --param room=friday

# any system, with gasm-run
gasm-run opencmr2.wasm --asset cd=CMR2.ISO --allow-net --param relay=ws://192.168.1.20:9000 --param room=friday
```

`--allow-net` lets the game open the connection; without it, or without a relay, the network menu
finds no connection.

## Joining from the browser

On the [play page](/play/), fill in **relay** and **room** under *network game* before pressing
Play. The page remembers them. Leave the relay empty to play offline.

## In the game

1. Choose **network** in the main menu, then your driver profile (or play as a guest and enter your
   name).
2. Choose the connection, **Internet (relay)**: the game opens the relay's room.
3. One player **creates** the session: its name, the game type (single rally, single stage, rally time
   trial, arcade race or arcade time trial), the number of players and, if you like, a password. That
   player is the host.
4. The others **join**: the session appears in the list of the room's sessions.
5. In the **waiting room** the host sets up the game (the rally, its stages, the weather), everyone
   chooses a car and marks **ready**, and the host starts. The chat line works here and between
   stages.

After each stage come the stage's results, the rally's and, between stages, the host's choice to go on,
restart or go back to the waiting room. If the host leaves, another player becomes the host, as in the
original.

## If it doesn't connect

- Everyone must use the same relay address and the same room name, exactly.
- A third player who can't join: start the relay with `--max-peers 8` (it lets 2 into a room by
  default; its log then says the room is full).
- From the desktop, check that `--allow-net` is there. The relay prints a line for each connection;
  nothing there means the address or the firewall is wrong.
- In the browser, a `ws://` relay on another computer is blocked: use `wss://`.
- The game's own log (the launcher's `gasm.log`, or the browser's console) says why a connection failed.
