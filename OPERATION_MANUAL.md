# nicAI950 Operation Manual

[Back to the project README](README.md)

- [Quick start](#quick-start)
- [Controls](#controls)
- [Frequency and memory modes](#frequency-and-memory-modes)
- [Squelch, bandwidth, and tones](#squelch-bandwidth-and-tones)
- [Broadcast tuner](#broadcast-tuner)
- [GPS](#gps)
- [APRS](#aprs)
- [Settings reference](#settings-reference)
- [Troubleshooting](#troubleshooting)

## Quick start

1. Turn the radio on. The **RADIO** screen shows VFOs A, B, and C, with the active VFO highlighted. Battery voltage appears at the top right.
2. Press **ABC** to choose a VFO. Press **V/M** to switch that VFO between frequency and memory modes.
3. In frequency mode, use **UP** and **DOWN** to tune by the selected step. To enter **145.500 MHz** directly, press `1` `4` `5` `*` `5` `0` `0`, then **MENU**.
4. Hold **PTT** on the **RADIO** screen to transmit on the selected frequency or memory channel. Release it to receive. The red LED indicates transmit and the green LED indicates open receive audio.

With default settings, VFO A starts at **145.500 MHz**, B at **433.500 MHz**, and C at **29.600 MHz**. Each starts with a **12.5 kHz** tuning step, **12.5 kHz** RX and TX bandwidths, squelch **3**, and a TX power setting of **10%**.

## Controls

| Control | Action |
| --- | --- |
| **MENU** | Open Settings from **RADIO**. During numeric entry, confirm the number. On feature screens, run the action in the footer. |
| **EXIT** | Cancel numeric entry or return to **RADIO**. In the APRS callsign editor, return to Settings. |
| **ABC** | Select the next VFO in order A, B, C. |
| **V/M** | Switch the active VFO between frequency and memory modes. |
| **UP** / **DOWN** | Tune, change channel, or move the menu selection. The rotary encoder acts like these keys. |
| **LEFT** / **RIGHT** | Tune in 1 MHz steps, move five memory slots, or change the selected setting. During numeric entry, **LEFT** erases the last character. |
| **0–9** | Enter a frequency or memory slot. Enter digits directly in the callsign editor. |
| **`*`** | Enter a decimal point in a frequency or a space in the callsign editor. |
| **SK1** | Open the broadcast tuner. Press again to return to **RADIO**. |
| **SK2** | Open GPS. Press again to return to **RADIO**. |
| **SK3** | Open APRS. Press again to return to **RADIO**. |

Voice PTT works from **RADIO** with a valid frequency or stored channel and numeric entry closed. Handset and external-microphone PTT use the active VFO. The radio ends a continuous voice transmission at **90 seconds**. Release PTT before starting another transmission.

## Frequency and memory modes

### Tune a frequency

**UP** increases the frequency and **DOWN** decreases it by the current **VFO STEP**. **RIGHT** and **LEFT** move up or down by **1 MHz**. For direct entry, enter MHz using `*` as the decimal point, with up to five decimal digits, and press **MENU** to tune or **EXIT** to cancel.

The frequency entry accepts **18–580 MHz** and **760–1160 MHz**. In simplex FM, the selected frequency is used for both receive and transmit.

### Store and use a memory

1. Select a VFO with **ABC**, switch it to frequency mode with **V/M** if needed, and set its frequency and radio settings.
2. Open **MENU** and highlight **CHANNEL SLOT**. Use **LEFT** or **RIGHT** to choose a slot from **1** to **32**.
3. Highlight **STORE VFO** and press **MENU**. This copies the VFO's frequency, squelch, bandwidths, tones, and TX power to that slot, replacing its existing record.
4. Press **EXIT**, then **V/M** to use memory mode. Use the arrows to browse, or enter a slot number and press **MENU** to select it.

Each VFO remembers its own selected slot. All three VFOs use the same set of 32 memories. Settings changed while a populated memory is active edit that memory. An unused slot shows **EMPTY**.

To delete a memory, select its **CHANNEL SLOT**, highlight **CLEAR CHANNEL**, and press **MENU**. The action clears the selected slot immediately.

## Squelch, bandwidth, and tones

### Squelch

From **RADIO**, press **MENU**, select **SQUELCH**, and use **LEFT** or **RIGHT**. Higher levels require a quieter EXNOISE reading before the speaker opens. Start at **3** and choose the lowest level that keeps the idle receiver quiet.

| Squelch level | Maximum EXNOISE reading for receive audio |
| --- | --- |
| 0 | Monitor mode bypasses the EXNOISE check |
| 1 | 40 |
| 2 | 38 |
| 3 | 36 |
| 4 | 34 |
| 5 | 32 |
| 6 | 30 |
| 7 | 28 |
| 8 | 26 |
| 9 | 24 |

At level **0** with **RX TONE TYPE** set to **OFF**, the speaker can play static between transmissions. A configured RX tone is still required at level **0**. The dBm reading represents signal strength. Voice squelch uses the transceiver's EXNOISE reading from register `0x65`. [BK4829 register table, page 33](BK4829.Registers.Table.pdf)

### Bandwidth and tones

Set **RX BANDWIDTH** and **TX BANDWIDTH** independently to **6.25**, **12.5**, or **25 kHz**.

To configure a tone, choose **RX TONE TYPE** or **TX TONE TYPE**, then use **RX TONE** or **TX TONE** to select the frequency or code. Each direction supports **OFF**, **CTCSS**, **DCS NORMAL**, and **DCS INVERTED**. The receive tone controls which signals open the speaker. The transmit tone is sent while you transmit.

## Broadcast tuner

Press **SK1** to open the Si4732 tuner. Use **LEFT** and **RIGHT** to select a band and **UP**, **DOWN**, or the encoder to tune. Press **MENU** to retry the current station if the tuner reports an error. Adjust the level from **TUNER VOLUME** in Settings.

| Band | Tuning range | Step |
| --- | --- | --- |
| FM | 87.50–108.00 MHz | 100 kHz |
| LW | 153–279 kHz | 9 kHz |
| MW | 520–1710 kHz | 1 kHz |
| SW | 2300–23000 kHz | 5 kHz |

Broadcast mode pauses VFO reception. Press **EXIT** or **SK1** to resume the active VFO.

## GPS

Press **SK2** to see GPS position and UTC. **MENU** toggles GPS power from this screen. When **WAITING FOR FIX** is shown, give the GPS time to acquire satellites with a clear view of the sky. **POSITION FIX** shows the latitude, longitude, and UTC time. Press **EXIT** or **SK2** to return to **RADIO**.

## APRS

### Set up a beacon

1. In Settings, highlight **APRS CALLSIGN** and press **MENU**. Enter a base callsign of three to six characters, including at least one letter and one digit. Use **UP** and **DOWN** to cycle characters and **LEFT** and **RIGHT** to move the cursor. Digits can be entered directly. Press **`*`** to blank a position and **MENU** to save.
2. Set the callsign suffix with **APRS SSID**, from **0** to **15**.
3. Set **APRS FREQUENCY** for your local APRS channel. The default is **144.800 MHz**. **LEFT** and **RIGHT** adjust it by **5 kHz**. Pressing **MENU** on that setting copies the active VFO or channel frequency.
4. Enable **GPS POWER** and wait for a fix. Set **APRS INTERVAL** to **MANUAL** or **1**, **2**, **5**, **10**, **20**, or **30 minutes**.

### Send or receive

Press **SK3** to open APRS. The radio listens on the configured APRS frequency and shows the packet count and the latest received packet's source, destination, and contents. Press **MENU** to send a position beacon. The radio sends when it has a recent GPS fix, a valid callsign, and a clear channel.

Timed beacons run while the **APRS** screen is open. The interval starts when the screen opens and restarts after each beacon. A busy channel or unavailable fix delays the next beacon. APRS uses **1200-baud AFSK** and sends direct AX.25 position beacons. Beacon transmission uses the active VFO or channel's TX power setting.

Press **EXIT** or **SK3** to return to the active voice VFO.

## Settings reference

From **RADIO**, press **MENU** with numeric entry closed. Use **UP** and **DOWN** to select an item and **LEFT** and **RIGHT** to change it. Press **MENU** to run an action and **EXIT** to return to **RADIO**.

| Setting | Values or action |
| --- | --- |
| **SQUELCH** | 0–9. See [squelch](#squelch). |
| **RX BANDWIDTH**, **TX BANDWIDTH** | 6.25, 12.5, or 25 kHz. |
| **RX TONE TYPE**, **TX TONE TYPE** | Off, CTCSS, DCS normal, or DCS inverted. |
| **RX TONE**, **TX TONE** | Frequency or code for the selected tone type. |
| **TX POWER** | 1–100% PA control setting. |
| **VFO STEP** | 5, 6.25, 10, 12.5, 25, or 50 kHz. |
| **CHANNEL SLOT** | Select memory 1–32. |
| **STORE VFO**, **CLEAR CHANNEL** | Press **MENU** to write or clear the selected memory. |
| **BACKLIGHT** | 10–100%. |
| **GPS POWER** | On or off. |
| **TUNER VOLUME** | 0–63. |
| **APRS FREQUENCY** | Adjust in 5 kHz steps or copy the active frequency with **MENU**. |
| **APRS CALLSIGN** | Press **MENU** to edit. |
| **APRS SSID** | 0–15. |
| **APRS INTERVAL** | Manual or 1, 2, 5, 10, 20, or 30 minutes. |

Changes apply immediately. After editing, return to **RADIO** and leave PTT released for a couple of seconds to let the radio save. Switching the radio off normally also saves pending changes.

## Troubleshooting

| Display or symptom | What to check |
| --- | --- |
| **EMPTY** | The selected memory has no frequency. Choose a populated slot or switch to frequency mode with **V/M**. |
| **CHECK FREQUENCY AND DECIMAL DIGITS** | Check the [frequency range](#tune-a-frequency), enter MHz using `*` as the decimal point, and use at most five decimal digits. |
| **Receive audio stays muted** | Check the frequency and **RX TONE TYPE**. Use squelch **0** and RX tone **OFF** to listen to the receiver directly. |
| **RSSI UNAVAILABLE** | The radio could not obtain an RSSI reading. Record the active VFO and frequency if the message persists. |
| **TUNER DID NOT RESPOND** | Press **MENU** to retry the selected broadcast frequency. |
| **WAITING FOR FIX** or **WAIT FOR A GPS FIX** | Check GPS power and allow time for the module to acquire a position. |
| **APRS CHANNEL BUSY** | Listen on the APRS screen until the channel clears, then retry a manual beacon. |
| **SET APRS CALLSIGN IN SETTINGS** | Enter your base callsign using **APRS CALLSIGN** and set the suffix using **APRS SSID**. |
| **FLASH ID** or another **FLASH** message | Record the exact footer message. To check persistence, switch VFOs, wait a few seconds, then power-cycle and check the selected VFO. |
