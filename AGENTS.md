# Agent notes

## Launching the game on Windows

The agent (and anything else you run over SSH / from the OpenCode background
service) runs in **Windows session 0**, which has **no GPU and no interactive
desktop**. Launching the game directly from here means it cannot create a
Direct3D device and fails to start ("no GPU available" / no window).

Always start the game through the **`CS2Launch` scheduled task**, which runs in
the interactive session (session 1) where the GPU lives:

```powershell
schtasks /run /tn CS2Launch
# or
powershell -NoProfile -c "Start-ScheduledTask -Name CS2Launch"
```

That task runs `scripts/launch-cs2.cmd` in session 1 (Steam `-applaunch 730`
with the offline flags `-insecure -novid`). To change the flags, edit
`scripts/launch-cs2.cmd`.

Do **not** run `cs2.exe` or `steam.exe` directly from an agent/SSH shell for
play/injection testing — it will land in session 0 and there will be no GPU.
