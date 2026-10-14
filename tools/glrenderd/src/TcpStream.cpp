// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// renderserver still references tcp, glrenderd only ever listens on the unix socket
#include "TcpStream.h"

TcpStream::TcpStream(size_t bufSize) : SocketStream(bufSize) {}
TcpStream::TcpStream(int sock, size_t bufSize) : SocketStream(sock, bufSize) {}
int TcpStream::listen(char addrstr[MAX_ADDRSTR_LEN]) { return -1; }
SocketStream* TcpStream::accept() { return nullptr; }
int TcpStream::connect(const char* addr) { return -1; }
