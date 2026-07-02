/*
 * web_api.h - static file serving for the local UI
 *
 * The BirdWatch local UI (web/local/index.html + web/assets/) is copied
 * onto the SD card as /web/index.html and /web/assets/... and served
 * from there, so UI updates never require reflashing. If the SD has no
 * UI, a minimal built-in status page keeps the camera reachable.
 *
 * /birds/IMG_n.jpg detection photos are served straight from SD too.
 */
#ifndef WEB_API_H
#define WEB_API_H

#include <WebServer.h>
#include "SD_MMC.h"

extern WebServer server;

class WebFiles {
public:
  static String contentType(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".ico"))  return "image/x-icon";
    return "application/octet-stream";
  }

  static bool streamFromSD(String path) {
    if (!SD_MMC.exists(path)) return false;
    File f = SD_MMC.open(path, FILE_READ);
    if (!f || f.isDirectory()) { if (f) f.close(); return false; }
    server.streamFile(f, contentType(path));
    f.close();
    return true;
  }

  // Root + not-found handler: map the site onto the SD /web tree, and
  // let /birds photos and /assets illustrations through directly.
  static void handleNotFound() {
    String uri = server.uri();
    if (uri == "/" || uri == "/index.html") {
      if (streamFromSD("/web/index.html")) return;
      server.send(200, "text/html", fallbackPage());
      return;
    }
    if (uri.startsWith("/birds/") && uri.endsWith(".jpg")) {
      if (streamFromSD(uri)) return;
    }
    if (uri.startsWith("/assets/")) {
      if (streamFromSD("/web" + uri)) return;
    }
    if (streamFromSD("/web" + uri)) return;
    server.send(404, "text/plain", "Not found");
  }

  static String fallbackPage() {
    return String(
      "<!doctype html><html><head><meta name=viewport content='width=device-width'>"
      "<title>birdwatch cam</title></head>"
      "<body style='font-family:Georgia,serif;background:#fcfcfb;color:#1a1612;"
      "max-width:34em;margin:8vh auto;padding:0 1em'>"
      "<p style='font-style:italic;color:#4a3f31'>birdwatch cam</p>"
      "<h1 style='letter-spacing:.06em'>CAMERA ONLINE</h1>"
      "<p>The web UI isn't on the SD card yet. Copy the repo's "
      "<code>web/local/index.html</code> to <code>/web/index.html</code> and "
      "<code>web/assets/</code> to <code>/web/assets/</code> on the card.</p>"
      "<p>The JSON API is live: <a href='/api/status'>/api/status</a> · "
      "<a href='/api/detections'>/api/detections</a> · "
      "<a href='/api/config'>/api/config</a></p>"
      "</body></html>");
  }
};

#endif
