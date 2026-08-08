#pragma once
#include <Arduino.h>
#include <M5_Ethernet.h>
#include <functional>

enum PoeHttpMethod { HTTP_ANY, HTTP_GET, HTTP_POST };
enum PoeUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct PoeHTTPUpload {
  PoeUploadStatus status = UPLOAD_FILE_ABORTED;
  String filename;
  uint8_t buf[1024];
  size_t currentSize = 0;
  size_t totalSize = 0;
};
class PoeEthernetServer : public EthernetServer {
 public:
  explicit PoeEthernetServer(uint16_t port) : EthernetServer(port), port_(port) {}
  void begin() override { EthernetServer::begin(port_); }
 private:
  uint16_t port_;
};
class PoeWebServer {
 public:
  using Handler = std::function<void(void)>;
  explicit PoeWebServer(uint16_t port) : server_(port) {}
  void on(const char* uri, PoeHttpMethod method, Handler handler) { addRoute(uri, method, handler, nullptr); }
  void on(const char* uri, PoeHttpMethod method, Handler handler, Handler uploadHandler) { addRoute(uri, method, handler, uploadHandler); }
  void begin() { server_.begin(); }
  bool hasArg(const String& name) const { return name == "plain" ? body_.length() > 0 : formName_ == name; }
  String arg(const String& name) const { return name == "plain" ? body_ : formName_ == name ? formValue_ : String(); }
  PoeHTTPUpload& upload() { return upload_; }
  bool authenticate(const char* user, const char* password) const {
    return authorization_ == "Basic " + base64(String(user) + ":" + password);
  }
  void requestAuthentication() {
    if (!active_) return;
    active_->println("HTTP/1.1 401 Unauthorized");
    active_->println("WWW-Authenticate: Basic realm=\"firmware\"");
    active_->println("Content-Length: 0\r\nConnection: close\r\n");
    responseSent_ = true;
  }
  void send(int code, const char* type, const String& content) {
    if (!active_) return;
    active_->print("HTTP/1.1 "); active_->print(code); active_->print(' ');
    active_->println(code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized" :
                     code == 404 ? "Not Found" : "Internal Server Error");
    active_->print("Content-Type: "); active_->println(type);
    active_->print("Content-Length: "); active_->println(content.length());
    active_->println("Connection: close\r\n");
    active_->print(content);
    responseSent_ = true;
  }
  void handleClient() {
    EthernetClient incoming = server_.available();
    if (!incoming) return;
    activeClient_ = incoming; active_ = &activeClient_; responseSent_ = false;
    body_ = ""; formName_ = ""; formValue_ = ""; authorization_ = "";
    const unsigned long deadline = millis() + 5000;
    String requestLine = readLine(deadline);
    if (!requestLine.length()) { finish(); return; }
    int firstSpace = requestLine.indexOf(' '), secondSpace = requestLine.indexOf(' ', firstSpace + 1);
    String verb = requestLine.substring(0, firstSpace);
    String uri = requestLine.substring(firstSpace + 1, secondSpace);
    int query = uri.indexOf('?'); if (query >= 0) uri = uri.substring(0, query);
    PoeHttpMethod method = verb == "POST" ? HTTP_POST : HTTP_GET;
    size_t contentLength = 0; String contentType;
    while (active_->connected() && millis() < deadline) {
      String line = readLine(deadline); if (!line.length()) break;
      int colon = line.indexOf(':'); if (colon < 0) continue;
      String key = line.substring(0, colon), value = line.substring(colon + 1);
      key.toLowerCase(); value.trim();
      if (key == "content-length") contentLength = value.toInt();
      else if (key == "content-type") contentType = value;
      else if (key == "authorization") authorization_ = value;
    }
    Route* route = findRoute(uri, method);
    if (!route) { send(404, "text/plain", "Not found"); finish(); return; }
    if (contentLength && contentType.startsWith("multipart/form-data") && route->uploadHandler)
      handleMultipart(contentLength, contentType, *route, deadline);
    else {
      body_.reserve(contentLength);
      while (body_.length() < contentLength && active_->connected() && millis() < deadline) {
        if (active_->available()) body_ += char(active_->read()); else delay(1);
      }
      if (contentType.startsWith("application/x-www-form-urlencoded")) parseForm(body_);
      route->handler();
    }
    if (!responseSent_) send(500, "text/plain", "No response");
    finish();
  }
 private:
  struct Route { String uri; PoeHttpMethod method; Handler handler; Handler uploadHandler; };
  PoeEthernetServer server_;
  EthernetClient activeClient_;
  EthernetClient* active_ = nullptr;
  Route routes_[12]; uint8_t routeCount_ = 0;
  String body_, formName_, formValue_, authorization_;
  PoeHTTPUpload upload_; bool responseSent_ = false;
  void addRoute(const char* uri, PoeHttpMethod method, Handler handler, Handler uploadHandler) {
    if (routeCount_ < 12) routes_[routeCount_++] = {uri, method, handler, uploadHandler};
  }
  Route* findRoute(const String& uri, PoeHttpMethod method) {
    for (uint8_t i = 0; i < routeCount_; i++)
      if (routes_[i].uri == uri && (routes_[i].method == method || routes_[i].method == HTTP_ANY)) return &routes_[i];
    return nullptr;
  }
  String readLine(unsigned long deadline, size_t* bytes = nullptr) {
    String line;
    while (active_->connected() && millis() < deadline) {
      if (!active_->available()) { delay(1); continue; }
      char c = active_->read(); if (bytes) (*bytes)++;
      if (c == '\n') break; if (c != '\r') line += c;
    }
    return line;
  }
  void handleMultipart(size_t contentLength, const String& contentType, Route& route, unsigned long deadline) {
    int bp = contentType.indexOf("boundary=");
    if (bp < 0) { send(400, "text/plain", "Missing multipart boundary"); return; }
    String boundary = contentType.substring(bp + 9); boundary.replace("\"", ""); boundary.trim();
    size_t consumed = 0; readLine(deadline, &consumed);
    String disposition = readLine(deadline, &consumed);
    readLine(deadline, &consumed); readLine(deadline, &consumed);
    int fp = disposition.indexOf("filename=\"");
    if (fp >= 0) { int start = fp + 10; upload_.filename = disposition.substring(start, disposition.indexOf('"', start)); }
    size_t trailerLength = boundary.length() + 8;
    if (contentLength < consumed + trailerLength) { send(400, "text/plain", "Invalid firmware upload"); return; }
    size_t remaining = contentLength - consumed - trailerLength;
    upload_.status = UPLOAD_FILE_START; upload_.currentSize = upload_.totalSize = 0; route.uploadHandler();
    while (remaining && active_->connected() && millis() < deadline + 120000UL) {
      size_t wanted = min(remaining, sizeof(upload_.buf)), received = 0;
      while (received < wanted && active_->connected()) {
        if (active_->available()) upload_.buf[received++] = active_->read(); else delay(1);
      }
      if (!received) break;
      remaining -= received; upload_.status = UPLOAD_FILE_WRITE;
      upload_.currentSize = received; upload_.totalSize += received; route.uploadHandler();
    }
    size_t drain = trailerLength;
    while (drain && active_->connected()) { if (active_->available()) { active_->read(); drain--; } else delay(1); }
    upload_.status = remaining ? UPLOAD_FILE_ABORTED : UPLOAD_FILE_END; upload_.currentSize = 0;
    route.uploadHandler(); route.handler();
  }
  void parseForm(const String& body) {
    int equals = body.indexOf('='); if (equals < 0) return;
    formName_ = urlDecode(body.substring(0, equals)); formValue_ = urlDecode(body.substring(equals + 1));
  }
  static String urlDecode(const String& input) {
    String out;
    for (uint16_t i = 0; i < input.length(); i++) {
      if (input[i] == '+') out += ' ';
      else if (input[i] == '%' && i + 2 < input.length()) {
        char hex[3] = {input[i + 1], input[i + 2], 0}; out += char(strtol(hex, nullptr, 16)); i += 2;
      } else out += input[i];
    }
    return out;
  }
  static String base64(const String& input) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out; int value = 0, bits = -6;
    for (uint16_t i = 0; i < input.length(); i++) {
      value = (value << 8) | uint8_t(input[i]); bits += 8;
      while (bits >= 0) { out += table[(value >> bits) & 0x3F]; bits -= 6; }
    }
    if (bits > -6) out += table[((value << 8) >> (bits + 8)) & 0x3F];
    while (out.length() % 4) out += '='; return out;
  }
  void finish() { delay(1); if (active_) active_->stop(); active_ = nullptr; }
};
