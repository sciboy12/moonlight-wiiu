#include "stream_diag.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <enet/enet.h>

ssize_t __real_recv(int socket, void* buffer, size_t length, int flags);
ssize_t __real_recvfrom(int socket, void* buffer, size_t length, int flags, struct sockaddr* address, socklen_t* addressLength);
ssize_t __real_send(int socket, const void* buffer, size_t length, int flags);
ssize_t __real_sendto(int socket, const void* buffer, size_t length, int flags, const struct sockaddr* destAddress, socklen_t addressLength);
int __real_enet_host_service(ENetHost* host, ENetEvent* event, enet_uint32 timeout);

static int is_rtp_packet(const uint8_t* packet, ssize_t length)
{
  return length >= 12 && (packet[0] & 0xC0) == 0x80;
}

static void note_media_recv(const void* buffer, ssize_t length)
{
  const uint8_t* packet = (const uint8_t*)buffer;

  if (!is_rtp_packet(packet, length)) {
    return;
  }

  uint8_t payloadType = packet[1] & 0x7F;
  if (payloadType == 97 || payloadType == 127) {
    wiiu_stream_diag_note_audio_recv();
  }
  else {
    wiiu_stream_diag_note_video_recv();
  }
}

ssize_t __wrap_recv(int socket, void* buffer, size_t length, int flags)
{
  ssize_t rc = __real_recv(socket, buffer, length, flags);
  if (rc > 0) {
    note_media_recv(buffer, rc);
  }
  return rc;
}

ssize_t __wrap_recvfrom(int socket, void* buffer, size_t length, int flags, struct sockaddr* address, socklen_t* addressLength)
{
  ssize_t rc = __real_recvfrom(socket, buffer, length, flags, address, addressLength);
  if (rc > 0) {
    note_media_recv(buffer, rc);
  }
  return rc;
}

ssize_t __wrap_send(int socket, const void* buffer, size_t length, int flags)
{
  ssize_t rc = __real_send(socket, buffer, length, flags);
  if (rc > 0) {
    wiiu_stream_diag_note_socket_send();
  }
  return rc;
}

ssize_t __wrap_sendto(int socket, const void* buffer, size_t length, int flags, const struct sockaddr* destAddress, socklen_t addressLength)
{
  ssize_t rc = __real_sendto(socket, buffer, length, flags, destAddress, addressLength);
  if (rc > 0) {
    wiiu_stream_diag_note_socket_send();
  }
  return rc;
}

int __wrap_enet_host_service(ENetHost* host, ENetEvent* event, enet_uint32 timeout)
{
  int rc = __real_enet_host_service(host, event, timeout);
  if (rc > 0 && event != NULL && event->type == ENET_EVENT_TYPE_RECEIVE) {
    wiiu_stream_diag_note_control_recv();
    wiiu_stream_diag_note_enet_receive();
  }
  return rc;
}
