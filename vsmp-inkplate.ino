#ifndef ARDUINO_INKPLATE10
#error "designed for Inkplate10"
#endif

#include <ArduinoJson.h>
#include <Inkplate.h>
#include <SdFat.h>

#include "trace.h"

#define IGNORE_STATE 0

struct Config {
  char movie_name[64];
  uint frame_advance;
  uint start_frame;
  uint usec_between_frames;
};

RTC_DATA_ATTR struct Config g_config;

// Use arduinojson.org/v6/assistant to compute the capacity.
#define CONFIG_JSON_DOC_CAPACITY 256
#define CONFIG_FILENAME "vsmp.jsn"

#define CONFIG_DEFAULT_MOVIE_NAME "movie"
#define CONFIG_DEFAULT_FRAME_ADVANCE 4
#define CONFIG_DEFAULT_START_FRAME 1
#define CONFIG_DEFAULT_SEC_BETWEEN_FRAMES 300

struct State {
  char movie_name[64];
  uint64_t start_time;
};

// Use arduinojson.org/v6/assistant to compute the capacity.
#define STATE_JSON_DOC_CAPACITY 256
#define STATE_FILENAME "vsmps.jsn"

// thread on battery voltages
// https://forum.e-radionica.com/en/viewtopic.php?f=23&t=344
#define BATTERY_VOLTAGE_HIGH 4.5
#define BATTERY_VOLTAGE_LOW 3.2
#define BATTERY_VOLTAGE_WARNING_SLEEP 3.55

// Image "colors" (3bit mode)
#define C_BLACK 0
#define C_WHITE 7

RTC_DATA_ATTR uint g_sleep_boot = 0;
RTC_DATA_ATTR uint g_frame;
RTC_DATA_ATTR uint64_t g_last_frame_time;
RTC_DATA_ATTR uint64_t g_last_sleep_duration;
RTC_DATA_ATTR uint64_t g_start_time;

Inkplate display(INKPLATE_3BIT);

void fatal_error(char* message) {
  Serial.print("Error: ");
  Serial.println(message);

  // completely reset the display
  display.selectDisplayMode(INKPLATE_1BIT);
  display.clearDisplay();
  display.setCursor(10, 10);
  display.setTextColor(BLACK, WHITE);
  display.setTextSize(4);

  // display the rror
  display.print("Error: ");
  display.println(message);
  display.display();

  // and sleep forever
  esp_deep_sleep_start();
}

void parse_json_file(JsonDocument& doc, char* filename) {
  SdFile file;

  // it's ok if the open errors out, the following parse will fail and doc
  // will be empty and we will still use the defaults
  file.open(filename, O_RDONLY);

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    TRACE("failed to read %s, using defaults", filename);
  }

  // is this necessary, or does SdFile follow the RAII model?
  file.close();
}

void load_config(Config* config) {
  StaticJsonDocument<CONFIG_JSON_DOC_CAPACITY> doc;
  parse_json_file(doc, CONFIG_FILENAME);

  strlcpy(config->movie_name, doc["movie_name"] | CONFIG_DEFAULT_MOVIE_NAME,
          sizeof(config->movie_name));
  config->frame_advance = doc["frame_advance"] | CONFIG_DEFAULT_FRAME_ADVANCE;
  config->start_frame = doc["start_frame"] | CONFIG_DEFAULT_START_FRAME;
  config->usec_between_frames =
      (doc["sec_between_frames"] | CONFIG_DEFAULT_SEC_BETWEEN_FRAMES) * 1000000;

  TRACE("config movie: %s start_frame: %d frame_advance: %d",
        config->movie_name, config->start_frame, config->frame_advance);
}

void load_state(State* state) {
  StaticJsonDocument<STATE_JSON_DOC_CAPACITY> doc;

  if (!IGNORE_STATE) {
    parse_json_file(doc, STATE_FILENAME);
  }

  strlcpy(state->movie_name, doc["movie_name"] | CONFIG_DEFAULT_MOVIE_NAME,
          sizeof(state->movie_name));
  state->start_time = doc["start_time"];

  TRACE("state movie_name: %s start_time: %lld", state->movie_name,
        state->start_time);
}

void save_state(State* state) {
  SdFile file;

  if (!file.open(STATE_FILENAME, O_CREAT | O_WRONLY | O_TRUNC)) {
    fatal_error("could not create state file");
  }

  StaticJsonDocument<STATE_JSON_DOC_CAPACITY> doc;
  doc["movie_name"] = state->movie_name;
  doc["start_time"] = state->start_time;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    fatal_error("could not serialize state");
  }

  // is this necessary, or does SdFile follow the RAII model?
  file.close();
}

//
// Get the filename of the next frame.
//
// Each movie is stored in a separate directory, with each frame of that
// movie as an individual jpg file.
//
// The basename of the frame file is a ten digit, zero padded, number of the
// frame within the movie. For example, frame 50036 would be stored in file
// 0000050036.jpg.
//
// The files are stored in a directory structure within the movie's top level
// directory where the first two bytes of the name identify the name of the
// top parent directory, the second two bytes identify the second top most
// parent directory, etc.  For example, 0000050036.jpg is stored
// in moviename/00/00/05/00/0000050036.jpg.
//
// This avoids having hundreds of thousands of frame files in
// a single directory.
void get_frame_filename(char* buf, uint len, char* movie_name, uint frame) {
  char base_buf[sizeof("0123456789.jpg")];

  snprintf(base_buf, sizeof(base_buf), "%010d.jpg", frame);
  snprintf(buf, len, "%s/%.2s/%.2s/%.2s/%.2s/%s", movie_name, base_buf,
           base_buf + 2, base_buf + 4, base_buf + 6, base_buf);
}

//
// Display the next rame of the movie.
//
bool display_frame(char* movie_name, uint frame) {
  char fname[256];

  get_frame_filename(fname, sizeof(fname), movie_name, frame);
  TRACE("fname: %s", fname);

  display.clearDisplay();
  if (!display.drawImage(fname, Image::Format::JPG, Image::Position::Center)) {
    TRACE("drawImage failed");
    SdFat sd_fat = display.getSdFat();
    if (!sd_fat.exists(fname)) {
      return false;
    }
    fatal_error("could not draw image");
  }

  return true;
}

//
// get microseconds elapsed
//
uint64_t microseconds() {
  struct timeval tv;
  gettimeofday(&tv, NULL);

  return (int64_t)tv.tv_usec + tv.tv_sec * 1000000ll;
}

// At the time this was written, an on my board, there is about 6 seconds
// of overhead for sleep/wake, reading the image, jpg decoding, and drawing.
// Tracking the display time of the last frame like this, and then using
// it to calibrate the time to sleep, enables us to hit the desired frame
// rate.  It will adapt if more logic is added and/or if the performance
// of the device changes for whatever reason.
void deep_sleep(uint usec_between_frames) {
  uint64_t overhead;
  uint64_t sleep_duration;

  // calc time to sleep, adjusting for overhead of processing/display
  if (!g_sleep_boot) {
    overhead = 0;
    sleep_duration = usec_between_frames;
  } else {
    overhead = microseconds() - g_last_frame_time - g_last_sleep_duration;
    sleep_duration = usec_between_frames - overhead;
  }

  g_sleep_boot = 1;
  g_last_frame_time = microseconds();
  g_last_sleep_duration = sleep_duration;

  TRACE("overhead: %lld sleep_duration: %lld", overhead, sleep_duration);

  esp_sleep_enable_timer_wakeup(sleep_duration);
  esp_deep_sleep_start();
}

uint get_battery_percentage(double voltage) {
  uint percentage = ((voltage - BATTERY_VOLTAGE_LOW) * 100.0) /
                    (BATTERY_VOLTAGE_HIGH - BATTERY_VOLTAGE_LOW);

  if (percentage > 100) {
    percentage = 100;
  }

  if (percentage < 0) {
    percentage = 0;
  }

  return percentage;
}

void display_stats() {
  double battery_voltage = display.readBattery();
  uint battery_percent = get_battery_percentage(battery_voltage);

  display.setTextColor(C_BLACK, C_WHITE);
  display.setCursor(1175, 815);
  display.setTextSize(1);

  TRACE("voltage: %.2f percent: %d", battery_voltage, battery_percent);

  display.printf("%d%%", battery_percent);
}

void low_battery_check() {
  double voltage = display.readBattery();

  // if voltage was < 1, it was a bad reading.
  if (voltage > 1 && voltage <= BATTERY_VOLTAGE_WARNING_SLEEP) {
    // TODO: a nicer error message
    fatal_error("low battery");
  }
}

uint calc_frame_from_start_time(Config* config, uint64_t start_time) {
  uint64_t now = microseconds();
  uint64_t elapsed;
  uint64_t frames;

  TRACE("now: %lld start_time: %lld", now, start_time);

  // all kinds of wonkiness can happen if the clock was not set
  // right in the past
  if (now < start_time) {
    return 0;
  }

  elapsed = now - start_time;
  frames = elapsed / config->usec_between_frames;
  if (frames > UINT_MAX) {
    return 0;
  }

  frames = frames * config->frame_advance + config->start_frame;
  return frames;
}

void start_at_beginning() {
  g_frame = g_config.start_frame;
  g_start_time = microseconds();

  // save the state file
  State state;
  state.start_time = g_start_time;
  strlcpy(state.movie_name, g_config.movie_name, sizeof(g_config.movie_name));

  TRACE("saving initial state: %s %lld", state.movie_name, state.start_time);
  save_state(&state);
}

void setup_from_poweron() {
  // start by loading a config file, if it exists
  load_config(&g_config);
  g_frame = g_config.start_frame;

  // it might be the case that we need to recover state from a full power
  // loss. Check if a state file exists, and if so, figure out which frame
  // we should be on.
  State state;
  load_state(&state);
  if (state.start_time && !strcmp(state.movie_name, g_config.movie_name)) {
    uint frame = calc_frame_from_start_time(&g_config, state.start_time);
    if (frame) {
      TRACE("using computed frame: %d", frame);
      g_frame = frame;
    }
    g_start_time = state.start_time;
  } else {
    start_at_beginning();
  }
}

void set_time_from_rtc() {
  uint32_t rtc_epoch;
  struct timeval tv;
  tv.tv_sec = rtc_epoch;

  // For now, just set epoc to 1 because really doesn't matter,
  // but this could init from NTP.
  if (!display.rtcIsSet()) {
    TRACE("rtc clock not set");
    rtc_epoch = 1;
  } else {
    rtc_epoch = display.rtcGetEpoch();
  }

  tv.tv_sec = rtc_epoch;
  settimeofday(&tv, NULL);
  TRACE("now: %lld", microseconds());
}

void setup_inkplate() {
  Serial.begin(115200);
  display.begin();

  // setup display
  display.selectDisplayMode(INKPLATE_3BIT);
  display.clearDisplay();

  // set time
  set_time_from_rtc();

  // setup sd card
  TRACE("sd card init");
  if (!display.sdCardInit()) {
    fatal_error("SD Card Error");
    return;
  }
  TRACE("sd card ok");
}

//
// The main loop
//
void setup() {
  // initialize inkplate hardware
  setup_inkplate();

  TRACE("rtcIsSet: %d now: %lld g_sleep_boot: %d", display.rtcIsSet(),
        microseconds(), g_sleep_boot);

  // load config and state settings into rtc storage
  if (!g_sleep_boot) {
    setup_from_poweron();
  }

  TRACE("start_frame: %d g_frame: %d computed: %d", g_config.start_frame,
        g_frame, calc_frame_from_start_time(&g_config, g_start_time));

  // display the next frame, will return false if we ran past the
  // last frame. errors with jpeg decoding would have resulted
  // in a fatal error during display.
  if (!display_frame(g_config.movie_name, g_frame)) {
    // ensure that the first frame exists and that we don't enter
    // a boot loop
    if (g_frame == g_config.start_frame) {
      fatal_error("frame not found");
    } else {
      TRACE("restarting movie");
      start_at_beginning();
    }
  }

  display_stats();
  display.display();

  // advance to next frame
  g_frame += g_config.frame_advance;

  low_battery_check();
  deep_sleep(g_config.usec_between_frames);
}

void loop() {
  // should never get here
  fatal_error("entered loop function unexpectedly");
}
