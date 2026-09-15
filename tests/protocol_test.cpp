#include "protocol.hpp"
#include <iostream>
using namespace meatcan;
void check(bool b) {
  if (!b)
    throw std::runtime_error("check failed");
}
template <class F> void rejects(F f) {
  bool bad = false;
  try {
    f();
  } catch (...) {
    bad = true;
  }
  check(bad);
}
int main() {
  check(bitrate("500k") == 500000);
  check(bitrate("1m") == 1000000);
  rejects([] { bitrate("2m"); });
  rejects([] { bitrate("-1"); });
  rejects([] { bitrate("18446744073709552k"); });
  auto f = parse("123#01020304");
  f.echo = 12345;
  auto remote = f;
  remote.id |= 0x40000000u;
  check(format(remote) == "123#R4");
  auto b = encode(f);
  auto out = decode(b.data(), b.size());
  check(out.size() == 1 && out[0].echo == 12345 &&
        format(out[0]) == "123#01020304");
  auto second = b;
  b.insert(b.end(), second.begin(), second.end());
  check(decode(b.data(), b.size()).size() == 2);
  rejects([&] { decode(b.data(), 39); });
  auto timestamped = encode(f);
  timestamped.resize(24);
  check(decode(timestamped.data(), 24, true).size() == 1);
  rejects([&] { decode(timestamped.data(), 24, false); });
  b[8] = 9;
  rejects([&] { decode(b.data(), 40); });
  rejects([] { parse("800#00"); });
  rejects([] { parse("123#0"); });
  check(format(parse("00000123#")) == "00000123#");
  std::array<uint32_t, 10> c{0, 36000000, 1, 16, 1, 8, 4, 1, 1024, 1};
  auto t = timing(c, 25000);
  check(t.brp == 90 && t.tseg1 == 13 && t.tseg2 == 2);
  for (auto rate :
       {10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000}) {
    auto x = timing(c, rate);
    check(uint64_t(rate) * x.brp * (1 + x.tseg1 + x.tseg2) == 36000000);
  }
  rejects([&] { timing(c, 999999); });
  std::cout << "protocol tests passed\n";
}
