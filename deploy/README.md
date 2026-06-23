# Deploying with podman + Quadlet (rootless)

Runs Telegram Echo as a rootless systemd service via [Quadlet](https://docs.podman.io/en/latest/markdown/podman-systemd.unit.5.html).
Requires **podman ≥ 4.4** on the server. The image is public on GHCR, so no
registry login is needed to pull.

## 1. Install the unit and config

```sh
mkdir -p ~/.config/containers/systemd ~/.config/tg-echo
cp deploy/tg-echo.container ~/.config/containers/systemd/
cp deploy/tg-echo.env.example ~/.config/tg-echo/tg-echo.env
```

Edit `~/.config/tg-echo/tg-echo.env` and set `API_ID` / `API_HASH`
(from <https://my.telegram.org/apps>).

## 2. Create data dirs and a prompt

```sh
mkdir -p ~/.local/share/tg-echo/{tdlib_db,recordings}
```

Provide the audio prompt (48 kHz stereo MP3) played to callers — copy your own to
`~/.local/share/tg-echo/prompt.mp3`, or generate one with the tools image:

```sh
# simple beep prompt
podman run --rm -v ~/.local/share/tg-echo:/out \
  ghcr.io/nikicat/tg-echo-tools:latest prompt

# or a GLaDOS TTS prompt
podman run --rm -v ~/.local/share/tg-echo:/out \
  ghcr.io/nikicat/tg-echo-tools:latest \
  glados-prompt GLADOS_TEXT="Please leave a message after the beep."
```

## 3. One-time Telegram login

Authenticate once to create the TDLib session in `tdlib_db` (interactive — enter
your phone number and the login code Telegram sends):

```sh
podman run --rm -it \
  --env-file ~/.config/tg-echo/tg-echo.env \
  -v ~/.local/share/tg-echo/tdlib_db:/app/tdlib_db:Z \
  ghcr.io/nikicat/tg-echo:latest auth
```

## 4. Start it

```sh
loginctl enable-linger "$USER"          # keep the service running with no login session
systemctl --user daemon-reload          # generate the service from the .container unit
systemctl --user start tg-echo  # [Install] handles auto-start at boot
```

Check status / logs:

```sh
systemctl --user status tg-echo
journalctl --user -u tg-echo -f
```

## 5. Updates (optional)

The unit sets `AutoUpdate=registry`; enable the timer to pull a newer `:latest` and
restart automatically:

```sh
systemctl --user enable --now podman-auto-update.timer
```

To pin a release instead, set `Image=ghcr.io/nikicat/tg-echo:v1.2.3` in the
unit and drop the `AutoUpdate=registry` line.

## Notes

- **`Network=pasta`** needs the `passt` package (default on recent distros; it's the
  rootless default on podman ≥ 5). If unavailable, remove that line to fall back to
  the default rootless network.
- `%h` expands to the user's home. If your systemd doesn't expand it in a value,
  replace `%h/...` with absolute paths.
- **Rootful variant:** put the `.container` in `/etc/containers/systemd/`, replace
  `%h/...` with absolute paths (e.g. `/var/lib/tg-echo/...`), then
  `sudo systemctl daemon-reload && sudo systemctl start tg-echo`.
