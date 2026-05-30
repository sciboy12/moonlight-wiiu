#include "stream_diag.h"

#if __has_include(<enet/enet.h>)
#include <enet/enet.h>

/* Linker --wrap hooks for successful ENet socket I/O and queued reliable packet sends. */

int __real_enet_socket_send(ENetSocket socket, const ENetAddress* address, const ENetBuffer* buffers, size_t bufferCount);
int __real_enet_socket_receive(ENetSocket socket, ENetAddress* address, ENetBuffer* buffers, size_t bufferCount);
int __real_enet_peer_send(ENetPeer* peer, enet_uint8 channelID, ENetPacket* packet);

int __wrap_enet_socket_send(ENetSocket socket, const ENetAddress* address, const ENetBuffer* buffers, size_t bufferCount) {
  int ret = __real_enet_socket_send(socket, address, buffers, bufferCount);

  if (ret > 0) {
    wiiu_stream_diag_mark_enet_send();
  }

  return ret;
}

int __wrap_enet_socket_receive(ENetSocket socket, ENetAddress* address, ENetBuffer* buffers, size_t bufferCount) {
  int ret = __real_enet_socket_receive(socket, address, buffers, bufferCount);

  if (ret > 0) {
    wiiu_stream_diag_mark_enet_receive();
  }

  return ret;
}

int __wrap_enet_peer_send(ENetPeer* peer, enet_uint8 channelID, ENetPacket* packet) {
  int ret = __real_enet_peer_send(peer, channelID, packet);

  if (ret == 0) {
    wiiu_stream_diag_mark_enet_send();
  }

  return ret;
}
#endif
