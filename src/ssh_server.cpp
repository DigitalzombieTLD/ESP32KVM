// SSH server built on LibSSH-ESP32 (see its samplesshd-kbdint example).
#include "ssh_server.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <string.h>
#include <sys/reent.h>

#include "libssh_esp32.h"
#include "libssh_esp32_config.h"
#include <libssh/libssh.h>
#include <libssh/server.h>

#include "settings.h"
#include "shell.h"
#include "term.h"

// Required by the LibSSH-ESP32 port: libssh runs in its own task with its own reent struct.
struct _reent reent_data_esp32;
struct _reent *_impure_ptr = &reent_data_esp32;

static const char *HOSTKEY_PATH = "/spiffs/.ssh/id_ed25519";
static const int MAX_AUTH_ATTEMPTS = 3;
static const unsigned SSH_STACK = 20480;

// Constant-time comparison so password checks don't leak length/prefix timing.
static bool secureEquals(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  uint8_t diff = (uint8_t)(la != lb);
  size_t n = la > lb ? la : lb;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)((i < la ? a[i] : 0) ^ (i < lb ? b[i] : 0));
  return diff == 0;
}

class SshTerm : public Term {
 public:
  explicit SshTerm(ssh_channel c) : chan_(c) {}
  int rawWrite(const char *data, size_t len) override {
    int r = ssh_channel_write(chan_, data, len);
    return r;
  }
  int rawRead(char *buf, size_t len, int timeoutMs) override {
    int r = ssh_channel_read_timeout(chan_, buf, len, 0, timeoutMs);
    if (r > 0) return r;
    if (r == SSH_ERROR) return -1;
    if (ssh_channel_is_eof(chan_) || !ssh_channel_is_open(chan_)) return -1;
    return 0;
  }

 private:
  ssh_channel chan_;
};

static bool ensureHostKey() {
  if (!SPIFFS.exists("/.ssh/id_ed25519")) {
    Serial.println("[ssh] generating ed25519 host key (first boot)...");
    ssh_key key = NULL;
    if (ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key) != SSH_OK) {
      Serial.println("[ssh] key generation failed");
      return false;
    }
    int rc = ssh_pki_export_privkey_file(key, NULL, NULL, NULL, HOSTKEY_PATH);
    ssh_key_free(key);
    if (rc != SSH_OK) {
      Serial.println("[ssh] could not write host key");
      return false;
    }
    Serial.println("[ssh] host key stored in SPIFFS");
  }
  return true;
}

static bool authenticate(ssh_session session) {
  int failures = 0;
  while (failures < MAX_AUTH_ATTEMPTS) {
    ssh_message msg = ssh_message_get(session);
    if (!msg) return false;
    bool ok = false;
    if (ssh_message_type(msg) == SSH_REQUEST_AUTH &&
        ssh_message_subtype(msg) == SSH_AUTH_METHOD_PASSWORD) {
      const char *u = ssh_message_auth_user(msg);
      const char *p = ssh_message_auth_password(msg);
      // evaluate both so timing doesn't reveal which one was wrong
      bool uok = secureEquals(u ? u : "", cfg.sshUser.c_str());
      bool pok = secureEquals(p ? p : "", cfg.sshPass.c_str());
      ok = uok && pok;
      if (ok) {
        ssh_message_auth_reply_success(msg, 0);
        ssh_message_free(msg);
        return true;
      }
      failures++;
      Serial.printf("[ssh] failed login attempt %d/%d\n", failures, MAX_AUTH_ATTEMPTS);
      delay(1000);
    }
    // password is the only accepted method
    ssh_message_auth_set_methods(msg, SSH_AUTH_METHOD_PASSWORD);
    ssh_message_reply_default(msg);
    ssh_message_free(msg);
  }
  return false;
}

static ssh_channel waitForChannel(ssh_session session) {
  while (true) {
    ssh_message msg = ssh_message_get(session);
    if (!msg) return NULL;
    if (ssh_message_type(msg) == SSH_REQUEST_CHANNEL_OPEN &&
        ssh_message_subtype(msg) == SSH_CHANNEL_SESSION) {
      ssh_channel c = ssh_message_channel_request_open_reply_accept(msg);
      ssh_message_free(msg);
      return c;
    }
    ssh_message_reply_default(msg);
    ssh_message_free(msg);
  }
}

// Handles pty/env/window-change until a shell or exec request arrives.
// Returns 1 for shell, 2 for exec (command copied to execCmd), 0 on failure.
static int waitForShell(ssh_session session, String &execCmd) {
  while (true) {
    ssh_message msg = ssh_message_get(session);
    if (!msg) return 0;
    int result = -1;
    if (ssh_message_type(msg) == SSH_REQUEST_CHANNEL) {
      switch (ssh_message_subtype(msg)) {
        case SSH_CHANNEL_REQUEST_PTY:
          ssh_message_channel_request_reply_success(msg);
          break;
        case SSH_CHANNEL_REQUEST_SHELL:
          ssh_message_channel_request_reply_success(msg);
          result = 1;
          break;
        case SSH_CHANNEL_REQUEST_EXEC: {
          const char *c = ssh_message_channel_request_command(msg);
          execCmd = c ? c : "";
          ssh_message_channel_request_reply_success(msg);
          result = 2;
          break;
        }
        default:
          ssh_message_reply_default(msg);
      }
    } else {
      ssh_message_reply_default(msg);
    }
    ssh_message_free(msg);
    if (result >= 0) return result;
  }
}

static void handleClient(ssh_session session) {
  if (ssh_handle_key_exchange(session)) {
    Serial.printf("[ssh] key exchange failed: %s\n", ssh_get_error(session));
    return;
  }
  if (!authenticate(session)) {
    Serial.println("[ssh] authentication failed");
    return;
  }
  Serial.println("[ssh] user authenticated");
  ssh_channel chan = waitForChannel(session);
  if (!chan) return;
  String execCmd;
  int mode = waitForShell(session, execCmd);
  if (mode) {
    SshTerm term(chan);
    if (mode == 1) {
      shellRun(term);
    } else {
      shellExecute(term, execCmd);
    }
  }
  ssh_channel_send_eof(chan);
  ssh_channel_close(chan);
  ssh_channel_free(chan);
}

static void sshTask(void *) {
  _REENT_INIT_PTR((&reent_data_esp32));
  libssh_begin();

  if (!SPIFFS.begin(true)) {
    Serial.println("[ssh] SPIFFS mount failed, SSH disabled");
    vTaskDelete(NULL);
    return;
  }
  if (!ensureHostKey()) {
    vTaskDelete(NULL);
    return;
  }

  ssh_bind bind = ssh_bind_new();
  char portStr[8];
  snprintf(portStr, sizeof(portStr), "%u", cfg.sshPort);
  ssh_bind_options_set(bind, SSH_BIND_OPTIONS_HOSTKEY, HOSTKEY_PATH);
  ssh_bind_options_set(bind, SSH_BIND_OPTIONS_BINDADDR, "0.0.0.0");
  ssh_bind_options_set(bind, SSH_BIND_OPTIONS_BINDPORT_STR, portStr);
  if (ssh_bind_listen(bind) < 0) {
    Serial.printf("[ssh] listen failed: %s\n", ssh_get_error(bind));
    vTaskDelete(NULL);
    return;
  }
  Serial.printf("[ssh] listening on port %s\n", portStr);

  while (true) {
    ssh_session session = ssh_new();
    long timeout = 30;  // seconds, covers handshake/auth of stalled clients
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
    if (ssh_bind_accept(bind, session) == SSH_OK) {
      Serial.println("[ssh] client connected");
      handleClient(session);
      ssh_disconnect(session);
      Serial.println("[ssh] client disconnected");
    } else {
      Serial.printf("[ssh] accept failed: %s\n", ssh_get_error(bind));
      delay(1000);
    }
    ssh_free(session);
  }
}

void sshServerStart() {
  xTaskCreatePinnedToCore(sshTask, "sshd", SSH_STACK, NULL, tskIDLE_PRIORITY + 3, NULL,
                          portNUM_PROCESSORS - 1);
}
