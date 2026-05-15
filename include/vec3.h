// 3D vektor + zakladni linear-algebra operace pro accelerometr.
// Pouziva se na accel sample (Vec3 = (ax, ay, az) v g) i na baseline.
//
// Inline implementace — male funkce volane v hot path (kazdy wake cyklus),
// kompilator to spravne inlineuje a bez .cpp je o jeden TU mene.

#pragma once

#include <math.h>

struct Vec3 {
  float x, y, z;
};

inline float vec_mag(const Vec3& v) {
  return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

// 3D uhel mezi dvema vektory ve stupnich. Vraci 0 pri degenerate vstupech
// (NaN, nulova magnituda) — bezpecne i pri RTC mem corruption.
inline float angle_deg(const Vec3& a, const Vec3& b) {
  float ma = vec_mag(a), mb = vec_mag(b);
  if (isnan(ma) || isnan(mb) || ma < 1e-6f || mb < 1e-6f) return 0;
  float c = (a.x * b.x + a.y * b.y + a.z * b.z) / (ma * mb);
  if (isnan(c)) return 0;
  if (c > 1.0f)  c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 180.0f / static_cast<float>(PI);
}
