# InkOS Studio / ShadUI

InkOS Studio uses C#/.NET 10, **ShadUI 0.2.4 and Avalonia 12.1.0**. White surfaces, fine dividers and embedded IBM Plex Mono Regular/Light follow the display's visual style. ShadTheme supplies the actual control library; small app styles adjust typography, spacing and monochrome appearance without replacing its control templates.

Run the complete Windows package from Releases, or `../out/studio/InkOS Studio.exe` after publishing. The Windows x64 folder includes its .NET runtime; keep the whole folder together. Select USB and the actual serial port, then **Connect / Read**, edit and **Save to screen**. USB pairing enables encrypted IP connections through the same pages. The computer and this app can then be closed.

## Settings

- **Calendars:** private iCloud CalDAV discovery and selection of one event calendar, plus separate Apple, Google and Outlook read-only ICS subscriptions. A dedicated Apple app-specific password is sent to the ESP32; it is never read back. See [iCloud setup](../docs/ICLOUD.md). URLs are masked, transmitted over USB or paired encrypted IP and stored on the ESP32. No desktop settings/token file is written.
- **Place & clocks:** location label, latitude/longitude, local time zone, two world-clock labels/time zones, Open-Meteo or MET Norway weather, API contact and weather animation toggle. Supported time zones are explicit in the dropdown; daylight-saving rules are calculated on the ESP32.
- **Spotify:** Client ID, official PKCE authorization/re-authorization, account status and removal of device credentials. No Client Secret is needed. Register `http://127.0.0.1:8768/callback` in your Spotify developer app. The callback listener exists only during authorization. The older Python setup helper uses port 8767; its Redirect URI is different.
- **Network:** Wi-Fi SSID and keep/replace/open password modes, separate Save Wi-Fi, device IP, USB-only pairing/revocation. IP configuration uses AES-256-GCM. See [network setup](../docs/NETWORK.md).
- **Logs (Studio 0.7.3, firmware 0.7.7):** device logging on/off, the `/logs` file list, a viewer that downloads and decompresses rotated `.tar.gz` archives, line filter, live tail of the RAM ring, save to disk and clear with confirmation. See [device logs](../docs/LOGS.md).
- **Device:** Previous, Next, Home controls use the same two-stage navigation as the physical keys. Only the top-left title changes while selecting; the full page refreshes after the title becomes visible and input stops. Set Page selection delay to 0.1–5 seconds (default 0.5) and Save to screen. The panel's partial-refresh duration comes before this wait.

Apple uses an iCloud calendar's shared/public webcal link. Google supports the private iCal address. Outlook uses a published ICS URL, subject to organization policy. Private CalDAV is available for iCloud; Google Calendar OAuth and Microsoft Graph OAuth are not implemented. It does not publish calendars or change account sharing settings for you.

## Code boundaries

`MainWindow` owns the shell and sidebar selection only; `Views/` contains seven independent views (`LogsView` with `MainViewModel.Logs` and `LogArchive`). `MainViewModel` coordinates editable settings and validation; `MainViewModel.Commands` provides UI-neutral commands, busy gating and cancellation. `DeviceClient` serializes requests through `UsbTransport` or `NetworkTransport` and parses only matching InkOS responses. `SpotifyAuthorization` owns PKCE, the temporary loopback callback and token exchange. `Models` contains the DTOs and supported place/timezone catalog.

Frequent Home updates (weather animation and blinking colon) default to off after sustained fast refresh faded physical text and dirtied the background. Minute clock updates continue. Re-enabling uses the factory waveform, so 0.5 s cadence is not guaranteed. The experimental short waveform was removed from the firmware.

The device's typed settings, subscription fetch, normalized agenda, renderer and panel refresh policy remain separate firmware modules. The desktop app never renders production screen pages or fetches routine screen data on its behalf.

## Build

With the .NET 10 SDK:

```powershell
dotnet restore InkOS.Studio/InkOS.Studio.csproj --locked-mode
dotnet publish InkOS.Studio/InkOS.Studio.csproj -c Release -r win-x64 --self-contained true -o ../out/studio
```

NuGet dependencies and transitive versions are pinned in `packages.lock.json`: ShadUI 0.2.4, Avalonia Desktop/Headless 12.1.0 and System.IO.Ports 10.0.8. ShadUI and Avalonia use MIT licenses; IBM Plex Mono uses OFL. Published third-party notices accompany the executable. Build outputs under `bin`/`obj` are not source.

Developer checks: `--network-smoke` temporarily pairs over USB and checks real encrypted IP settings/navigation, wrong keys, replay rejection and Wi-Fi keep, then revokes pairing; `--self-test` checks PKCE, configuration defaults, coordinate mapping and disconnected command gating; `--device-smoke` checks the real COM9 settings protocol and navigation wrap without exposing calendar URLs; `--logs-smoke [--port COM9]` checks the device log commands end to end (see docs/LOGS.md); `--render-preview` uses Avalonia Headless + Skia to render the actual ShadUI visual tree, verify masked-URL two-way binding/sidebar clicks, and check minimum window size without an interactive window. It verifies layout, not physical e-paper contrast. Do not log settings_get responses: they contain subscription URLs.

## Energy / 0.3
The Energy section configures FI data source (Sähkötin / Porssisahko / compatible custom HTTPS), VAT 25.5%, margin 0.45 ct/kWh, transmission 6 ct/kWh, and whether both fees already include VAT. Defaults exclude VAT: (net spot + margin + transmission) × (1 + VAT/100). Keys are masked and write-only: blank keeps the stored key; remove clears it. Presets need no key. Seven screen pages include Moon, Sun and Energy. See ../docs/ENERGY.md for source terms, units and limits.

## Firmware upgrades (Studio 0.5)

Choose IP, enter the device IP and existing pairing key, Connect / Read, then Network → Firmware → Choose OTA package → Upgrade + restart. Keep power connected until the app confirms the new slot is healthy. See [OTA and first-time USB migration](../docs/OTA.md).
