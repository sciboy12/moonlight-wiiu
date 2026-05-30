#include "stream_diag.h"

#include <coreinit/time.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MUTEX_WAIT_LOG_THRESHOLD_MS 5

static FILE* diagLog;
static uint64_t streamStartTicks;
static uint64_t lastDumpTicks;
static uint64_t lastVideoRecvTicks;
static uint64_t lastAudioRecvTicks;
static uint64_t lastControlRecvTicks;
static uint64_t lastSocketSendTicks;
static uint64_t lastVideoPacketTicks;
static uint64_t lastAudioPacketTicks;
static uint64_t lastControlPacketTicks;
static uint64_t lastEnetSendTicks;
static uint64_t lastEnetReceiveTicks;
static uint64_t lastFrameDecodedTicks;
static uint64_t lastFrameEnqueuedTicks;
static uint64_t lastFrameRenderedTicks;
static uint32_t enqueuedFrames;
static uint32_t dequeuedFrames;
static uint32_t emptyFramePolls;
static uint32_t droppedFrameEvents;
static uint32_t mutexWaitEvents;
static uint32_t videoRecvCount;
static uint32_t audioRecvCount;
static uint32_t controlRecvCount;
static uint32_t socketSendCount;
static uint32_t videoPacketCount;
static uint32_t audioPacketCount;
static uint32_t controlPacketCount;
static uint32_t enetSendCount;
static uint32_t enetReceiveCount;
static uint32_t decodedFrameCount;
static uint32_t renderedFrameCount;
static uint32_t audioRecoveryFailures;
static uint32_t enetSendFailures;
static uint64_t maxMutexWaitTicks;

static void diag_printf(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);

  if (diagLog != NULL) {
    va_start(args, format);
    vfprintf(diagLog, format, args);
    va_end(args);
    fflush(diagLog);
  }
}

static void print_age(const char* label, uint64_t ticks)
{
  if (ticks == 0) {
    diag_printf("  %s: never\n", label);
    return;
  }

  uint64_t now = OSGetTime();
  uint64_t ageMs = OSTicksToMilliseconds(now - ticks);
  diag_printf("  %s: %llu ms ago\n", label, (unsigned long long)ageMs);
}

void wiiu_stream_diag_reset(void)
{
  if (diagLog == NULL) {
    diagLog = fopen(WIIU_STREAM_DIAG_LOG_PATH, "a");
    if (diagLog == NULL) {
      printf("Stream diagnostics: unable to open %s\n", WIIU_STREAM_DIAG_LOG_PATH);
    }
  }

  streamStartTicks = OSGetTime();
  lastDumpTicks = 0;
  lastVideoRecvTicks = 0;
  lastAudioRecvTicks = 0;
  lastControlRecvTicks = 0;
  lastSocketSendTicks = 0;
  lastVideoPacketTicks = 0;
  lastAudioPacketTicks = 0;
  lastControlPacketTicks = 0;
  lastEnetSendTicks = 0;
  lastEnetReceiveTicks = 0;
  lastFrameDecodedTicks = 0;
  lastFrameEnqueuedTicks = 0;
  lastFrameRenderedTicks = 0;
  enqueuedFrames = 0;
  dequeuedFrames = 0;
  emptyFramePolls = 0;
  droppedFrameEvents = 0;
  mutexWaitEvents = 0;
  videoRecvCount = 0;
  audioRecvCount = 0;
  controlRecvCount = 0;
  socketSendCount = 0;
  videoPacketCount = 0;
  audioPacketCount = 0;
  controlPacketCount = 0;
  enetSendCount = 0;
  enetReceiveCount = 0;
  decodedFrameCount = 0;
  renderedFrameCount = 0;
  audioRecoveryFailures = 0;
  enetSendFailures = 0;
  maxMutexWaitTicks = 0;

  diag_printf("Stream diagnostics reset; logging to %s\n", WIIU_STREAM_DIAG_LOG_PATH);
}

void wiiu_stream_diag_dump(uint32_t currentFrame,
                           uint32_t nextFrame,
                           uint32_t queueReadIndex,
                           uint32_t queueWriteIndex,
                           uint32_t droppedFrames)
{
  uint32_t queuedFrames = queueWriteIndex - queueReadIndex;

  diag_printf("Stream diagnostics dump:\n");
  print_age("stream started", streamStartTicks);
  if (lastDumpTicks != 0) {
    print_age("previous dump", lastDumpTicks);
  }
  print_age("last successful video recv", lastVideoRecvTicks);
  print_age("last successful audio recv", lastAudioRecvTicks);
  print_age("last successful control recv", lastControlRecvTicks);
  print_age("last successful socket send", lastSocketSendTicks);
  print_age("last video callback entry", lastVideoPacketTicks);
  print_age("last audio callback entry", lastAudioPacketTicks);
  print_age("last control callback entry", lastControlPacketTicks);
  print_age("last successful ENet send", lastEnetSendTicks);
  print_age("last successful ENet receive", lastEnetReceiveTicks);
  print_age("last frame decoded", lastFrameDecodedTicks);
  print_age("last frame enqueued", lastFrameEnqueuedTicks);
  print_age("last frame rendered", lastFrameRenderedTicks);
  diag_printf("  frames: current=%u next=%u backlog=%u decoded=%u rendered=%u\n",
              currentFrame, nextFrame, nextFrame - currentFrame, decodedFrameCount, renderedFrameCount);
  diag_printf("  queue: read=%u write=%u queued=%u\n",
              queueReadIndex, queueWriteIndex, queuedFrames);
  diag_printf("  totals: enqueued=%u dequeued=%u empty-polls=%u dropped-events=%u dropped-frames=%u\n",
              enqueuedFrames, dequeuedFrames, emptyFramePolls, droppedFrameEvents, droppedFrames);
  diag_printf("  recv: video=%u audio=%u control=%u socket-send=%u\n",
              videoRecvCount, audioRecvCount, controlRecvCount, socketSendCount);
  diag_printf("  callbacks: video=%u audio=%u control=%u enet-send=%u enet-recv=%u\n",
              videoPacketCount, audioPacketCount, controlPacketCount, enetSendCount, enetReceiveCount);
  diag_printf("  failures: audio-recovery=%u enet-send=%u\n",
              audioRecoveryFailures, enetSendFailures);
  diag_printf("  mutex waits: events=%u max=%llu ms\n",
              mutexWaitEvents, (unsigned long long)OSTicksToMilliseconds(maxMutexWaitTicks));

  lastDumpTicks = OSGetTime();
  if (diagLog != NULL) {
    fflush(diagLog);
  }
}

void wiiu_stream_diag_close(void)
{
  if (diagLog != NULL) {
    diag_printf("Stream diagnostics closing log\n");
    fclose(diagLog);
    diagLog = NULL;
  }
}

void wiiu_stream_diag_log_mutex_wait(const char* mutexName, uint64_t waitTicks)
{
  uint64_t waitMs = OSTicksToMilliseconds(waitTicks);

  if (waitTicks > maxMutexWaitTicks) {
    maxMutexWaitTicks = waitTicks;
  }

  if (waitMs >= MUTEX_WAIT_LOG_THRESHOLD_MS) {
    mutexWaitEvents++;
    diag_printf("Stream diagnostics: waited %llu ms for %s mutex\n", (unsigned long long)waitMs, mutexName);
    if (diagLog != NULL) {
      fflush(diagLog);
    }
  }
}

void wiiu_stream_diag_log_queue_overflow(uint32_t droppedFrames)
{
  diag_printf("Video frame queue overflow (%u drops). Dropping old frames to keep stream responsive.\n", droppedFrames);
}

void wiiu_stream_diag_note_frame_enqueued(void)
{
  lastFrameEnqueuedTicks = OSGetTime();
  enqueuedFrames++;
}

void wiiu_stream_diag_note_frame_dequeued(void)
{
  dequeuedFrames++;
}

void wiiu_stream_diag_note_frame_empty(void)
{
  emptyFramePolls++;
}

void wiiu_stream_diag_note_frame_dropped(void)
{
  droppedFrameEvents++;
}

void wiiu_stream_diag_note_video_recv(void)
{
  lastVideoRecvTicks = OSGetTime();
  videoRecvCount++;
}

void wiiu_stream_diag_note_audio_recv(void)
{
  lastAudioRecvTicks = OSGetTime();
  audioRecvCount++;
}

void wiiu_stream_diag_note_control_recv(void)
{
  lastControlRecvTicks = OSGetTime();
  controlRecvCount++;
}

void wiiu_stream_diag_note_socket_send(void)
{
  lastSocketSendTicks = OSGetTime();
  socketSendCount++;
}

void wiiu_stream_diag_note_video_packet(void)
{
  lastVideoPacketTicks = OSGetTime();
  videoPacketCount++;
}

void wiiu_stream_diag_note_audio_packet(void)
{
  lastAudioPacketTicks = OSGetTime();
  audioPacketCount++;
}

void wiiu_stream_diag_note_control_packet(void)
{
  lastControlPacketTicks = OSGetTime();
  controlPacketCount++;
}

void wiiu_stream_diag_note_enet_send(void)
{
  lastEnetSendTicks = OSGetTime();
  enetSendCount++;
}

void wiiu_stream_diag_note_enet_receive(void)
{
  lastEnetReceiveTicks = OSGetTime();
  enetReceiveCount++;
}

void wiiu_stream_diag_note_frame_decoded(void)
{
  lastFrameDecodedTicks = OSGetTime();
  decodedFrameCount++;
}

void wiiu_stream_diag_note_frame_rendered(void)
{
  lastFrameRenderedTicks = OSGetTime();
  renderedFrameCount++;
}

void wiiu_stream_diag_note_connection_log(const char* format)
{
  if (strstr(format, "Unable to recover audio data block") != NULL) {
    audioRecoveryFailures++;
  }

  if (strstr(format, "Failed to send ENet control packet") != NULL) {
    enetSendFailures++;
  }
}
