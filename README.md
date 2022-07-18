# Very Slow Movie Player - For Inkplate

[Very Slow Movie Player](https://medium.com/s/story/very-slow-movie-player-499f76c48b62) for the [Inkplate](https://inkplate.io) ESP32 based e-ink platform.

Movies are displayed at 24 frames an hour rather than 24 frames a second.
There are no controls to pause, fast-forward, or rewind.  If the player loses power,
the movie will continue from where it would have been had power not been lost.

Slowing the movie down to an almost imperceptible pace changes our
relationship with the material being presented, as does the realization that
the piece progresses at its own pace, and own time, regardless of our actions or
desires.

## Background

I wanted to build an art project for my partner's birthday, and was looking
for something that would let me have some fun hacking on a technical project.
My partner is an art director, with a background in animation, so I started
considering visual oriented projects.

I came across Bryan Boyer's 2018 essay on
[Creating a Very Slow Movie Player](https://medium.com/s/story/very-slow-movie-player-499f76c48b62)
where he builds a movie player that plays images at 24 frames an hour rather
than 24 frames a second. I also found
[Tom Whitwell's Slow Movie](https://github.com/TomWhitwell/SlowMovie)
implementation using a Waveshare e-paper display designed for the Raspberry Pi.

I was intrigued by idea of a VSMP (very slow movie player) and thought that it
would make a fantastic gift for my partner to hang in her art studio, and so, I
set out to build one of my own.

I decided on the following artistic and technical design points:
* Run without being connected to power, ideally for days or weeks.
* Completely self contained. Load the movie from the SD card and do not make any
  network calls to external servers.
* Continue to progress through the movie, even when turned off.
* No pause, rewind, or fast forward controls.

## Hardware Design

Both of the above VSMPs are designed to be plugged in to a power source. Since I
wanted to create a VSMP that could run from battery power for an extended period
of time, I needed a lower power platform as the underlying hardware
implementation. This meant either designing around a
[Raspberry Pi Zero](https://www.raspberrypi.com/products/raspberry-pi-zero/)
or an
[ESP32](https://www.espressif.com/en/products/socs/esp32).

In my research for looking into how to put the ESP32 or Raspberry Pi into a low
power state between the display of frames, I came across the
[Inkplate](https://inkplate.io), which met all of my hardware needs. The
Inkplate is already pre-wired to an e-paper display, supports a deep sleep state
where it only consumes 25uA, has an onboard real time clock that can be used as
alarm to wake the system from deep sleep, and has an onboard battery charger. It
is the perfect hardware platform for this project.

In addition to the [Inkplate 10](https://www.crowdsupply.com/soldered/inkplate-10)
I purchased and installed:
* [CR2032 Lithium Coin Cell Battery](https://www.adafruit.com/product/654) for
  the real time clock
* [Lithium Ion Polymer Battery - 3.7v 1200mAh](https://www.adafruit.com/product/258)
for the main battery. Larger capacity batteries are available, but if you use
the provided Inkplate frame, you need to keep the thickness under 5mm.

## Software Design

### Movie Player and Video Encoding

I researched existing video players for the esp32 and found a writeup on
[Creating a Videoplayer for ESP32](https://www.appelsiini.net/2020/esp32-mjpeg-video-player/).
The auther tests two techniques: 1) RGB565 - a format where each frame is stored
consecutively and uncompressed.  2) Motion JPEG where each frame is stored
consecutively but with JPEG compression.

I ruled both of these approaches out, as RGB565 would take far too much storage,
and Motion JPEG is missing any for of index of frame locations needed to
seek to a specific frame, as is needed to continue after a power loss.
Tracking the player's position within the file and storing it to the sd card
on a periodic basis would have been possible, but I was concerned about writing
to the same file over could lead to degrading the flash.  was not sure what
wear leveling strategies the FAT drivers for the ESP32 platform employ, if any.

This led to the decision to implement the simplest format I could
think of that would a) provide some level of compression for the video stream,
b) support efficient seeking to a frame or time offset.  The simplest
is no format at all!

Each movie is stored in a separate directory, with each frame of that
movie as an individual jpg file.

The basename of the frame file is a ten digit, zero padded, number of the
frame within the movie. For example, frame 50036 would be stored in file
0000050036.jpg.

The files are stored in a directory structure within the movie's top level
directory where the first two bytes of the name identify the name of the
top parent directory, the second two bytes identify the second top most
parent directory, etc.  For example, 0000050036.jpg is stored
in moviename/00/00/05/00/0000050036.jpg.

I suppose this makes this more of a fancy picture viewer rather than
a movie player, but then, that's true of all movie players.

## Setup

**Batteries:** You can run without the main battery, but the CR2032 for the RTC
is needed to track the position in the movie if the Inkplate loses power.
Otherwise, the movie will restart on every power cycle.

**Dev Setup:** Follow the [Getting Started section of the Inkplate
docs](https://inkplate.readthedocs.io/en/latest/get-started.html). Run a few
example programs to make sure your development environment is working.

**Setting the clock:**  If the RTC is not set, the VSMP will set the time
arbitrarily to the number of seconds since boot resulting in the RTC being set
to Jan 1,1970.  If you care about setting the time correctly, run the [NTP time
to RTC for
Inkplate10](https://forum.e-radionica.com/en/viewtopic.php?f=22&t=424) sketch
from the Inkplate forums.

**Factory settings:** Some of the Inkplate 10s shipped at the time I wrote the
VSMP need their factory settings adjusted.  If your greyscale seems off, see the
[Variable image quality on different Inkplate10s
thread](https://forum.e-radionica.com/en/viewtopic.php?f=23&t=407&p=801&hilit=waveform#p801)
in the Inkplate forum.

## Encoding the movie

ffmpeg can be used to encode the movie:

```
$ mkdir movie
$ ffmpeg -i movie.mp4 -pix_fmt yuv420p -q:v 1 -vf scale="1200:-1" -an movie/%10d.jpg
```

This will result in a directory with hundreds of thousands of files.  Use the included
mvtree.py python script to move the frames into the expected directory structure.

```
$ cd movie
$ ~/path/to/repo/mvtree.py
```

## Configuring the player

You can configure the player by changing the constants at the top of
vsmp-inpkplate.ino and reflashing.  You can set the movie name,
the number of frames to advance on update, the starting frame and the
amount of time to sleep before advancing to the next frame.  These settings
can also be set via a vsmp.jsn file in the root directory of the SD card, e.g.
```
{
    "movie_name":"the_fall",
    "frame_advance": 4,
    "start_frame": 355,
    "sec_between_frames": 300
}
```

The above vsmp.jsn file will play the movie in directory `the_fall`,
starting at frame 355, sleep 300 seconds between updates, and advance 4 frames
on every update.

The player will save a `vsmps.jsn` state that it uses to resume playing
in the event of no battery and power loss.  Delete this file from the SD card
to restart from the beginning, or relfash with the `IGNORE_STATE` constant in the
vsmp-inkplate.ino set to 1.  Note: you likely want to relfash with `IGNORE_STATE`
back to zero if you take the reflashing route.  The state will also be reinitialized
if you switch movies, either using the vsmp.jsn config file, or directly in code.
