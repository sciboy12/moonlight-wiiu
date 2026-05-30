#pragma once

#include <stdint.h>

#define WIIU_MOONLIGHT_SD_PATH "/vol/external01/moonlight"
#define WIIU_STREAM_DIAG_LOG_PATH WIIU_MOONLIGHT_SD_PATH "/stream-diag.log"

void wiiu_stream_diag_reset(void);
void wiiu_stream_diag_dump(uint32_t currentFrame,
                           uint32_t nextFrame,
                           uint32_t queueReadIndex,
                           uint32_t queueWriteIndex,
                           uint32_t droppedFrames);
void wiiu_stream_diag_close(void);
void wiiu_stream_diag_log_mutex_wait(const char* mutexName, uint64_t waitTicks);
void wiiu_stream_diag_note_frame_enqueued(void);
void wiiu_stream_diag_note_frame_dequeued(void);
void wiiu_stream_diag_note_frame_empty(void);
void wiiu_stream_diag_note_frame_dropped(void);
void wiiu_stream_diag_log_queue_overflow(uint32_t droppedFrames);
