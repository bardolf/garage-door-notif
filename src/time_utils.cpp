#include "time_utils.h"

#include "config.h"
#include "storage.h"

bool time_is_synced() {
  time_t now;
  time(&now);
  return now > NTP_VALID_EPOCH_THRESHOLD;
}

void now_local(struct tm& out) {
  time_t now;
  time(&now);
  localtime_r(&now, &out);
}

void format_time(char* buf, size_t buf_size, const char* fmt) {
  if (buf_size == 0) return;
  struct tm tm_local;
  now_local(tm_local);
  strftime(buf, buf_size, fmt, &tm_local);
}

int current_hour() {
  struct tm tm_local;
  now_local(tm_local);
  return tm_local.tm_hour;
}

int current_yday() {
  struct tm tm_local;
  now_local(tm_local);
  return tm_local.tm_yday;
}

bool is_night_window() {
  int h = current_hour();
  return (h >= cfg.night_start_hour || h < cfg.night_end_hour);
}
