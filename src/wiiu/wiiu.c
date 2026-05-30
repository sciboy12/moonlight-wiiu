#include "wiiu.h"
#include "stream_diag.h"

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <stdarg.h>

#include <whb/proc.h>
#include <whb/gfx.h>
#include <coreinit/fastmutex.h>
#include <coreinit/time.h>
#include <gx2/mem.h>
#include <gx2/draw.h>
#include <gx2/registers.h>

#include "shaders/display.h"

#define ATTRIB_SIZE (8 * 2 * sizeof(float))
#define ATTRIB_STRIDE (4 * sizeof(float))

static GX2Sampler screenSamp;
static WHBGfxShaderGroup shaderGroup;

static float* tvAttribs;
static float* drcAttribs;

static float tvScreenSize[2];
static float drcScreenSize[2];

uint32_t currentFrame;
uint32_t nextFrame;

static OSFastMutex queueMutex;
static yuv_texture_t* queueMessages[MAX_QUEUEMESSAGES];
static uint32_t queueWriteIndex;
static uint32_t queueReadIndex;
static uint32_t droppedFrames;

static void lock_queue_mutex(const char* caller)
{
  uint64_t startTicks = OSGetTime();
  OSFastMutex_Lock(&queueMutex);
  wiiu_stream_diag_log_mutex_wait(caller, OSGetTime() - startTicks);
}

void wiiu_stream_init(uint32_t width, uint32_t height)
{
  currentFrame = nextFrame = 0;

  OSFastMutex_Init(&queueMutex, "");
  queueReadIndex = queueWriteIndex = 0;
  droppedFrames = 0;

  if (!WHBGfxLoadGFDShaderGroup(&shaderGroup, 0, display_gsh)) {
    printf("Cannot load shader\n");
  }

  WHBGfxInitShaderAttribute(&shaderGroup, "in_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
  WHBGfxInitShaderAttribute(&shaderGroup, "in_texCoord", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32);

  if (!WHBGfxInitFetchShader(&shaderGroup)) {
    printf("cannot init shader\n");
  }

  GX2InitSampler(&screenSamp, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_POINT);

  GX2ColorBuffer* cb = WHBGfxGetTVColourBuffer();
  tvScreenSize[0] = 1.0f / (float) cb->surface.width;
  tvScreenSize[1] = 1.0f / (float) cb->surface.height;

  tvAttribs = memalign(GX2_VERTEX_BUFFER_ALIGNMENT, ATTRIB_SIZE);
  int i = 0;

  tvAttribs[i++] = 0.0f;                      tvAttribs[i++] = 0.0f;
  tvAttribs[i++] = 0.0f;                      tvAttribs[i++] = 0.0f;

  tvAttribs[i++] = (float) cb->surface.width; tvAttribs[i++] = 0.0f;
  tvAttribs[i++] = 1.0f;                      tvAttribs[i++] = 0.0f;

  tvAttribs[i++] = (float) cb->surface.width; tvAttribs[i++] = (float) cb->surface.height;
  tvAttribs[i++] = 1.0f;                      tvAttribs[i++] = 1.0f;

  tvAttribs[i++] = 0.0f;                      tvAttribs[i++] = (float) cb->surface.height;
  tvAttribs[i++] = 0.0f;                      tvAttribs[i++] = 1.0f;
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, tvAttribs, ATTRIB_SIZE);

  cb = WHBGfxGetDRCColourBuffer();
  drcScreenSize[0] = 1.0f / (float) cb->surface.width;
  drcScreenSize[1] = 1.0f / (float) cb->surface.height;

  drcAttribs = memalign(GX2_VERTEX_BUFFER_ALIGNMENT, ATTRIB_SIZE);
  i = 0;

  drcAttribs[i++] = 0.0f;                      drcAttribs[i++] = 0.0f;
  drcAttribs[i++] = 0.0f;                      drcAttribs[i++] = 0.0f;

  drcAttribs[i++] = (float) cb->surface.width; drcAttribs[i++] = 0.0f;
  drcAttribs[i++] = 1.0f;                      drcAttribs[i++] = 0.0f;

  drcAttribs[i++] = (float) cb->surface.width; drcAttribs[i++] = (float) cb->surface.height;
  drcAttribs[i++] = 1.0f;                      drcAttribs[i++] = 1.0f;

  drcAttribs[i++] = 0.0f;                      drcAttribs[i++] = (float) cb->surface.height;
  drcAttribs[i++] = 0.0f;                      drcAttribs[i++] = 1.0f;
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, drcAttribs, ATTRIB_SIZE);
}

static void reset_stream_queue(void)
{
  lock_queue_mutex("stream reset");
  currentFrame = nextFrame = 0;
  queueReadIndex = queueWriteIndex = 0;
  droppedFrames = 0;
  OSFastMutex_Unlock(&queueMutex);
}

void wiiu_stream_reset(void)
{
  wiiu_stream_diag_reset();
  reset_stream_queue();
}

void wiiu_stream_reset_queue(void)
{
  reset_stream_queue();
}

int wiiu_stream_draw(void)
{
  yuv_texture_t* tex = get_frame();
  if (tex) {
    uint32_t backlog = nextFrame - currentFrame;
    if (backlog > NUM_BUFFERS) {
      // display thread is behind decoder, skip this old frame
      currentFrame++;
    } else {
      WHBGfxBeginRender();

      // TV
      WHBGfxBeginRenderTV();
      WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);

      GX2SetPixelTexture(&tex->yTex, 0);
      GX2SetPixelTexture(&tex->uvTex, 1);
      GX2SetPixelSampler(&screenSamp, 0);
      GX2SetPixelSampler(&screenSamp, 1);

      GX2SetFetchShader(&shaderGroup.fetchShader);
      GX2SetVertexShader(shaderGroup.vertexShader);
      GX2SetPixelShader(shaderGroup.pixelShader);

      GX2SetVertexUniformReg(0, 2, tvScreenSize);
      GX2SetAttribBuffer(0, ATTRIB_SIZE, ATTRIB_STRIDE, tvAttribs);
      GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);

      WHBGfxFinishRenderTV();

      // DRC
      WHBGfxBeginRenderDRC();
      WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);

      GX2SetPixelTexture(&tex->yTex, 0);
      GX2SetPixelTexture(&tex->uvTex, 1);
      GX2SetPixelSampler(&screenSamp, 0);
      GX2SetPixelSampler(&screenSamp, 1);

      GX2SetFetchShader(&shaderGroup.fetchShader);
      GX2SetVertexShader(shaderGroup.vertexShader);
      GX2SetPixelShader(shaderGroup.pixelShader);

      GX2SetVertexUniformReg(0, 2, drcScreenSize);
      GX2SetAttribBuffer(0, ATTRIB_SIZE, ATTRIB_STRIDE, drcAttribs);
      GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
      
      WHBGfxFinishRenderDRC();

      WHBGfxFinishRender();
      currentFrame++;
      wiiu_stream_diag_note_frame_rendered();
    }
    return 1;
  }

  return 0;
}

void wiiu_stream_diag_dump_state(void)
{
  lock_queue_mutex("diagnostic dump");
  wiiu_stream_diag_dump(currentFrame, nextFrame, queueReadIndex, queueWriteIndex, droppedFrames);
  OSFastMutex_Unlock(&queueMutex);
}

void wiiu_stream_fini(void)
{
  free(tvAttribs);
  free(drcAttribs);

  WHBGfxFreeShaderGroup(&shaderGroup);
}

void* get_frame(void)
{
  lock_queue_mutex("get frame");

  uint32_t elements_in = queueWriteIndex - queueReadIndex;
  if(elements_in == 0) {
    wiiu_stream_diag_note_frame_empty();
    OSFastMutex_Unlock(&queueMutex);
    return NULL; // framequeue is empty
  }

  uint32_t i = (queueReadIndex)++ & (MAX_QUEUEMESSAGES - 1);
  yuv_texture_t* message = queueMessages[i];

  wiiu_stream_diag_note_frame_dequeued();

  OSFastMutex_Unlock(&queueMutex);
  return message;
}

void add_frame(yuv_texture_t* msg)
{
  lock_queue_mutex("add frame");

  uint32_t elements_in = queueWriteIndex - queueReadIndex;
  if (elements_in == MAX_QUEUEMESSAGES) {
    // Queue is full, drop the oldest frame so we can keep the latest decode output.
    queueReadIndex++;
    wiiu_stream_diag_note_frame_dropped();
    if ((++droppedFrames % 120) == 0) {
      wiiu_stream_diag_log_queue_overflow(droppedFrames);
    }
  }

  uint32_t i = (queueWriteIndex)++ & (MAX_QUEUEMESSAGES - 1);
  queueMessages[i] = msg;
  wiiu_stream_diag_note_frame_enqueued();

  OSFastMutex_Unlock(&queueMutex);
}

void wiiu_setup_renderstate(void)
{
  WHBGfxBeginRenderTV();
  GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
  GX2SetBlendControl(GX2_RENDER_TARGET_0,
    GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA,
    GX2_BLEND_COMBINE_MODE_ADD,
    TRUE,
    GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_INV_SRC_ALPHA,
    GX2_BLEND_COMBINE_MODE_ADD
  );
  GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
  WHBGfxBeginRenderDRC();
  GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
  GX2SetBlendControl(GX2_RENDER_TARGET_0,
    GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA,
    GX2_BLEND_COMBINE_MODE_ADD,
    TRUE,
    GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_INV_SRC_ALPHA,
    GX2_BLEND_COMBINE_MODE_ADD
  );
  GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
}
