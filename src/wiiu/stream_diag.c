#include "stream_diag.h"

#include <coreinit/time.h>
#include <stdio.h>

#define PERIODIC_LOG_MS 1000
#define LONG_MUTEX_WAIT_MS 10

static uint32_t stream_start_ms;
static uint32_t last_diag_log_ms;
static uint32_t last_decoded_frame_ms;
static uint32_t last_rendered_frame_ms;
static uint32_t last_video_packet_ms;
static uint32_t last_audio_packet_ms;
static uint32_t last_enet_send_ms;
static uint32_t last_enet_receive_ms;
static uint32_t decoded_frame_count;
static uint32_t rendered_frame_count;
static uint32_t video_packet_count;
static uint32_t audio_packet_count;
static uint32_t enet_send_count;
static uint32_t enet_receive_count;

uint32_t wiiu_stream_diag_now_ms(void) {
  return (uint32_t)OSTicksToMilliseconds(OSGetTime());
}

static uint32_t age_ms(uint32_t now_ms, uint32_t timestamp_ms) {
  if (timestamp_ms == 0) {
    return UINT32_MAX;
  }

  return now_ms - timestamp_ms;
}

static void print_age(const char* label, uint32_t now_ms, uint32_t timestamp_ms, uint32_t count) {
  uint32_t age = age_ms(now_ms, timestamp_ms);

  if (age == UINT32_MAX) {
    printf(" %s=never(count=%u)", label, count);
  } else {
    printf(" %s=%ums_ago@+%ums(count=%u)", label, age, timestamp_ms - stream_start_ms, count);
  }
}

void wiiu_stream_diag_reset(void) {
  uint32_t now_ms = wiiu_stream_diag_now_ms();

  stream_start_ms = now_ms;
  last_diag_log_ms = 0;
  last_decoded_frame_ms = 0;
  last_rendered_frame_ms = 0;
  last_video_packet_ms = 0;
  last_audio_packet_ms = 0;
  last_enet_send_ms = 0;
  last_enet_receive_ms = 0;
  decoded_frame_count = 0;
  rendered_frame_count = 0;
  video_packet_count = 0;
  audio_packet_count = 0;
  enet_send_count = 0;
  enet_receive_count = 0;

  printf("[stream-diag] reset at %ums\n", now_ms);
}

void wiiu_stream_diag_mark_decoded_frame(void) {
  last_decoded_frame_ms = wiiu_stream_diag_now_ms();
  decoded_frame_count++;
}

void wiiu_stream_diag_mark_rendered_frame(void) {
  last_rendered_frame_ms = wiiu_stream_diag_now_ms();
  rendered_frame_count++;
}

void wiiu_stream_diag_mark_video_packet(void) {
  last_video_packet_ms = wiiu_stream_diag_now_ms();
  video_packet_count++;
}

void wiiu_stream_diag_mark_audio_packet(void) {
  last_audio_packet_ms = wiiu_stream_diag_now_ms();
  audio_packet_count++;
}

void wiiu_stream_diag_mark_enet_send(void) {
  last_enet_send_ms = wiiu_stream_diag_now_ms();
  enet_send_count++;
}

void wiiu_stream_diag_mark_enet_receive(void) {
  last_enet_receive_ms = wiiu_stream_diag_now_ms();
  enet_receive_count++;
}

void wiiu_stream_diag_dump(const char* reason) {
  uint32_t now_ms = wiiu_stream_diag_now_ms();

  printf("[stream-diag] %s now=+%ums", reason, now_ms - stream_start_ms);
  print_age("video-packet", now_ms, last_video_packet_ms, video_packet_count);
  print_age("decoded-frame", now_ms, last_decoded_frame_ms, decoded_frame_count);
  print_age("rendered-frame", now_ms, last_rendered_frame_ms, rendered_frame_count);
  print_age("audio-packet", now_ms, last_audio_packet_ms, audio_packet_count);
  print_age("enet-send", now_ms, last_enet_send_ms, enet_send_count);
  print_age("enet-recv", now_ms, last_enet_receive_ms, enet_receive_count);
  printf("\n");
}

void wiiu_stream_diag_periodic(const char* reason) {
  uint32_t now_ms = wiiu_stream_diag_now_ms();
  uint32_t log_due = last_diag_log_ms == 0 || now_ms - last_diag_log_ms >= PERIODIC_LOG_MS;

  if (log_due) {
    last_diag_log_ms = now_ms;
    wiiu_stream_diag_dump(reason);
  }
}

void wiiu_stream_diag_log_mutex_wait(const char* name, uint32_t started_ms, uint32_t acquired_ms) {
  uint32_t waited_ms = acquired_ms - started_ms;

  if (waited_ms >= LONG_MUTEX_WAIT_MS) {
    printf("[stream-diag] long mutex wait: %s waited %ums\n", name, waited_ms);
  }
}
