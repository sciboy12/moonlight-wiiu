#include "wiiu.h"

#include <stdlib.h>
#include <malloc.h>

#include <coreinit/thread.h>
#include <nn/nets2/somemopt.h>

// 0x300000 is the maximum for somemopt
#define NET_MEMORY_SIZE 0x300000

static OSThread memoryThread;
static void* netMemory = NULL;
static bool netMemoryInitialized = false;

static int wiiu_net_memory_thread(int argc, const char **argv)
{
  // This provides a bunch of memory to the net stack,
  // which we can use for buffering the videostream socket
  netMemory = memalign(0x40, NET_MEMORY_SIZE);
  if (netMemory == NULL) {
    printf("Failed to allocate SOMEMOPT buffer.\n");
    return -1;
  }

  int rc = somemopt(SOMEMOPT_REQUEST_INIT, netMemory, NET_MEMORY_SIZE, SOMEMOPT_FLAGS_NONE);
  printf("SOMEMOPT_REQUEST_INIT: %d\n", rc);
  if (rc < 0) {
    free(netMemory);
    netMemory = NULL;
    return rc;
  }

  netMemoryInitialized = true;

  return 0;
}

static void wiiu_net_thread_deallocator(OSThread* thread, void* stack)
{
  free(stack);
}

void wiiu_net_init(void)
{
  const int stack_size = 4 * 1024 * 1024;
  uint8_t* stack = (uint8_t*)memalign(16, stack_size);
  if (!stack) {
    return;
  }

  if (!OSCreateThread(&memoryThread,
                      wiiu_net_memory_thread,
                      0, NULL,
                      stack + stack_size, stack_size,
                      0x10,
                      OS_THREAD_ATTRIB_AFFINITY_ANY | OS_THREAD_ATTRIB_DETACHED))
  {
    free(stack);
    return;
  }

  OSSetThreadName(&memoryThread, "NetMemory");
  OSSetThreadDeallocator(&memoryThread, wiiu_net_thread_deallocator);
  OSResumeThread(&memoryThread);

  // wait for somemopt to be initialized
  int rc = somemopt(SOMEMOPT_REQUEST_WAIT_FOR_INIT, NULL, 0, SOMEMOPT_FLAGS_NONE);
  printf("SOMEMOPT_REQUEST_WAIT_FOR_INIT: %d\n", rc);
  if (rc < 0 || !netMemoryInitialized) {
    printf("Warning: SOMEMOPT initialization failed; stream sockets may stall under packet bursts.\n");
  }
}

void wiiu_net_shutdown(void)
{
#ifdef SOMEMOPT_REQUEST_FINI
  if (netMemoryInitialized) {
    int rc = somemopt(SOMEMOPT_REQUEST_FINI, NULL, 0, SOMEMOPT_FLAGS_NONE);
    printf("SOMEMOPT_REQUEST_FINI: %d\n", rc);
    netMemoryInitialized = false;
  }
#else
  netMemoryInitialized = false;
#endif

  if (netMemory != NULL) {
    free(netMemory);
    netMemory = NULL;
  }
}
