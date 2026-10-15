#include "http_server.h"

WebServer &httpServer() {
  static WebServer server(80);
  return server;
}
