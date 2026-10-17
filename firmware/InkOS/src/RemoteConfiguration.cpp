#include "RemoteConfiguration.h"
#include <Preferences.h>
#include <WebServer.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <vector>
namespace ink {
static uint8_t key[32];
static bool enabled = false;
struct Challenge { String value; uint32_t issued = 0; };
static Challenge challenges[4];
static unsigned slot = 0;
static String hex(const uint8_t *p, size_t n) {
  const char *digits = "0123456789abcdef";
  String s; s.reserve(n * 2);
  while (n--) { s += digits[*p >> 4]; s += digits[*p++ & 15]; }
  return s;
}
static const char *field(cJSON *j, const char *name) {
  auto v = cJSON_GetObjectItemCaseSensitive(j, name);
  return cJSON_IsString(v) ? v->valuestring : "";
}
static String base64(const uint8_t *p, size_t n) {
  std::vector<uint8_t> out((n + 2) / 3 * 4 + 1); size_t len = 0;
  if (mbedtls_base64_encode(out.data(), out.size(), &len, p, n)) return "";
  return String((const char *)out.data(), len);
}
static bool decode(const char *s, std::vector<uint8_t> &out, size_t max) {
  size_t n = strlen(s), written = 0;
  if (!n || n > (max + 2) / 3 * 4) return false;
  out.resize(n / 4 * 3 + 3);
  if (mbedtls_base64_decode(out.data(), out.size(), &written, (const uint8_t *)s, n) || written > max) return false;
  out.resize(written); return true;
}
void loadRemoteConfiguration() {
  Preferences p;
  if (p.begin("inkos", true)) {
    enabled = p.getBytesLength("remote_key") == sizeof key && p.getBytes("remote_key", key, sizeof key) == sizeof key;
    p.end();
  }
}
bool remoteEnabled() { return enabled; }
cJSON *remotePairCommand(cJSON *command) {
  auto out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "inkos", true);
  bool enable = cJSON_IsTrue(cJSON_GetObjectItem(command, "enabled"));
  uint8_t candidate[32]; esp_fill_random(candidate, sizeof candidate);
  Preferences p; bool ok = p.begin("inkos", false);
  if (ok) {
    if (enable) ok = p.putBytes("remote_key", candidate, sizeof candidate) == sizeof candidate;
    else { p.remove("remote_key"); ok = p.getBytesLength("remote_key") == 0; }
    p.end();
  }
  if (ok) {
    enabled = enable; memcpy(key, candidate, sizeof key);
    for (auto &c : challenges) c.value = "";
    if (enabled) cJSON_AddStringToObject(out, "pairing_key", hex(key, sizeof key).c_str());
  }
  memset(candidate, 0, sizeof candidate);
  cJSON_AddBoolToObject(out, "saved", ok); cJSON_AddBoolToObject(out, "remote_enabled", enabled);
  return out;
}
void registerRemoteConfiguration(WebServer &server) {
  server.on("/api/challenge", HTTP_GET, [&server]() {
    server.sendHeader("Cache-Control", "no-store");
    if (!enabled) { server.send(403, "application/json", "{\"error\":\"Pair over USB first\"}"); return; }
    uint8_t random[16]; esp_fill_random(random, sizeof random);
    auto &c = challenges[slot++ % 4]; c.value = hex(random, sizeof random); c.issued = millis();
    server.send(200, "application/json", "{\"protocol\":\"inkos-config-v1\",\"challenge\":\"" + c.value + "\"}");
  });
  server.on("/api/command", HTTP_POST, [&server]() {
    server.sendHeader("Cache-Control", "no-store");
    if (!enabled) { server.send(403, "application/json", "{}"); return; }
    String body = server.arg("plain");
    if (body.length() > 7000) { server.send(413); return; }
    auto envelope = cJSON_Parse(body.c_str());
    String challenge = field(envelope, "challenge"); Challenge *found = nullptr;
    for (auto &c : challenges)
      if (c.value.length() && c.value == challenge && uint32_t(millis() - c.issued) < 30000) found = &c;
    std::vector<uint8_t> nonce, ciphertext, tag;
    bool valid = found && decode(field(envelope, "nonce"), nonce, 12) && nonce.size() == 12 &&
                 decode(field(envelope, "ciphertext"), ciphertext, 4095) &&
                 decode(field(envelope, "tag"), tag, 16) && tag.size() == 16;
    cJSON_Delete(envelope);
    if (!valid) { server.send(401, "application/json", "{}"); return; }
    String aad = "inkos-config-v1|" + challenge + "|request";
    std::vector<uint8_t> plain(ciphertext.size() + 1);
    mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!rc) rc = mbedtls_gcm_auth_decrypt(&ctx, ciphertext.size(), nonce.data(), nonce.size(),
      (const uint8_t *)aad.c_str(), aad.length(), tag.data(), tag.size(), ciphertext.data(), plain.data());
    if (rc) { mbedtls_gcm_free(&ctx); server.send(401, "application/json", "{}"); return; }
    // Consume before executing; a retry needs a fresh challenge and cannot replay a write.
    found->value = "";
    plain[ciphertext.size()] = 0;
    auto command = cJSON_ParseWithLength((const char *)plain.data(), ciphertext.size());
    String response = executeCommand(command, false); cJSON_Delete(command);
    memset(plain.data(), 0, plain.size());
    uint8_t responseNonce[12], responseTag[16]; esp_fill_random(responseNonce, sizeof responseNonce);
    std::vector<uint8_t> encrypted(response.length());
    aad = "inkos-config-v1|" + challenge + "|response";
    rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, response.length(), responseNonce, sizeof responseNonce,
      (const uint8_t *)aad.c_str(), aad.length(), (const uint8_t *)response.c_str(), encrypted.data(), sizeof responseTag, responseTag);
    mbedtls_gcm_free(&ctx);
    if (rc) { server.send(500); return; }
    auto out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "nonce", base64(responseNonce, sizeof responseNonce).c_str());
    cJSON_AddStringToObject(out, "ciphertext", base64(encrypted.data(), encrypted.size()).c_str());
    cJSON_AddStringToObject(out, "tag", base64(responseTag, sizeof responseTag).c_str());
    char *raw = cJSON_PrintUnformatted(out);
    server.send(raw ? 200 : 500, "application/json", raw ? raw : "{}");
    free(raw); cJSON_Delete(out);
  });
}
}
