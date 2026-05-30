#include "stream_diag.h"

#include <coreinit/time.h>

#include <stdarg.h>
#include <stdio.h>

#define MUTEX_WAIT_LOG_THRESHOLD_MS 5

static FILE* diagLog;
static uint64_t streamStartTicks;
static uint64_t lastDumpTicks;
static uint32_t enqueuedFrames;
static uint32_t dequeuedFrames;
static uint32_t emptyFramePolls;
static uint32_t droppedFrameEvents;
static uint32_t mutexWaitEvents;
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
  uint64_t now = OSGetTime();
  uint64_t ageMs = ticks == 0 ? 0 : OSTicksToMilliseconds(now - ticks);
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
  enqueuedFrames = 0;
  dequeuedFrames = 0;
  emptyFramePolls = 0;
  droppedFrameEvents = 0;
  mutexWaitEvents = 0;
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
  diag_printf("  frames: current=%u next=%u backlog=%u\n",
              currentFrame, nextFrame, nextFrame - currentFrame);
  diag_printf("  queue: read=%u write=%u queued=%u\n",
              queueReadIndex, queueWriteIndex, queuedFrames);
  diag_printf("  totals: enqueued=%u dequeued=%u empty-polls=%u dropped-events=%u dropped-frames=%u\n",
              enqueuedFrames, dequeuedFrames, emptyFramePolls, droppedFrameEvents, droppedFrames);
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
