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

- **Bridge the IN-OUT pads** on the ESP32 board (two small pads by the USB ports on many N16R8
  boards) with a blob of solder. Unbridged, a diode stops USB power reaching the 5V pin, so the
  amp gets only a trickle through its signal pins: it plays quietly, then buzzes as soon as it's
  turned up (here, above about 22%). Bridged, the 5V pin is USB power itself, so never feed 5 V
  into that pin while USB is plugged in from somewhere else.
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

The first time it has to be over USB. After that (firmware 1.6 and later) it can be updated over
Wi-Fi, sealed in its controller: *Sketch › Export Compiled Binary* in the IDE, then the
**Firmware** section of the speaker's page, or from a terminal:

```bash
curl -F "firmware=@ArcadeSpeaker.ino.bin" http://left-controller.local/update
```

It goes into the spare half of the app space and is only switched to once it's all there and
checks out, so a failed upload leaves the old firmware running. Settings and sounds stay.

## Setting it up

1. Power it up. With no Wi-Fi set, it makes its own network, **ArcadeSpeaker-xxxx**. Join it
   from a phone; the settings page opens (or browse to 192.168.4.1).
2. Enter the Christmas lights Wi-Fi network and password, the FPP player's address, and the
   **model it plays for**: the overlay model this controller plays, as on the Arcade Games page
   (for example Matrix-Left). Save; it restarts and joins the network.
3. On FPP, turn on **Sound** on the Arcade Games page. The speaker appears under *Controller
   speakers* within a few seconds, and fetches the sound files by itself (a minute or so the
   first time).
4. Its own page (click its name on the Arcade Games page, or browse to
   **http://controller-speaker-xxxx.local**) shows what it's doing and has the volume, a Test
   sound button and Fetch sounds now. Its name starts as *Controller Speaker xxxx*, the last four
   characters of its MAC address, so each speaker is different. The name gives the address: rename
   it *Left Controller* and it's at http://left-controller.local.

Sounds added on FPP are picked up when the Arcade Games page is saved, or within a minute.

## Show music

A speaker can also play the music of FPP's show - a playlist's media, or a sequence's - in time
with the lights. Tick **Play the show's music** on its page (it restarts); the music has its own
volume slider beside the games'. Each speaker is set on its own, so only the ones you choose play
it.

On the FPP player running the show, tick **Send MultiSync Packets** (*FPP Settings › Playback*, or
*Status/Control › MultiSync*) and **Send MultiSync to ALL remotes via Multicast**, or *via
Broadcast* (*Status/Control › MultiSync › More Settings*), then **Restart FPPD**. FPP's default
sends only to the FPP players it knows, which leaves the speakers out. FPP then says, every half
second, which media file is playing and how far in. The speaker downloads that file from the same
FPP, decodes it itself and keeps to FPP's position, within a few milliseconds; the page's *Show
audio* line says how close it is.

- MP3s only (FPP's Music folder). A song in another format is left silent, and the page says so.
- Setting FPP's **Open/Start Delay** (also under *Playback*) to about 1000 ms gives the speaker a moment to start
  downloading before each song, so it's in time from the first note. Without it, it's a few tens
  of milliseconds late at first and catches up within a few seconds.
- A speaker that starts part-way through a song (switched on mid-show, or back from a Wi-Fi
  drop) reads the file from the top to find its place, which takes a few seconds, then joins in.
- If the music is early or late against the lights, **Music timing** on its page moves it.
- Game sounds play over the music as usual.

## Status light

The board's RGB LED (GPIO 48; 38 on some boards, set at the top of `ArcadeSpeaker.ino`) shows
what the speaker is doing:

| Light | Means |
|---|---|
| Green, steady | Ready: on the Wi-Fi, FPP answers and the sounds are loaded. After a minute it goes dark (unless *Keep the status light on* is ticked), and lights again if anything below happens |
| White blink | FPP just sent a sound to play (while the light is on) |
| Purple, flickering | Fetching sounds from FPP |
| Amber | Starting up, joining the Wi-Fi, or waiting to hear from FPP |
| Blue, blinking | Its set-up network is on: no Wi-Fi set, or it can't reach the one that is |
| Red, steady | FPP doesn't answer: check the FPP address, and that the player is up |
| Red, blinking | A fault in the speaker: no sounds loaded, no FAT partition (wrong partition scheme), or the amp output didn't start |

"FPP answers" means its list of sounds came back the last time the speaker asked (every minute).
FPP's Sound setting can still be off, in which case nothing plays; the Arcade Games page lists
the speaker either way.

## Notes

- Sounds are fetched at 22.05 kHz mono as 8-bit mu-law, which is plenty for a small speaker
  (about 38 dB signal to noise) and half the size of 16-bit. Every sound is kept in the 8 MB of
  PSRAM so it plays instantly, which is room for about six minutes; all the games' sounds
  together come to about four and a half (6.1 MB). The speaker's page shows how much flash is in
  use, and a sound that doesn't fit is reported on the serial log as "No room for ...".
- The amp is fed at 44.1 kHz, one of the rates the MAX98357A supports (22.05 kHz isn't), with a
  point halfway between each pair of samples; storing the sounds at 44.1 kHz would take twice the
  room, more than the speaker has, for nothing a 3" speaker can play.
- Firmware 1.0 kept 16-bit sounds, and they no longer all fit. Firmware 1.1 and later clear them out the
  first time it starts and fetches them again as mu-law; it needs a plugin version that offers
  mu-law (with an older one it still works, fetching 16-bit).
- If it can't reach the Wi-Fi it keeps trying and offers its set-up network meanwhile.
- Wi-Fi now and then holds a packet up for a few hundred milliseconds. FPP stamps every sound
  with its own clock, and the speaker skips a short sound (under a second) that arrives more
  than 150 ms later than usual, since a bounce heard after the bounce is worse than none; music
  and other long sounds still play. Its page shows how many it skipped and how late the
  latest was. Firmware 1.3 and later; with an older plugin every sound plays.
- Messages are plain UDP (see `src/ArcadeSound.h` in the plugin), so a sound-effects speaker can
  be built from anything that can play a clip when told to.
