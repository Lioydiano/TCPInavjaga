- [x] the field goes wearing out with time passing
- [x] when you kill a worm with an headshot, the rest of the body becomes bullets that you can harvest
- [x] the worms leave clay around
- [x] you need to eat meat not to starve
- [x] archers move through the wall breaches...
- [x] ...while worms turn randomly
- [x] mines get triggered by an archer stepping on it...
- [x] ...but for a worm it's sufficient for the head to pass nearby
- [x] loss condition: an enemy reaching top (TUNNEL_UNIT * 2)^2 square
- [x] enemies dieing leave resources in the form of chests that replace them
- [x] provide an option to drop the inventory in a chest on death

# Multiplayer architecture

It will be a client-server architecture over TCP.

## Handshake, flow

```mermaid
---
config:
  theme: dark
---
sequenceDiagram
    Server->>+Client: SEED
    Server->>+Client: CONSTANTS
    loop Until agreed
        Client->>+Server: COORDS_OFFER
        Server->>-Client: COORDS_RESPONSE
    end
    activate Client

    Note left of Server: End lobby phase
    Server->>+Client: ALL_PLAYERS
    Note left of Server: Start game
    
    loop Main game loop
        Server->>+Client: ACT
        Client->>+Server: OWN_ACT
        Server-->>+Client: PLAYER_LEFT
    end

    Client-->>Server: DISCONNECT
    Note over Client,Server: Gracefully leave the game
    Server-->>Client: END
    Note left of Server: Game finished
```
_A sequence diagram of the communication between client and server_

- consider that a client disconnect could just be a `Q` sent by the player

## TCP sockets

- https://stackoverflow.com/a/13021852/15888601
