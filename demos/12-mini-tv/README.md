# Demo 12 · Mini TV

The robot becomes a tiny old TV. It plays videos from its microSD card on its screen, with sound, and you change the channel by tapping it: static, a hiss and "CH 2" in green, like the TVs of the eighties. The laptop turns any video into a format the robot can play and sends it over Wi-Fi, so you never have to take the card out. You'll learn how a video is made of pictures and sound, why the sound has to set the pace, and how to send big files to a microcontroller over HTTP.

```
laptop: video.mp4 --ffmpeg--> 320x240 JPEGs + 16 kHz sound --> clip.tv --HTTP (Wi-Fi)--> robot: /tv/clip.tv on the microSD
robot:  /tv/*.tv = channels --> JPEG on the screen + sound on the speaker
```

| On the screen | What it does |
|---|---|
| **Tap** | Next channel |
| **Hold** (half a second) | Pause and resume |
| **Tap the top band** | Volume: left half down, right half up |

## What you need

- **The robot with a microSD card** formatted as **FAT32** (a 32 GB card works). Cards of 64 GB or more come as exFAT and the robot can't read them: format them as FAT32 first.
- **The laptop** on the same Wi-Fi as the robot, with **ffmpeg** (`sudo apt install ffmpeg`; on Windows, `winget install ffmpeg`).
- **Videos.** The demo downloads the ones in [`channels.txt`](channels.txt): Google's video about its centre in Málaga and the trailers of *Sintel* and *Big Buck Bunny*, short films by the Blender Foundation under the [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) licence (you can show them anywhere as long as you credit them; the robot shows the credit on screen). You can add any video of your own.

## Run it

Plug the robot in over USB-C and run:

```bash
demos/12-mini-tv/run.sh               # flash the robot and upload the channels of channels.txt it doesn't have
demos/12-mini-tv/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/12-mini-tv/run.sh --no-flash    # don't flash: list the channels and upload the missing ones
demos/12-mini-tv/run.sh --help        # every option of this demo
```

Once the firmware is on the robot, these talk to it over Wi-Fi without flashing again:

```bash
demos/12-mini-tv/run.sh channels                        # download and upload what's missing from channels.txt
demos/12-mini-tv/run.sh add ~/Videos/clip.mp4           # convert and upload a video as a new channel
demos/12-mini-tv/run.sh add film.mkv --start 60 --duration 120 --title "My film"
demos/12-mini-tv/run.sh list                            # the channels on the card and the space used
demos/12-mini-tv/run.sh remove clip                     # delete a channel
```

On Windows use `run.bat` with `-FlashOnly` and `-NoFlash`, and `run.bat channels`, `run.bat add VIDEO`...

### The channel list

[`channels.txt`](channels.txt) is the list of channels the course robot carries. One per line: file name, title shown on screen and video, which can be a web address (mp4, ogg, webm… or an HLS `.m3u8` stream like those of news sites) or a file on the laptop:

```
00-google-malaga | Google en Málaga - Centro de Ingeniería de Seguridad | https://hls.diariodesevilla.es/.../master.m3u8
big-buck-bunny-trailer | Big Buck Bunny (trailer) - Blender Foundation, CC BY 3.0 | https://download.blender.org/peach/trailer/trailer_400p.ogg
sintel-trailer | Sintel (trailer) - Blender Foundation, CC BY 3.0 | https://download.blender.org/durian/trailer/sintel_trailer-480p.mp4
```

`run.sh` downloads each video into `videos/` (not in git), converts it and uploads it, **only if the card doesn't have it yet**: running it again doesn't repeat anything. To upload them all again (after changing the quality, say), add `--force`. The channels are sorted by file name, so the `00-` prefix makes the Google video channel 1.

The Blender trailers are CC BY 3.0. The Google video belongs to Google: it's fine to show it to the course group, but don't publish it.

### From another computer

The videos live on the robot's microSD card, not in the firmware or the repo. Flashing any demo doesn't touch the card, so the robot keeps its channels whichever computer flashes it. Another computer only needs this to fill a new card or another robot:

1. Clone the repo and run `./setup.sh` (`setup.bat` on Windows).
2. Install ffmpeg: `sudo apt install ffmpeg` (Windows: `winget install ffmpeg`).
3. Connect it to the **same Wi-Fi as the robot**.
4. Run `demos/12-mini-tv/run.sh` (`demos\12-mini-tv\run.bat` on Windows). It flashes the robot, downloads the videos of `channels.txt` and uploads the missing ones. If the robot already has demo 12: `run.sh channels`.
5. Check them with `demos/12-mini-tv/run.sh list`.

The laptop finds the robot as `stackchan-tv.local`. If that name doesn't work on your network, pass the IP that the robot's screen shows: `run.sh list --robot 192.168.1.134`.

| Option of `add` | What it does |
|---|---|
| `--start S`, `--duration S` | Only a part of the video, in seconds (or `hh:mm:ss`) |
| `--title TEXT` | What shows on screen when you switch to that channel |
| `--name NAME` | The file name on the card (default: from the video's name) |
| `--fit` | The whole picture, with black bars. By default it fills the screen and cuts the sides |
| `--fps N` | Frames per second (default 15) |
| `--quality N` | JPEG quality, from 2 (best, bigger file) to 31 (default 6) |

A minute of video takes about 5-6 MB on the card. A 32 GB card holds about four days of TV.

## How it works

### The .tv file

A video file (mp4, mkv...) is compressed with H.264 or similar codecs that an ESP32-S3 can't decode in real time. So the laptop does the hard part. With ffmpeg it cuts the video into **15 pictures per second**, scales each one to the screen's 320×240 and saves it as a **JPEG**, which the robot's graphics library (M5GFX) can decode. The sound becomes **16-bit mono PCM at 16 kHz**: raw samples, without compression, ready for the speaker.

`tv.py` puts it all in one file. After a 128-byte header (size, frames per second, sound rate, number of frames and the title), each frame comes with its own piece of sound:

```
"STV1" w h fps rate frames title | jpeg size, sound size, JPEG, sound | jpeg size, sound size, JPEG, sound | ...
```

So the robot reads the file from start to end, without jumping around, and picture and sound always arrive together.

### The sound sets the pace

The robot never waits for a clock to show the next picture: it waits for the **speaker**. The speaker plays one piece of sound and holds the next one in a queue. When there is room in the queue, the robot reads the next frame, queues its sound and draws its JPEG. Each piece lasts exactly one frame (1/15 s), so the pictures follow the sound.

If a JPEG takes too long to decode (a very detailed frame), the robot falls behind. Then it **skips drawing** the next frames, but still plays their sound, until it catches up. We hear any gap in the sound much more than a missing picture, so the sound must never stop.

### Sending a file to the robot

The robot runs a small web server (`WebServer`, port 80) and announces itself with **mDNS** as `stackchan-tv.local`. `tv.py` sends the file in a `multipart/form-data` POST, the same thing a browser does when you upload a photo to a website. The robot receives it in pieces of about 1.4 KB, gathers them in a 32 KB block and writes the block to the card. Writing to a microSD card in small pieces is very slow. The file is saved as `.part` and renamed when it is complete, so a cut upload never leaves a broken channel.

| Address | What it does |
|---|---|
| `GET /list` | The channels: name, bytes, frames, fps, title |
| `GET /info` | Card size and space used, in MB |
| `POST /upload?size=N` | Saves the file in `/tv` |
| `POST /delete?name=X.tv` | Deletes a channel |

### The microSD card and the screen share a bus

On the CoreS3 the card and the screen hang from the same SPI bus (pins G35, G36 and G37; CS G4 for the card). That's why the robot never reads the card and draws at the same time: everything happens in the `loop()` task, one thing after another. The servos stay unpowered: the TV doesn't move its head.

## The code

| File | What for |
|---|---|
| `firmware/src/main.cpp` | Channels, playback (`playStep()`), static between channels, on-screen display and the upload server |
| `channels.txt` | The channels `run.sh` keeps on the card |
| `laptop/tv.py` | Conversion with ffmpeg (`convert()`), upload (`upload()`) and the channel list (`load_channels()`) |

## Troubleshooting

| Symptom | What to do |
|---|---|
| "No microSD card" | The card isn't in, or it isn't FAT32 (cards of 64 GB or more come as exFAT). Format it as FAT32 and restart the robot. |
| `run.sh` can't find the robot | The robot's screen shows its IP when it joins the Wi-Fi: use `--robot <IP>`. Check that the laptop is on the same network. |
| The picture jumps | Very detailed scenes take longer to decode. Use `--quality 10` or `--fps 12`. The serial monitor prints the real fps every 5 s (`[TV] ... drawn 14.8 fps`). |
| You can't hear anything | Tap the right half of the top band to raise the volume. Some videos have no sound: `tv.py` warns about it when it converts them. |
| The sound is quiet or tinny | It's a small speaker: it can't play bass, so `tv.py` cuts it. Speech and music with voices sound best. |

## Ideas to extend

- **Remote control:** change the channel with the TV remote, like demo 06.
- **Ambilight:** light the base LEDs with the average colour of each frame (below `LED_MAX_LEVEL`).
- **Turn the head on:** when you change the channel, the robot nods as if you'd hit the TV.
- **TV guide:** the laptop uploads a programme and the robot changes the channel by itself at each hour.
