#include "protocol.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <libusb.h>
#include <mach-o/dyld.h>
#include <map>
#include <memory>
#include <signal.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace meatcan;
using Clock = std::chrono::steady_clock;
static volatile sig_atomic_t stopping = 0;
static void stop_signal(int) { stopping = 1; }
static std::string directory() {
  const char *e = getenv("MEATCAN_RUNTIME_DIR");
  std::string d = e ? e : "/tmp/meatcan-" + std::to_string(getuid());
  if (d.empty() || d[0] != '/')
    throw std::runtime_error("runtime directory must be absolute");
  if (mkdir(d.c_str(), 0700) < 0 && errno != EEXIST)
    throw std::runtime_error("cannot create runtime directory");
  struct stat st{};
  if (lstat(d.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
      (st.st_mode & 077))
    throw std::runtime_error(
        "runtime directory must be owned by you with mode 0700, not a symlink");
  return d;
}
static sockaddr_un address(const std::string &d) {
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  auto p = d + "/daemon.sock";
  if (p.size() >= sizeof(a.sun_path))
    throw std::runtime_error("runtime socket path too long");
  strcpy(a.sun_path, p.c_str());
  return a;
}
static int connect_to(const std::string &d) {
  auto a = address(d);
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0)
    return -1;
  if (connect(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) < 0) {
    close(s);
    return -1;
  }
  return s;
}
static void usb_check(int n, const char *op) {
  if (n < 0)
    throw std::runtime_error(std::string(op) + ": " + libusb_error_name(n));
}
class Usb {
  libusb_context *ctx = nullptr;
  libusb_device_handle *h = nullptr;
  bool claimed = false;
  bool started = false;
  int packet = 0;
  void control(const char *stage, uint8_t type, uint8_t req, uint8_t *b,
               uint16_t n) {
    int r = libusb_control_transfer(h, type, req, 0, 0, b, n, 1000);
    if (r < 0)
      throw std::runtime_error(
          std::string(stage) + " (USB control request=" + std::to_string(req) +
          " type=" + std::to_string(type) + "): " + libusb_error_name(r));
    if (r != n)
      throw std::runtime_error(std::string(stage) +
                               ": short USB control transfer");
  }

public:
  Usb() { usb_check(libusb_init(&ctx), "libusb_init"); }
  ~Usb() {
    disconnect();
    libusb_exit(ctx);
  }
  bool connected() const { return h; }
  void disconnect() {
    if (h) {
      if (claimed) {
        if (started) {
          uint8_t b[8]{};
          libusb_control_transfer(h, 0x41, 2, 0, 0, b, 8, 1000);
        }
        libusb_release_interface(h, 0);
      }
      libusb_close(h);
    }
    h = nullptr;
    claimed = false;
    started = false;
  }
  bool open(uint32_t rate) {
    libusb_device **list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    usb_check(int(count), "USB enumerate");
    libusb_device *chosen = nullptr;
    int matches = 0;
    for (ssize_t i = 0; i < count; i++) {
      libusb_device_descriptor desc{};
      if (libusb_get_device_descriptor(list[i], &desc) == 0 &&
          desc.idVendor == 0x1209 && desc.idProduct == 0x2323) {
        chosen = list[i];
        matches++;
      }
    }
    if (matches > 1) {
      libusb_free_device_list(list, 1);
      throw std::runtime_error(
          "multiple matching adapters; connect exactly one");
    }
    if (!chosen) {
      libusb_free_device_list(list, 1);
      return false;
    }
    int r = libusb_open(chosen, &h);
    libusb_free_device_list(list, 1);
    usb_check(r, "USB open");
    try {
      int cfg = 0;
      usb_check(libusb_get_configuration(h, &cfg), "get configuration");
      if (!cfg)
        usb_check(libusb_set_configuration(h, 1), "set configuration");
      usb_check(libusb_claim_interface(h, 0),
                "claim interface 0 (close other CAN programs)");
      claimed = true;
      packet = libusb_get_max_packet_size(libusb_get_device(h), 0x81);
      usb_check(packet, "IN endpoint packet size");
      if (packet < 20 || packet > 4096)
        throw std::runtime_error("unexpected endpoint packet size");
      // Match the working Ollie Python initialization: capabilities, timing,
      // then START. That path omits HOST_FORMAT and the pre-start channel
      // reset / endpoint drain. In particular, never reset the USB bus.
      uint8_t cb[40];
      control("read CAN capabilities", 0xc1, 4, cb, 40);
      std::array<uint32_t, 10> caps{};
      for (int i = 0; i < 10; i++)
        caps[i] = get32(cb + 4 * i);
      auto t = timing(caps, rate);
      uint8_t bt[20];
      put32(bt, 1);
      put32(bt + 4, t.tseg1 - 1);
      put32(bt + 8, t.tseg2);
      put32(bt + 12, 1);
      put32(bt + 16, t.brp);
      control("set CAN timing", 0x41, 1, bt, 20);
      uint8_t mode[8]{};
      put32(mode, 1);
      put32(mode + 4, 0);
      control("start CAN", 0x41, 2, mode, 8);
      started = true;
      return true;
    } catch (...) {
      disconnect();
      throw;
    }
  }
  void send_frame(const Frame &f) {
    auto b = encode(f);
    int n = 0;
    usb_check(libusb_bulk_transfer(h, 0x02, b.data(), int(b.size()), &n, 100),
              "USB TX (delivery may be uncertain)");
    if (n != int(b.size()))
      throw std::runtime_error("short USB TX; delivery uncertain");
  }
  std::vector<Frame> read() {
    std::vector<uint8_t> b(packet);
    int n = 0;
    int r = libusb_bulk_transfer(h, 0x81, b.data(), packet, &n, 10);
    if (r != LIBUSB_ERROR_TIMEOUT)
      usb_check(r, "USB RX");
    if (!n)
      return {};
    return decode(b.data(), size_t(n));
  }
};
struct Client {
  int fd;
  std::string in, out;
  bool subscriber = false, done = false;
  bool waiting = false;
  Clock::time_point since = Clock::now();
};
struct Pending {
  int fd;
  Frame f;
  Clock::time_point deadline;
};
static int daemon_main(const std::string &d, uint32_t rate, bool mock) {
  umask(0077);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, stop_signal);
  signal(SIGINT, stop_signal);
  int lock =
      open((d + "/daemon.lock").c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
  if (lock < 0)
    throw std::runtime_error("cannot open daemon lock: " +
                             std::string(strerror(errno)));
  if (flock(lock, LOCK_EX | LOCK_NB) < 0) {
    auto error = std::string(strerror(errno));
    close(lock);
    throw std::runtime_error("cannot lock daemon: " + error);
  }
  auto a = address(d);
  unlink(a.sun_path);
  int server = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server < 0 || bind(server, reinterpret_cast<sockaddr *>(&a), sizeof(a)) ||
      listen(server, 32)) {
    auto error = std::string(strerror(errno));
    if (server >= 0)
      close(server);
    close(lock);
    throw std::runtime_error("cannot create daemon socket: " + error);
  }
  fcntl(server, F_SETFL, O_NONBLOCK);
  std::map<int, Client> clients;
  std::map<uint32_t, Pending> pending;
  std::vector<std::pair<Clock::time_point, Frame>> mock_delayed;
  const char *mock_env = mock ? getenv("MEATCAN_MOCK_ECHO") : nullptr;
  std::string mock_echo = mock_env ? mock_env : "normal";
  uint32_t next_echo = 1;
  uint64_t rx = 0, tx = 0, dropped = 0;
  std::string state = "waiting", last_error = "none";
  Clock::time_point retry = Clock::now();
  Usb usb;
  auto reply = [&](int fd, std::string s) {
    auto it = clients.find(fd);
    if (it != clients.end()) {
      it->second.out += s + "\n";
      it->second.done = true;
    }
  };
  auto broadcast = [&](std::string s) {
    for (auto &kv : clients) {
      auto &c = kv.second;
      if (c.subscriber) {
        if (c.out.size() + s.size() > 65536) {
          c.done = true;
          c.subscriber = false;
          c.out = "ERR slow subscriber disconnected; frames dropped\n";
          dropped++;
        } else
          c.out += s + "\n";
      }
    }
  };
  auto fail_all = [&](std::string s) {
    for (auto &p : pending)
      reply(p.second.fd, "ERR " + s);
    pending.clear();
  };
  auto got = [&](const Frame &f) {
    if (f.flags & 1)
      broadcast("WARN device reports RX overflow");
    if (f.echo != no_echo) {
      auto it = pending.find(f.echo);
      if (it != pending.end()) {
        if (f.id != it->second.f.id || f.dlc != it->second.f.dlc ||
            f.data != it->second.f.data) {
          reply(it->second.fd, "ERR mismatched TX echo");
        } else {
          reply(it->second.fd, "OK");
          tx++;
        }
        pending.erase(it);
      }
      return;
    }
    rx++;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    broadcast("(" + std::to_string(us / 1000000) + "." +
              std::to_string(1000000 + us % 1000000).substr(1) + ") " +
              ((f.id & 0x20000000u) ? "ERROR " : "can0 ") + format(f));
  };
  while (!stopping) {
    auto now = Clock::now();
    if (state != "ready" && now >= retry) {
      try {
        if (mock || usb.open(rate)) {
          state = "ready";
          last_error = "none";
          broadcast("STATE ready");
        }
      } catch (const std::exception &e) {
        last_error = e.what();
        std::cerr << last_error << '\n';
      }
      retry = now + std::chrono::seconds(1);
    }
    for (;;) {
      int fd = accept(server, nullptr, nullptr);
      if (fd < 0)
        break;
      if (clients.size() >= 64) {
        close(fd);
        continue;
      }
      fcntl(fd, F_SETFL, O_NONBLOCK);
      clients.emplace(fd,
                      Client{fd, {}, {}, false, false, false, Clock::now()});
    }
    for (auto &kv : clients) {
      auto &c = kv.second;
      if (!c.done && !c.subscriber && !c.waiting) {
        char buf[256];
        ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
        if (n == 0) {
          c.done = true;
        } else if (n > 0) {
          c.in.append(buf, size_t(n));
          if (c.in.size() > 256) {
            reply(c.fd, "ERR command too long");
          } else if (c.in.find('\n') != std::string::npos) {
            auto cmd = c.in.substr(0, c.in.find('\n'));
            c.in.clear();
            if (cmd == "STATUS") {
              reply(c.fd, "OK state=" + state +
                              " adapter=" +
                              (state == "ready" ? "connected" : "waiting") +
                              " bitrate=" + std::to_string(rate) +
                              " mock=" + (mock ? "yes" : "no") + " rx=" +
                              std::to_string(rx) + " tx=" + std::to_string(tx) +
                              " slow_subscribers=" + std::to_string(dropped) +
                              " last_error=" + last_error);
            } else if (cmd == "DOWN") {
              auto previous_state = state;
              auto previous_adapter =
                  state == "ready" ? "connected" : "waiting";
              usb.disconnect();
              state = "stopped";
              reply(c.fd, "OK previous_state=" + previous_state +
                              " adapter=" + previous_adapter + " bitrate=" +
                              std::to_string(rate) + " rx=" +
                              std::to_string(rx) + " tx=" +
                              std::to_string(tx) + " last_error=" + last_error);
              stopping = 1;
            } else if (cmd == "DUMP") {
              c.subscriber = true;
              c.out = "OK\n";
            } else if (cmd.rfind("SEND ", 0) == 0) {
              try {
                if (state != "ready")
                  throw std::runtime_error(
                      "device not ready (waiting for USB)");
                if (pending.size() >= 10)
                  throw std::runtime_error("TX queue full");
                Frame f = parse(cmd.substr(5));
                if (next_echo == no_echo)
                  throw std::runtime_error(
                      "echo IDs exhausted; restart daemon");
                f.echo = next_echo++;
                if (!mock)
                  usb.send_frame(f);
                c.waiting = true;
                pending.emplace(
                    f.echo,
                    Pending{c.fd, f, Clock::now() + std::chrono::seconds(2)});
                c.since = Clock::now();
                if (mock) {
                  if (mock_echo == "delay") {
                    mock_delayed.emplace_back(
                        Clock::now() + std::chrono::milliseconds(100), f);
                  } else if (mock_echo != "drop") {
                    if (mock_echo == "mismatch")
                      f.id ^= 1;
                    got(f);
                  }
                  f.echo = no_echo;
                  got(f);
                }
              } catch (const std::exception &e) {
                reply(c.fd, "ERR " + std::string(e.what()));
              }
            } else
              reply(c.fd, "ERR unknown command");
          }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
          c.done = true;
        }
      }
      // Evict disconnected subscribers even when no CAN data is arriving.
      if (c.subscriber || c.waiting) {
        char byte;
        ssize_t n = recv(c.fd, &byte, 1, MSG_PEEK);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
          c.subscriber = false;
          c.done = true;
          c.out.clear();
        }
      }
      if (!c.subscriber && !c.done &&
          Clock::now() - c.since > std::chrono::seconds(5))
        reply(c.fd, "ERR request timeout");
    }
    if (!mock && usb.connected()) {
      try {
        for (auto &f : usb.read())
          got(f);
      } catch (const std::exception &e) {
        last_error = e.what();
        std::cerr << last_error << '\n';
        fail_all("USB failure; delivery uncertain, not replayed: " +
                 last_error);
        usb.disconnect();
        state = "waiting";
        broadcast("STATE waiting " + last_error);
        retry = Clock::now() + std::chrono::seconds(1);
      }
    }
    for (auto it = mock_delayed.begin(); it != mock_delayed.end();) {
      if (Clock::now() >= it->first) {
        got(it->second);
        it = mock_delayed.erase(it);
      } else
        ++it;
    }
    for (auto it = pending.begin(); it != pending.end();) {
      if (Clock::now() >= it->second.deadline) {
        reply(it->second.fd, "ERR TX echo timeout; CAN delivery unknown, not "
                             "replayed (check ACK, bitrate, termination)");
        it = pending.erase(it);
      } else
        ++it;
    }
    for (auto it = clients.begin(); it != clients.end();) {
      auto &c = it->second;
      if (!c.out.empty()) {
        ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), 0);
        if (n > 0)
          c.out.erase(0, size_t(n));
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          c.done = true;
          c.out.clear();
        }
      }
      if (c.done && c.out.empty()) {
        for (auto p = pending.begin(); p != pending.end();) {
          if (p->second.fd == c.fd)
            p = pending.erase(p);
          else
            ++p;
        }
        close(c.fd);
        it = clients.erase(it);
      } else
        ++it;
    }
    if (mock || !usb.connected())
      usleep(10000);
  }
  fail_all("daemon stopping; delivery may be uncertain");
  for (auto &kv : clients) {
    if (!kv.second.out.empty())
      ::send(kv.first, kv.second.out.data(), kv.second.out.size(), 0);
    close(kv.first);
  }
  usb.disconnect();
  close(server);
  unlink(a.sun_path);
  flock(lock, LOCK_UN);
  close(lock);
  return 0;
}
static int request(const std::string &d, const std::string &cmd,
                   bool stream = false) {
  int fd = connect_to(d);
  if (fd < 0)
    throw std::runtime_error(
        "daemon not running; use meatcan up --bitrate 500k");
  timeval tv{5, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  std::string q = cmd + "\n";
  if (::send(fd, q.data(), q.size(), 0) != ssize_t(q.size())) {
    close(fd);
    throw std::runtime_error("IPC send failed");
  }
  std::string line;
  bool handshake = false;
  while (true) {
    char c;
    ssize_t n = recv(fd, &c, 1, 0);
    if (n <= 0) {
      if (stream && handshake && n < 0 &&
          (errno == EAGAIN || errno == EWOULDBLOCK))
        continue;
      close(fd);
      if (stream && handshake)
        return 0;
      throw std::runtime_error("daemon connection ended or timed out");
    }
    if (c != '\n') {
      if (line.size() > 65536) {
        close(fd);
        throw std::runtime_error("invalid daemon response");
      }
      line += c;
      continue;
    }
    if (!handshake) {
      if (line.rfind("ERR ", 0) == 0) {
        close(fd);
        throw std::runtime_error(line.substr(4));
      }
      if (line.rfind("OK", 0) != 0) {
        close(fd);
        throw std::runtime_error("invalid daemon response");
      }
      if (!stream) {
        if (cmd == "STATUS")
          std::cout << line.substr(3) << '\n';
        else if (cmd == "DOWN") {
          auto stopped = line.substr(3);
          close(fd);
          auto socket_path = d + "/daemon.sock";
          for (int i = 0; i < 200; i++) {
            struct stat socket_status {};
            if (lstat(socket_path.c_str(), &socket_status) < 0 &&
                errno == ENOENT) {
              std::cout << "CAN stopped: " << stopped << '\n';
              return 0;
            }
            usleep(10000);
          }
          throw std::runtime_error("daemon did not finish stopping");
        }
        close(fd);
        return 0;
      }
      handshake = true;
    } else {
      std::cout << line << std::endl;
      if (line.rfind("ERR ", 0) == 0) {
        close(fd);
        return 1;
      }
    }
    line.clear();
  }
}
static void help() {
  std::cout
      << "meatcan 0.1.0 — macOS MeatPi Ollie v2 GS USB Classic CAN\nUsage:\n  "
         "meatcan up [-b|--bitrate 25k] [--mock]\n  meatcan dump\n  meatcan "
         "send 123#01020304\n  meatcan status\n  meatcan down\n  meatcan "
         "--version\n--mock is a test-only loopback backend, never real "
         "CAN.\nNormal mode acknowledges CAN even without dump subscribers.\n";
}
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  try {
    if (argc < 2 || std::string(argv[1]) == "--help") {
      help();
      return 0;
    }
    std::string cmd = argv[1];
    if (cmd == "--version") {
      std::cout << "meatcan 0.1.0\n";
      return 0;
    }
    auto d = directory();
    if (cmd == "up" || cmd == "__daemon") {
      uint32_t rate = 25000;
      bool mock = false;
      for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if ((a == "--bitrate" || a == "-b") && i + 1 < argc)
          rate = bitrate(argv[++i]);
        else if (a == "--mock")
          mock = true;
        else
          throw std::runtime_error("unknown or incomplete option: " + a);
      }
      if (cmd == "__daemon")
        return daemon_main(d, rate, mock);
      int existing = connect_to(d);
      if (existing >= 0) {
        close(existing);
        throw std::runtime_error("daemon already running; use status, or down "
                                 "before changing bitrate");
      }
      uint32_t size = 4096;
      std::vector<char> path(size);
      if (_NSGetExecutablePath(path.data(), &size)) {
        path.resize(size);
        if (_NSGetExecutablePath(path.data(), &size))
          throw std::runtime_error("cannot resolve executable");
      }
      pid_t child = fork();
      if (child < 0)
        throw std::runtime_error("fork failed");
      if (child == 0) {
        setsid();
        pid_t grandchild = fork();
        if (grandchild < 0)
          _exit(1);
        if (grandchild > 0)
          _exit(0);
        int log = open((d + "/daemon.log").c_str(),
                       O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
        int null = open("/dev/null", O_RDONLY);
        if (log < 0 || null < 0)
          _exit(1);
        dup2(null, 0);
        dup2(log, 1);
        dup2(log, 2);
        close(null);
        close(log);
        std::string b = std::to_string(rate);
        if (mock)
          execl(path.data(), path.data(), "__daemon", "--bitrate", b.c_str(),
                "--mock", nullptr);
        else
          execl(path.data(), path.data(), "__daemon", "--bitrate", b.c_str(),
                nullptr);
        _exit(1);
      }
      int status = 0;
      waitpid(child, &status, 0);
      for (int i = 0; i < 100; i++) {
        int fd = connect_to(d);
        if (fd >= 0) {
          close(fd);
          std::cout << "meatcan daemon started"
                    << (mock ? " (MOCK)" : "") << "\n";
          return request(d, "STATUS");
        }
        usleep(20000);
      }
      throw std::runtime_error("daemon failed to start; see " + d +
                               "/daemon.log");
    }
    if (cmd == "send") {
      if (argc != 3)
        throw std::runtime_error("usage: meatcan send ID#HEX");
      parse(argv[2]);
      return request(d, "SEND " + std::string(argv[2]));
    }
    if (argc != 2)
      throw std::runtime_error("unexpected arguments");
    if (cmd == "dump")
      return request(d, "DUMP", true);
    if (cmd == "status")
      return request(d, "STATUS");
    if (cmd == "down")
      return request(d, "DOWN");
    throw std::runtime_error("unknown command; use --help");
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    return 1;
  }
}
