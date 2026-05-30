#pragma once

#include <stdint.h>

void wiiu_stream_diag_reset(void);
void wiiu_stream_diag_mark_decoded_frame(void);
void wiiu_stream_diag_mark_rendered_frame(void);
/* Marks receipt of a Limelight video decode unit, which is the earliest video receive callback exposed here. */
void wiiu_stream_diag_mark_video_packet(void);
void wiiu_stream_diag_mark_audio_packet(void);
void wiiu_stream_diag_mark_enet_send(void);
void wiiu_stream_diag_mark_enet_receive(void);
void wiiu_stream_diag_periodic(const char* reason);
void wiiu_stream_diag_dump(const char* reason);
void wiiu_stream_diag_log_mutex_wait(const char* name, uint32_t started_ms, uint32_t acquired_ms);
uint32_t wiiu_stream_diag_now_ms(void);
