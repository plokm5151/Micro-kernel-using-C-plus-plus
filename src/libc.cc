#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void* dst, int c, size_t n) {
  auto* p = static_cast<uint8_t*>(dst);
  const uint8_t v = static_cast<uint8_t>(c);
  for (size_t i = 0; i < n; ++i) {
    p[i] = v;
  }
  return dst;
}

extern "C" void* memcpy(void* dst, const void* src, size_t n) {
  auto* d = static_cast<uint8_t*>(dst);
  const auto* s = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

extern "C" void* memmove(void* dst, const void* src, size_t n) {
  auto* d = static_cast<uint8_t*>(dst);
  const auto* s = static_cast<const uint8_t*>(src);
  if (d == s || n == 0) return dst;

  if (d < s) {
    for (size_t i = 0; i < n; ++i) {
      d[i] = s[i];
    }
    return dst;
  }

  for (size_t i = n; i != 0; --i) {
    d[i - 1] = s[i - 1];
  }
  return dst;
}

extern "C" int memcmp(const void* a, const void* b, size_t n) {
  const auto* p = static_cast<const uint8_t*>(a);
  const auto* q = static_cast<const uint8_t*>(b);
  for (size_t i = 0; i < n; ++i) {
    if (p[i] != q[i]) {
      return (p[i] < q[i]) ? -1 : 1;
    }
  }
  return 0;
}

