/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2019 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "connection.h"
#include "config.h"

#include <Limelight.h>

#include <SDL2/SDL.h>

#include <client.h>
#include <errors.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "wiiu/wiiu.h"
#include <whb/gfx.h>
#include <vpad/input.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#ifdef DEBUG
void Debug_Init();
#endif

#define SCREEN_BAR "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501" \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501" \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\n"

int state = STATE_INVALID;

int is_error = 0;
char message_buffer[1024] = "\0";

int autostream = 0;

static int get_app_id(GS_CLIENT client, PSERVER_DATA server, const char *name) {
  PAPP_LIST list = NULL;
  if (gs_applist(client, server, &list) != GS_OK) {
    fprintf(stderr, "Can't get app list\n");
    return -1;
  }

  while (list != NULL) {
    if (strcmp(list->name, name) == 0)
      return list->id;

    list = list->next;
  }
  return -1;
}

static int stream(GS_CLIENT client, PSERVER_DATA server, PCONFIGURATION config) {
  int appId = get_app_id(client, server, config->app);
  if (appId<0) {
    fprintf(stderr, "Can't find app %s\n", config->app);
    sprintf(message_buffer, "Can't find app %s\n", config->app);
    is_error = 1;
    return -1;
  }

  int gamepads = wiiu_input_num_controllers();
  int gamepad_mask = 0;
  for (int i = 0; i < gamepads; i++)
    gamepad_mask = (gamepad_mask << 1) + 1;

  int ret = gs_start_app(client, server, &config->stream, appId, server->isGfe, config->sops, config->localaudio, gamepad_mask);
  if (ret < 0) {
    if (ret == GS_NOT_SUPPORTED_4K)
      fprintf(stderr, "Server doesn't support 4K\n");
    else if (ret == GS_NOT_SUPPORTED_MODE)
      fprintf(stderr, "Server doesn't support %dx%d (%d fps) or remove --nounsupported option\n", config->stream.width, config->stream.height, config->stream.fps);
    else if (ret == GS_NOT_SUPPORTED_SOPS_RESOLUTION)
      fprintf(stderr, "Optimal Playable Settings isn't supported for the resolution %dx%d, use supported resolution or add --nosops option\n", config->stream.width, config->stream.height);
    else if (ret == GS_ERROR)
      fprintf(stderr, "Gamestream error: %s\n", gs_get_error_message());
    else
      fprintf(stderr, "Errorcode starting app: %d\n", ret);
      
    sprintf(message_buffer, "Errorcode starting app: %d\n", ret);
    is_error = 1;
    return -1;
  }


  if (config->debug_level > 0) {
    printf("Stream %d x %d, %d fps, %d kbps\n", config->stream.width, config->stream.height, config->stream.fps, config->stream.bitrate);
  }

  if (LiStartConnection(&server->serverInfo, &config->stream, &connection_callbacks, &decoder_callbacks_wiiu, &audio_callbacks_wiiu, NULL, 0, config->audio_device, 0) != 0) {
    fprintf(stderr, "Failed to start connection\n");
    sprintf(message_buffer, "Failed to start connection\n");
    is_error = 1;
    return -1;
  }

  return 0;
}



typedef struct {
  volatile int active;
  volatile int stopRequested;
  volatile int networkRunning;
  volatile int decodeRunning;
  volatile int renderRunning;
  volatile int networkConnected;
  uint64_t networkTick;
  uint64_t decodeTick;
  uint64_t renderTick;
  OSThread networkThread;
  OSThread decodeThread;
  OSThread renderThread;
} STREAM_WORKERS;

static STREAM_WORKERS workers;

static uint64_t now_ms(void) {
  return OSTicksToMilliseconds(OSGetTime());
}

static void worker_deallocator(OSThread* thread, void* stack) { free(stack); }

static int network_worker_proc(int argc, const char** argv) {
  workers.networkRunning = 1;
  workers.networkConnected = 1;
  workers.networkTick = now_ms();
  while (!workers.stopRequested) {
    // Limelight handles control/network internally after LiStartConnection().
    // We keep this worker as lifecycle owner and watchdog heartbeat source.
    workers.networkTick = now_ms();
    OSSleepTicks(OSMillisecondsToTicks(1));
  }
  workers.networkConnected = 0;
  workers.networkRunning = 0;
  return 0;
}

static int decode_worker_proc(int argc, const char** argv) {
  uint32_t lastNextFrame = nextFrame;
  workers.decodeRunning = 1;
  workers.decodeTick = now_ms();
  while (!workers.stopRequested) {
    if (nextFrame != lastNextFrame) {
      lastNextFrame = nextFrame;
      workers.decodeTick = now_ms();
    }
    OSSleepTicks(OSMillisecondsToTicks(1));
  }
  workers.decodeRunning = 0;
  return 0;
}

static int render_worker_proc(int argc, const char** argv) {
  workers.renderRunning = 1;
  workers.renderTick = now_ms();
  while (!workers.stopRequested) {
    if (wiiu_stream_draw()) {
      workers.renderTick = now_ms();
    } else {
      OSSleepTicks(OSMillisecondsToTicks(1));
    }
  }
  workers.renderRunning = 0;
  return 0;
}

static int start_worker(OSThread* thread, const char* name, int (*entry)(int, const char**), int priority) {
  const int stack_size = 2 * 1024 * 1024;
  uint8_t* stack = (uint8_t*)memalign(16, stack_size);
  if (!stack) return -1;
  if (!OSCreateThread(thread, entry, 0, NULL, stack + stack_size, stack_size, priority, OS_THREAD_ATTRIB_AFFINITY_ANY)) {
    free(stack);
    return -1;
  }
  OSSetThreadName(thread, name);
  OSSetThreadDeallocator(thread, worker_deallocator);
  OSResumeThread(thread);
  return 0;
}

static int start_stream_workers(void) {
  memset(&workers, 0, sizeof(workers));
  workers.active = 1;
  if (start_worker(&workers.networkThread, "StreamNetwork", network_worker_proc, 0x12) != 0) return -1;
  if (start_worker(&workers.decodeThread, "StreamDecode", decode_worker_proc, 0x11) != 0) return -1;
  if (start_worker(&workers.renderThread, "StreamRender", render_worker_proc, 0x10) != 0) return -1;
  return 0;
}

static void stop_stream_workers(void) {
  workers.stopRequested = 1;
  if (workers.renderRunning) OSJoinThread(&workers.renderThread, NULL);
  if (workers.decodeRunning) OSJoinThread(&workers.decodeThread, NULL);
  if (workers.networkRunning) OSJoinThread(&workers.networkThread, NULL);
  workers.active = 0;
}

int main(int argc, char* argv[]) {
  wiiu_proc_init();

#ifdef DEBUG
  Debug_Init();
  printf("Moonlight Wii U started\n");
#endif

  WHBGfxInit();
  wiiu_setup_renderstate();

  SDL_InitSubSystem(SDL_INIT_AUDIO);

  wiiu_net_init();

  wiiu_input_init();

  Font_Init();

  Font_SetSize(50);
  Font_SetColor(255, 255, 255, 255);
  Font_Print(8, 58, "Reading configuration...");
  Font_Draw_TVDRC();

  CONFIGURATION config;
  config_parse(argc, argv, &config);

  // TODO
  config.unsupported = true;
  config.sops = false;

  if (config.address == NULL) {
    fprintf(stderr, "Specify an IP address\n");
    Font_Clear();
    Font_Print(8, 58, "Specify an IP address in the configuration file.\nMake sure to remove the '#' in front of the 'address' line.");
    state = STATE_INVALID;
  }
  else {
    char host_config_file[PATH_MAX];
    snprintf(host_config_file, PATH_MAX, "/vol/external01/moonlight/hosts/%s.conf", config.address);
    if (access(host_config_file, R_OK) != -1)
      config_file_parse(host_config_file, &config);
    
    // automatically connect on first launch
    state = STATE_CONNECTING;
  }

  wiiu_stream_init(config.stream.width, config.stream.height);

  GS_CLIENT client = gs_new(config.key_dir);
  if (client == NULL && gs_get_error(NULL) == GS_BAD_CONF) {
    if (gs_conf_init(config.key_dir) != GS_OK) {
      fprintf(stderr, "Failed to create client info: %s\n", gs_get_error_message());
      Font_Clear();
      Font_Printf(8, 58, "Failed to create client info:\n %s.", gs_get_error_message());
      state = STATE_INVALID;
    } else {
      client = gs_new(config.key_dir);
    }
  }

  if (client == NULL) {
    fprintf(stderr, "Failed to create GameStream client: %s\n", gs_get_error_message());
    Font_Clear();
    Font_Printf(8, 58, "Failed to create GameStream client:\n %s.", gs_get_error_message());
    state = STATE_INVALID;
  }

  SERVER_DATA server;
  while (wiiu_proc_running()) {
    switch (state) {
      case STATE_INVALID: {
        Font_Draw_TVDRC();
        break;
      }
      case STATE_DISCONNECTED: {
        Font_Clear();
        Font_SetSize(50);
        Font_SetColor(255, 255, 255, 255);

        Font_Printf(8, 58, "Moonlight Wii U %s (Disconnected)\n"
                           SCREEN_BAR
                           "Press \ue000 to connect to %s", VERSION_STRING, config.address);

        if (is_error) {
          Font_SetColor(255, 0, 0, 255);
        }
        else {
          Font_SetColor(0, 255, 0, 255);
        }
        Font_SetSize(50);
        Font_Print(8, 400, message_buffer);

        Font_Draw_TVDRC();

        uint32_t btns = wiiu_input_buttons_triggered();
        if (btns & VPAD_BUTTON_A) {
          message_buffer[0] = '\0';
          state = STATE_CONNECTING;
        }
        break;
      }
      case STATE_CONNECTING: {
        printf("Connecting to %s...\n", config.address);

        Font_Clear();
        Font_SetSize(50);
        Font_SetColor(255, 255, 255, 255);

        Font_Printf(8, 58, "Connecting to %s...\n", config.address);
        Font_Draw_TVDRC();

        int ret;
        if ((ret = gs_get_status(client, &server, config.address, config.unsupported)) == GS_OUT_OF_MEMORY) {
          fprintf(stderr, "Not enough memory\n");
          sprintf(message_buffer, "Not enough memory\n");
          is_error = 1;
          state = STATE_DISCONNECTED;
          break;
        } else if (ret == GS_ERROR) {
          fprintf(stderr, "Gamestream error: %s\n", gs_get_error_message());
          sprintf(message_buffer, "Gamestream error:\n%s\n", gs_get_error_message());
          is_error = 1;
          state = STATE_DISCONNECTED;
          break;
        } else if (ret == GS_INVALID) {
          fprintf(stderr, "Invalid data received from server: %s\n", gs_get_error_message());
          sprintf(message_buffer, "Invalid data received from server:\n%s\n", gs_get_error_message());
          is_error = 1;
          state = STATE_DISCONNECTED;
          break;
        } else if (ret == GS_UNSUPPORTED_VERSION) {
          fprintf(stderr, "Unsupported version: %s\n", gs_get_error_message());
          sprintf(message_buffer, "Unsupported version:\n%s\n", gs_get_error_message());
          is_error = 1;
          state = STATE_DISCONNECTED;
          break;
        } else if (ret != GS_OK) {
          fprintf(stderr, "Can't connect to server %s\n", config.address);
          sprintf(message_buffer, "Can't connect to server\n");
          is_error = 1;
          state = STATE_DISCONNECTED;
          break;
        }

        if (config.debug_level > 0) {
          printf("NVIDIA %s, GFE %s (%s, %s)\n", server.gpuType, server.serverInfo.serverInfoGfeVersion, server.gsVersion, server.serverInfo.serverInfoAppVersion);
          printf("Server codec flags: 0x%x\n", server.serverInfo.serverCodecModeSupport);
        }

        if (autostream) {
          state = STATE_START_STREAM;
          break;
        }

        state = STATE_CONNECTED;
        break;
      }
      case STATE_CONNECTED: {
        Font_Clear();
        Font_SetSize(50);
        Font_SetColor(255, 255, 255, 255);

        Font_Printf(8, 58, "Moonlight Wii U %s (Connected to %s)\n"
                           SCREEN_BAR
                           "Press \ue000 to stream\nPress \ue001 to pair\n", VERSION_STRING, config.address);

        if (is_error) {
          Font_SetColor(255, 0, 0, 255);
        }
        else {
          Font_SetColor(0, 255, 0, 255);
        }
        Font_SetSize(50);
        Font_Print(8, 400, message_buffer);
        Font_Draw_TVDRC();

        uint32_t btns = wiiu_input_buttons_triggered();
        if (btns & VPAD_BUTTON_A) {
          message_buffer[0] = '\0';
          state = STATE_START_STREAM;
        } else if (btns & VPAD_BUTTON_B) {
          message_buffer[0] = '\0';
          state = STATE_PAIRING;
        }
        break;
      }
      case STATE_PAIRING: {
        char pin[5];
        sprintf(pin, "%d%d%d%d", (unsigned)random() % 10, (unsigned)random() % 10, (unsigned)random() % 10, (unsigned)random() % 10);
        printf("Please enter the following PIN on the target PC: %s\n", pin);
        Font_Clear();
        Font_SetSize(50);
        Font_SetColor(255, 255, 255, 255);
        Font_Printf(8, 58, "Please enter the following PIN on the target PC:\n%s\n", pin);
        Font_Draw_TVDRC();
        gs_set_timeout(client, 60);
        if (gs_pair(client, &server, &pin[0]) != GS_OK) {
          fprintf(stderr, "Failed to pair to server: %s\n", gs_get_error_message());
          sprintf(message_buffer, "Failed to pair to server:\n%s\n", gs_get_error_message());
          is_error = 1;
        } else {
          printf("Succesfully paired\n");
          sprintf(message_buffer, "Succesfully paired\n");
          is_error = 0;
        }
        gs_set_timeout(client, 5);

        // wrong state, make sure we reconnect first
        if (server.currentGame != 0) {
          state = STATE_DISCONNECTED;
          break;
        }

        state = STATE_CONNECTED;
        break;
      }
      case STATE_START_STREAM: {
        Font_Clear();
        Font_SetSize(50);
        Font_SetColor(255, 255, 255, 255);

        Font_Print(8, 58, "Starting stream...");
        Font_Draw_TVDRC();

        if (server.paired) {
          // Wii U only supports H264
          config.stream.supportedVideoFormats = VIDEO_FORMAT_H264;

          wiiu_stream_reset();

          if (stream(client, &server, &config) == 0) {
            wiiu_proc_set_home_enabled(0);
            start_input_thread();
            if (start_stream_workers() != 0) {
              stop_input_thread();
              LiStopConnection();
              state = STATE_STOP_STREAM;
              break;
            }
            state = STATE_STREAMING;
            break;
          }
        }
        else {
          printf("You must pair with the PC first\n");
          sprintf(message_buffer, "You must pair with the PC first\n");
          is_error = 1;
        }

        state = STATE_CONNECTED;
        break;
      }
      case STATE_STREAMING: {
        static uint64_t lastHealthMs = 0;
        uint64_t now = now_ms();
        if (lastHealthMs == 0 || now - lastHealthMs >= 5000) {
          lastHealthMs = now;
          uint32_t depth = wiiu_stream_queue_depth();
          uint32_t highwater = wiiu_stream_queue_highwater();
          uint64_t netDelta = now - workers.networkTick;
          if (netDelta > 50 && workers.networkConnected) {
            printf("Health warn: network/control worker stalled for %llu ms\n", netDelta);
          }
          printf("Health: q=%u q_hi=%u net=%llums dec=%llums ren=%llums\n", depth, highwater, now - workers.networkTick, now - workers.decodeTick, now - workers.renderTick);
        }
        break;
      }
      case STATE_STOP_STREAM: {
        stop_input_thread();
        stop_stream_workers();
        LiStopConnection();
        wiiu_stream_reset();

        if (config.quitappafter) {
          if (config.debug_level > 0)
            printf("Sending app quit request ...\n");
          gs_quit_app(client, &server);
        }

        wiiu_proc_set_home_enabled(1);
        state = STATE_DISCONNECTED;
        break;
      }
    }

    if (state != STATE_STREAMING) {
      OSSleepTicks(OSMillisecondsToTicks(16));
    }
  }

  Font_Deinit();

  wiiu_stream_fini();

  wiiu_net_shutdown();

  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  WHBGfxShutdown();

  wiiu_proc_shutdown();

  return 0;
}
