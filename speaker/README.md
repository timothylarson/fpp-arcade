# Arcade Speaker

A speaker built into a game controller, so each player hears their own game. It's an ESP32-S3
with an I2S amplifier and a small full-range speaker, on Wi-Fi. FPP Arcade doesn't stream
audio to it: the speaker keeps its own copy of the game sounds and FPP sends it a tiny "play this"
message for each one, so sounds play within milliseconds of the event and a lost packet on a
busy network costs at most one sound.

The controller's buttons stay on their own wireless board; the speaker is separate, so a sound
problem never affects play.

## Parts (one per controller)

| Part | Notes | About |
|---|---|---|
| ESP32-S3 dev board, **N16R8** (16 MB flash, 8 MB PSRAM), e.g. ESP32-S3-DevKitC-1 | The sounds are kept in flash and played from PSRAM. | $8-12 |
| MAX98357A I2S amplifier board | 3 W into 4 ohms from 5 V, digital input so no hiss. | $2-5 |
| 3"-4" full-range speaker, 4 ohms, 5-10 W | A real cone carries outdoors; a weather-resistant (marine) one suits outdoors. | $8-15 |
| USB-C cable, short | Power bank to the ESP32. | |
| Hook-up wire, standoffs, acoustic mesh or speaker cloth | | |

## Wiring

```
                 ESP32-S3-DevKitC-1                     MAX98357A
               +--------------------+                +-------------+
 power bank ---| USB-C              |                |             |
   USB out 1   |                 5V |----------------| VIN         |
               |                GND |----------------| GND         |
               |             GPIO 5 |----------------| BCLK        |      +---------+
               |             GPIO 6 |----------------| LRC         |      | speaker |
               |             GPIO 7 |----------------| DIN      +  |------| +       |
               |             GPIO 8 |----------------| SD       -  |------| -       |
               +--------------------+                |  GAIN (open)|      +---------+
                                                     +-------------+
 power bank --- micro-USB charge port of the controller's own wireless board
   USB out 2
```

- **SD** is driven by the ESP32: high plays, low switches the amp off after two quiet seconds, which
  saves battery and stops any hiss between sounds.
- **GAIN** left open is 9 dB. Tied to GND it's 12 dB, to VIN 6 dB. Start open and use the volume
  setting; go to 12 dB only if it's too quiet at 100%.
- Never connect either speaker wire to ground: the amp drives both.
- Keep the speaker leads short and away from the ESP32's antenna end.
- Any free GPIOs will do; the pin numbers are at the top of `ArcadeSpeaker.ino`.

## Power

A 20,000 mAh bank is about 60 Wh usable at 5 V. The speaker averages roughly 1-1.5 W (the ESP32
keeps its radio awake so sounds play instantly; the amp only draws while playing), which is
40 hours or more: a few weeks of three-hour nights between charges.

The second USB output keeps the controller's own small battery charged. Many power banks switch
themselves off when the current drawn is small; the ESP32's steady draw usually prevents that,
but leave a controller running for an evening before the event to be sure.

## Enclosure

- A downward-firing speaker works well with 10-15 mm of feet or standoffs under the controller,
  so the sound can get out; the ground reflects it back up to the players.
- Seal the back of the speaker against the inside of the controller so the body works as a closed
  box, which gives fuller sound than a bare driver.
- Cover the holes with acoustic mesh or speaker cloth. Facing down keeps rain off the cone; keep
  the ESP32 and amp away from the holes (December nights bring condensation).

## Building the firmware

In the Arduino IDE (2.x, with the "esp32" boards package by Espressif, version 3.x), open
`ArcadeSpeaker/ArcadeSpeaker.ino`, choose **ESP32S3 Dev Module** and set:

- Flash Size: **16MB (128Mb)**
- PSRAM: **OPI PSRAM**
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**

then Upload. Or from a terminal, with the Arduino CLI:

```bash
arduino-cli compile --upload -p /dev/cu.usbmodem1101 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" ArcadeSpeaker
```

## Setting it up

1. Power it up. With no Wi-Fi set, it makes its own network, **ArcadeSpeaker-xxxx**. Join it
   from a phone; the settings page opens (or browse to 192.168.4.1).
2. Enter the Christmas lights Wi-Fi network and password, the FPP player's address, and the
   **model it plays for**: the overlay model this controller plays, as on the Arcade Games page
   (for example Matrix-Left). Save; it restarts and joins the network.
3. On FPP, turn on **Sound** on the Arcade Games page. The speaker appears under *Controller
   speakers* within a few seconds, and fetches the sound files by itself (a minute or so the
   first time).
4. Its own page (click its name on the Arcade Games page) shows what it's doing and has a volume
   slider, a Test sound button and Fetch sounds now.

Sounds added on FPP are picked up when the Arcade Games page is saved, or within a minute.

## Notes

- Sounds are fetched at 22.05 kHz mono as 8-bit mu-law, which is plenty for a small speaker
  (about 38 dB signal to noise) and half the size of 16-bit. Every sound is kept in the 8 MB of
  PSRAM so it plays instantly, which is room for about six minutes; all the games' sounds
  together come to about four and a half (6.1 MB). The speaker's page shows how much flash is in
  use, and a sound that doesn't fit is reported on the serial log as "No room for ...".
- Firmware 1.0 kept 16-bit sounds, and they no longer all fit. Firmware 1.1 clears them out the
  first time it starts and fetches them again as mu-law; it needs a plugin version that offers
  mu-law (with an older one it still works, fetching 16-bit).
- If it can't reach the Wi-Fi it keeps trying and offers its set-up network meanwhile.
- Messages are plain UDP (see `src/ArcadeSound.h` in the plugin), so a sound-effects speaker can
  be built from anything that can play a clip when told to.
