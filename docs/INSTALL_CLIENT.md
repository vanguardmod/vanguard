# Installing VanguardMod (Client)

You usually do **not** need to install VanguardMod manually.

When you connect to a VanguardMod server, ETLegacy auto-downloads
the matching `vanguard_vX.Y.Z.pk3` from the server (visible as a
brief "Awaiting downloads…" screen on first connect). The download
is cached in your local game folder and reused on subsequent
connects to any server running the same version.

The client archive (`vanguard-vX.Y.Z-client.zip`) is provided for
players who want to **pre-stage** the mod — for example, on a
slow connection, on LAN where auto-download is disabled, or when
joining a server right after a release while seeders are still
warming up.

## Manual install

1. Unzip `vanguard-vX.Y.Z-client.zip` somewhere convenient.
2. Inside you'll find a single `vanguard/` directory containing the
   `.pk3`:

   ```
   vanguard/
   └── vanguard_vX.Y.Z.pk3
   ```

3. Copy the `vanguard/` directory into your ETLegacy game folder:

   - **Linux:** `~/.etlegacy/vanguard/`
   - **Windows:** `%APPDATA%\etlegacy\vanguard\`
   - **macOS:** `~/Library/Application Support/etlegacy/vanguard/`

4. Connect to a VanguardMod server. The pk3 hash matches → no
   download needed → no "Awaiting downloads…" screen.

## Verifying the install

In the in-game console (default key: `~`), check:

```
\fs_game vanguard
\dir vanguard
```

You should see `vanguard_vX.Y.Z.pk3` listed.

## Where the auto-downloaded pk3 lives

After your first connection to a VanguardMod server, the .pk3
will be in the same directory listed in step 3 above. You can
delete it any time — the server will re-deliver on next connect.

---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
