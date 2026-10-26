#pragma once
// Backend for the web UI: serves the pages from WebPages.h and implements the
// JSON API (images on LittleFS, saved networks via WifiMgr).
//
//   GET  /                      gallery page
//   GET  /upload                upload page
//   GET  /style.css, /app.js    shared assets
//   GET  /api/images            {"w","h","files":[{name,size}],"used","total"}
//   GET  /img?name=X            serve a stored image
//   POST /api/images/delete     name=X
//   POST /api/images/draw       name=X
//   POST /api/upload            multipart, field "file" (must be a valid .bin)
//   POST /api/preview           multipart, field "file" (display only; never stored)
//   GET  /api/networks          {"connected","ssid","ip","saved":[...]}
//   POST /api/networks/add      ssid=X&pass=Y
//   POST /api/networks/remove   i=N
//   GET  /api/scan              ["ssid", ...]   (takes a few seconds)
//
// Image files are raw 1bpp bitmaps (see WebPages.h): black plane, then an
// optional red plane. Read one into canvas buffers with
// WebUI::readImage("name.bin", epd_b.getBuffer(), epd_r.getBuffer()).

#include <LittleFS.h>
#include "WifiMgr.h"
#include "WebPages.h"
#include "Epaper.h"

// WebUI namespace: contains helpers and state for the web interface
    namespace WebUI {
        // Constants
            constexpr const char *IMG_DIR = "/img";
            constexpr size_t MIN_FREE_BYTES = 16 * 1024;  // refuse uploads when almost full

        // State variables for the web UI and E-Paper display
            static WebServer &srv = WifiMgr::server;
            static int epdW = 400, epdH = 300;
            static size_t epdBytes = 15000;  // ((W+7)/8)*H, set in begin()

        // Upload state variables
            static File uploadFile;
            static String uploadPath;
            static size_t uploadBytes = 0;
            static bool uploadFailed = false;

        // In-memory preview state; the worker owns the buffer while queued/displaying.
            constexpr size_t PREVIEW_CAPACITY =
                2 * ((Epaper::WIDTH + 7) / 8) * Epaper::HEIGHT;
            static uint8_t previewData[PREVIEW_CAPACITY];
            static size_t previewBytes = 0;
            static bool previewBusy = false;
            static bool previewReceiving = false;
            static bool previewFailed = false;
            static portMUX_TYPE previewMux = portMUX_INITIALIZER_UNLOCKED;

        // JSON helpers
            inline String jsonEsc(const String &s) {
            String o;
            o.reserve(s.length() + 4);
            for (char c : s) {
                if (c == '"' || c == '\\') { o += '\\'; o += c; }
                else if ((uint8_t)c < 0x20) o += ' ';
                else o += c;
            }
            return o;
    }

    // Queue for drawing operations
    struct DrawRequest {
        char name[41];  // safeName() limits names to 40 characters
        bool preview;
    };

    static QueueHandle_t drawQueue = nullptr;

// [[Helpers]]
    // Response helpers
        inline void sendJson(const String &j) { srv.send(200, "application/json", j); }
        inline void sendErr(int code, const char *msg) { srv.send(code, "text/plain", msg); }
        inline void sendOk() { sendJson("{\"ok\":true}"); }

    // Strip any path, keep only [A-Za-z0-9._-], cap length, no leading dot.
        inline String safeName(String raw) {
            int a = raw.lastIndexOf('/'), b = raw.lastIndexOf('\\');
            int s = a > b ? a : b;
            if (s >= 0) raw = raw.substring(s + 1);
            String o;
            for (char c : raw) o += (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? c : '_';
            if (o.length() > 40) o = o.substring(o.length() - 40);  // keeps the extension
            if (o.startsWith(".")) o = "_" + o;
            return o;
        }

    // nullptr = file type not served. (.bin = display images; the rest are legacy test uploads)
        inline const char *mimeFor(String n) {
            n.toLowerCase();
            if (n.endsWith(".bin")) return "application/octet-stream";
            if (n.endsWith(".png")) return "image/png";
            if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
            if (n.endsWith(".gif")) return "image/gif";
            if (n.endsWith(".bmp")) return "image/bmp";
            if (n.endsWith(".webp")) return "image/webp";
            return nullptr;
        }

    // Construct the full path to an image file within the IMG_DIR directory.
        inline String imgPath(const String &name) { return String(IMG_DIR) + "/" + name; }

    // Read a stored image into a 1bpp buffer of epdBytes (e.g. GFXcanvas1::getBuffer()).
        // Files are the black plane, optionally followed by a red plane (2*epdBytes).
        // Pass redDst to receive the red plane (it is cleared if the file has none).
        // Returns false if the file is missing or not exactly the right size.
        inline bool readImage(const String &name, uint8_t *dst, uint8_t *redDst = nullptr) {
            File f = LittleFS.open(imgPath(safeName(name)), "r");
            if (!f) return false;
            size_t sz = f.size();
            bool ok = (sz == epdBytes || sz == 2 * epdBytes) && f.read(dst, epdBytes) == epdBytes;
            if (ok && redDst) {
                if (sz == 2 * epdBytes) ok = f.read(redDst, epdBytes) == epdBytes;
                else memset(redDst, 0, epdBytes);
            }
            f.close();
            return ok;
        }

    // Worker function that continuously processes draw requests from the drawQueue.
        inline void displayWorker(void *) {
            DrawRequest request;

            for (;;) {
                if (xQueueReceive(drawQueue, &request, portMAX_DELAY) != pdTRUE) {
                    continue;
                }

                if (request.preview) {
                    Serial.println("[ui] Display worker received preview");
                    const bool displayed = Epaper::displayImageData(previewData, previewBytes);
                    if (!displayed) Serial.println("[ui] Preview display failed");
                    portENTER_CRITICAL(&previewMux);
                    previewBusy = false;
                    portEXIT_CRITICAL(&previewMux);
                    continue;
                }

                Serial.printf("[ui] Display worker received: %s\n", request.name);
                File image = LittleFS.open(imgPath(String(request.name)), "r");
                if (!image) {
                    Serial.printf("[ui] Queued image not found: %s\n", request.name);
                    continue;
                }

                const bool displayed = Epaper::displayImage(image);
                image.close();

                if (!displayed) {
                    Serial.printf("[ui] Display failed for image: %s\n", request.name);
                } else {
                    Serial.printf("[ui] Displayed image: %s\n", request.name);
                }
            }
        }

// [[Images]]
    inline void apiImages() {
        String j = "{\"w\":" + String(epdW) + ",\"h\":" + String(epdH) + ",\"files\":[";
        bool first = true;
        File dir = LittleFS.open(IMG_DIR);
        if (dir) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                if (f.isDirectory()) continue;
                if (!first) j += ',';
                first = false;
                j += "{\"name\":\"" + jsonEsc(f.name()) + "\",\"size\":" + String((unsigned)f.size()) + "}";
            }
        }
        j += "],\"used\":" + String((unsigned)LittleFS.usedBytes()) + ",\"total\":" + String((unsigned)LittleFS.totalBytes()) + "}";
        sendJson(j);
    }

    inline void serveImage() {
        String name = safeName(srv.arg("name"));
        const char *mime = mimeFor(name);
        if (!name.length() || !mime || !LittleFS.exists(imgPath(name))) {
            sendErr(404, "Not found");
            return;
        }
        File f = LittleFS.open(imgPath(name), "r");
        srv.sendHeader("Cache-Control", "no-cache");  // same name can be re-uploaded with new content
        srv.streamFile(f, mime);
        f.close();
    }

    inline void deleteImage() {
        String name = safeName(srv.arg("name"));
        if (!name.length() || !LittleFS.remove(imgPath(name))) {
            sendErr(404, "Not found");
            return;
        }
        sendOk();
    }

    inline void uploadData() {
        HTTPUpload &up = srv.upload();
        if (up.status == UPLOAD_FILE_START) {
            uploadPath = "";
            uploadBytes = 0;
            String name = safeName(up.filename);
            uploadFailed = !name.endsWith(".bin") ||
                        (LittleFS.totalBytes() - LittleFS.usedBytes()) < MIN_FREE_BYTES;

            if (!uploadFailed) {
                uploadPath = imgPath(name);
                uploadFile = LittleFS.open(uploadPath, "w");
                uploadFailed = !uploadFile;
            }
        } else if (up.status == UPLOAD_FILE_WRITE) {
            if (uploadFile && !uploadFailed) {
                // too big, or storage full -> drop the partial file
                if (uploadBytes + up.currentSize > 2 * epdBytes || uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
                    uploadFailed = true;
                    uploadFile.close();
                    LittleFS.remove(uploadPath);
                } else {
                    uploadBytes += up.currentSize;
                }
            }
        } else if (up.status == UPLOAD_FILE_END) {
            if (uploadFile) {
                uploadFile.close();
                if (!uploadFailed && uploadBytes != epdBytes && uploadBytes != 2 * epdBytes) {  // wrong size = not a valid image for this display
                    uploadFailed = true;
                    LittleFS.remove(uploadPath);
                }
            }
        } else if (up.status == UPLOAD_FILE_ABORTED) {
            if (uploadFile) uploadFile.close();
            if (uploadPath.length()) LittleFS.remove(uploadPath);
            uploadFailed = true;
        }
    }

    inline void uploadDone() {
        if (uploadFailed) sendErr(500, "Rejected (not a valid image for this display, or storage full)");
        else sendOk();
    }

    inline void previewDataUpload() {
        HTTPUpload &up = srv.upload();
        if (up.status == UPLOAD_FILE_START) {
            bool available;
            portENTER_CRITICAL(&previewMux);
            available = !previewBusy;
            if (available) previewBusy = true;
            portEXIT_CRITICAL(&previewMux);

            previewReceiving = available;
            previewFailed = !available || !drawQueue;
            previewBytes = 0;
            if (previewFailed && available) {
                portENTER_CRITICAL(&previewMux);
                previewBusy = false;
                portEXIT_CRITICAL(&previewMux);
            }
        } else if (up.status == UPLOAD_FILE_WRITE) {
            if (previewReceiving && !previewFailed) {
                if (previewBytes + up.currentSize > sizeof(previewData)) {
                    previewFailed = true;
                } else {
                    memcpy(previewData + previewBytes, up.buf, up.currentSize);
                    previewBytes += up.currentSize;
                }
            }
        } else if (up.status == UPLOAD_FILE_END) {
            if (previewBytes != epdBytes && previewBytes != 2 * epdBytes) {
                previewFailed = true;
            }
        } else if (up.status == UPLOAD_FILE_ABORTED) {
            previewFailed = true;
        }
    }

    inline void previewDone() {
        if (!previewReceiving || previewFailed) {
            const bool busy = previewBusy;
            if (previewReceiving && busy) {
                portENTER_CRITICAL(&previewMux);
                previewBusy = false;
                portEXIT_CRITICAL(&previewMux);
            }
            previewReceiving = false;
            const bool unavailable = busy || !drawQueue;
            sendErr(unavailable ? 503 : 400,
                    unavailable ? "Display busy or worker unavailable" :
                                  "Preview must contain a valid image for this display");
            return;
        }

        DrawRequest request{};
        request.preview = true;
        if (xQueueSend(drawQueue, &request, 0) != pdTRUE) {
            portENTER_CRITICAL(&previewMux);
            previewBusy = false;
            portEXIT_CRITICAL(&previewMux);
            previewReceiving = false;
            sendErr(503, "Display queue is full");
            return;
        }

        previewReceiving = false;
        srv.send(202, "application/json", "{\"ok\":true,\"status\":\"queued\"}");
    }

    inline void displayImage() {
        String name = safeName(srv.arg("name"));
        if (!name.length()) {
            sendErr(400, "Missing image name");
            return;
        }

        File image = LittleFS.open(imgPath(name), "r");
        if (!image) {
            sendErr(404, "Image not found");
            return;
        }

        const bool displayed = Epaper::displayImage(image);
        image.close();

        if (!displayed) {
            sendErr(422, "Image is invalid or the display failed");
            return;
        }

        sendOk();
    }

    // POST /api/images/draw       name=X
        inline void drawImage() {
            String name = safeName(srv.arg("name"));
            if (!name.length()) return sendErr(400, "Missing image name");
            if (!drawQueue) return sendErr(503, "Display worker unavailable");
            if (!LittleFS.exists(imgPath(name))) return sendErr(404, "Image not found");

            DrawRequest request{};
            name.toCharArray(request.name, sizeof(request.name));
            request.preview = false;

            if (xQueueSend(drawQueue, &request, 0) != pdTRUE) {
                return sendErr(503, "Display queue is full");
            }

            srv.send(202, "application/json", "{\"ok\":true,\"status\":\"queued\"}");
        }

// [[Networks]]
    inline void apiNetworks() {
        bool c = WiFi.status() == WL_CONNECTED;
        String ssid = c ? WiFi.SSID() : String("");
        String ip = c ? WiFi.localIP().toString() : String("");
        String j = String("{\"connected\":") + (c ? "true" : "false") + ",\"ssid\":\"" + jsonEsc(ssid) +
                    "\",\"ip\":\"" + ip + "\",\"saved\":[";
        int n = WifiMgr::count();
        for (int i = 0; i < n; i++) {
            if (i) j += ',';
            j += "\"" + jsonEsc(WifiMgr::getSsid(i)) + "\"";
        }
        j += "]}";
        sendJson(j);
    }

    inline void addNetwork() {
        String ssid = srv.arg("ssid");
        if (!ssid.length() || ssid.length() > 32) return sendErr(400, "Invalid SSID");
        if (!WifiMgr::addNet(ssid, srv.arg("pass"))) return sendErr(409, "Saved list is full");
        WifiMgr::retryNow = true;
        sendOk();
    }

    inline void removeNetwork() {
        WifiMgr::removeNet(srv.arg("i").toInt());
        sendOk();
    }

    inline void apiScan() {
        int n = WiFi.scanNetworks();
        String j = "[";
        bool first = true;
        for (int i = 0; i < n; i++) {
            String s = jsonEsc(WiFi.SSID(i));
            if (!s.length() || j.indexOf("\"" + s + "\"") >= 0) continue;  // hidden / duplicate
            if (!first) j += ',';
            first = false;
            j += "\"" + s + "\"";
        }
        j += "]";
        WiFi.scanDelete();
        sendJson(j);
    }

// [[Begin]]
    // Call after WifiMgr::begin() and before WifiMgr::startServer().
    // width/height = your display size (EPD_WIDTH, EPD_HEIGHT).
    inline void begin(int width, int height) {
        // Store display dimensions and calculate required buffer size.
            epdW = width;
            epdH = height;
            epdBytes = (size_t)((width + 7) / 8) * height;

        // Initialize LittleFS and create image directory if needed.
            if (LittleFS.begin(true)) {  // true = format on first use
                LittleFS.mkdir(IMG_DIR);
                Serial.printf("[ui] LittleFS ok, %u/%u bytes used, image size %u bytes\n",
                            (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes(), (unsigned)epdBytes);
            } else {
                Serial.println("[ui] LittleFS mount failed");
            }

        // Initialize the display worker queue and task.
            drawQueue = xQueueCreate(1, sizeof(DrawRequest));
            if (!drawQueue) {
                Serial.println("[ui] Could not create display queue");
            } else if (xTaskCreatePinnedToCore(
                        displayWorker, "epdDisplay", 8192, nullptr, 1, nullptr, 1) != pdPASS) {
                vQueueDelete(drawQueue);
                drawQueue = nullptr;
                Serial.println("[ui] Could not start display worker");
            }

        // Set up web server routes for the UI and API endpoints.s
            srv.on("/", HTTP_GET, [] { srv.send_P(200, "text/html; charset=utf-8", PAGE_INDEX); });
            srv.on("/upload", HTTP_GET, [] { srv.send_P(200, "text/html; charset=utf-8", PAGE_UPLOAD); });
            srv.on("/style.css", HTTP_GET, [] { srv.send_P(200, "text/css", PAGE_CSS); });
            srv.on("/app.js", HTTP_GET, [] { srv.send_P(200, "application/javascript", PAGE_JS); });

            srv.on("/api/images", HTTP_GET, apiImages);
            srv.on("/img", HTTP_GET, serveImage);
            srv.on("/api/images/delete", HTTP_POST, deleteImage);
            srv.on("/api/images/draw", HTTP_POST, drawImage);
            srv.on("/api/upload", HTTP_POST, uploadDone, uploadData);
            srv.on("/api/preview", HTTP_POST, previewDone, previewDataUpload);

            srv.on("/api/networks", HTTP_GET, apiNetworks);
            srv.on("/api/networks/add", HTTP_POST, addNetwork);
            srv.on("/api/networks/remove", HTTP_POST, removeNetwork);
            srv.on("/api/scan", HTTP_GET, apiScan);

            srv.onNotFound([] { sendErr(404, "Not found"); });
    }
}