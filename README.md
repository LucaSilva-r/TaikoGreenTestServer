# Taiko Green Test Server

ASP.NET Core test server for Taiko no Tatsujin Green with a Docker runtime that supports the legacy TLS profile used by the game.

## Docker

Build and start the server:

```sh
docker compose up -d --build
```

The container uses host networking and listens on:

- `80` for the AMAuth `PowerOn` endpoint
- `10122`, `54430`, and `54431` for legacy TLS endpoints

Follow logs:

```sh
docker compose logs -f
```

Stop the server:

```sh
docker compose down
```

## RPCS3 Host Switches

For local testing, map the game hosts to localhost in RPCS3:

```txt
mobirouter.loc=127.0.0.1&&tenporouter.loc=127.0.0.1&&bbrouter.loc=127.0.0.1&&dslrouter.loc=127.0.0.1&&naominet.jp=127.0.0.1&&v402-front.mucha-prd.nbgi-amnet.jp=127.0.0.1&&vsapi.taiko-p.jp=127.0.0.1
```

