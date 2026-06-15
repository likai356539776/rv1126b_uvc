#include "app_log.h"

int g_app_log_level = 3;

void app_log_init_from_env(void) {
  const char* e = getenv("MY_APP_LOG_LEVEL");
  if (!e || !*e) {
    return;
  }
  int v = atoi(e);
  if (v >= 1 && v <= 4) {
    g_app_log_level = v;
  }
}
