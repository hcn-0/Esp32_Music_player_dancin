# Dancin ESP32 OLED Player

A beat-synced, visual OLED player for **Aaron Smith - Dancin (KRONO Remix)** on ESP32 + SSD1306 (128x64).

## Visual Layouts & Animations

The display adapts dynamically to the song's energy and lyrics:

| Layout | Screen Behavior | Content & Animation |
|--------|-----------------|---------------------|
| **Side-by-Side (Art Left, Lyrics Right)** | Character cleanly framed in Left 64px, lyrics in Right 64px | Character bobs to the rhythm and animates poses on every beat. Lyrics reveal word-by-word with drop-in bounce. |
| **Side-by-Side (Lyrics Left, Art Right)** | Lyrics in Left 64px, character in Right 64px | Same dynamic animation, alternating sides so the screen stays engaging. |
| **Kinetic Center (Big Hook)** | Full-screen massive bold typography | Used for high-impact lines (e.g. `MINE`, `DANCIN'`). Screen shake on beat drops + dynamic audio equalizer bars at the bottom. |
| **Full Anime Romance Scene** | 128x64 couple pinky-promise scene | Plays automatically during instrumental breaks, intros, and guitar drops with an animated beating heart and floating sparkles. |

### Character Artworks
1. **Dancing Anime Boy (2 Poses):** Alternates between Pose 1 (rocking with jacket) and Pose 2 (finger pointing up) on every musical beat.
2. **Shrugging Chibi Boy:** Displays for playful and questioning lines ("*Can't you see*", "*I don't know why*").
3. **Romantic Anime Couple:** Displays for romantic lines ("*Everytime when I look in your eyes*", "*Honey you are for me*").
4. **Floating Music Notes & Sparkle Stars:** Drift upward from the character on beats.

> [!NOTE]
> Characters are isolated 64x64 bitmaps cleanly framed with generous margins and no divider line — they **never overlap, cover, or cut off** the lyrics.
> Display color is inverted by default (`#define INVERT_DISPLAY true`), giving a bright manga-paper aesthetic with dark ink artwork and typography. You can change this to `false` in `DancinPlayer.ino` for a dark background.

## How to Upload to ESP32

1. Open [`esp32/DancinPlayer/DancinPlayer.ino`](DancinPlayer/DancinPlayer.ino) in the Arduino IDE.
2. Verify you have installed:
   - **Adafruit SSD1306**
   - **Adafruit GFX Library**
3. Select board: **ESP32 Dev Module** (or **DOIT ESP32 DEVKIT V1**) and choose the COM port.
4. Click **Upload** (hold **BOOT** button on the ESP32 if upload stays at `Connecting...`).
5. Close the Arduino Serial Monitor when done.

### I2C Connections (SSD1306 128x64)
- **VCC** -> 3V3
- **GND** -> GND
- **SDA** -> GPIO 21
- **SCL** -> GPIO 22
- Address: `0x3C` (default; change `OLED_ADDR` to `0x3D` if your display uses it).

## Run the Player

From the project root:

```powershell
python dancin_player\dancin.py
```

- If you want to specify the COM port manually:
  ```powershell
  python dancin_player\dancin.py --port COM5
  ```
- **Controls:**
  - `SPACE`: Play / Pause / Resume
  - `S`: Stop
  - `,` / `LEFT`: Seek -5s
  - `.` / `RIGHT`: Seek +5s
  - `Q`: Quit
