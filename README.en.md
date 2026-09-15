# meatcan

[한국어](README.md) | [English](README.en.md)

**A GS CAN (`gs_usb`) CLI for MeatPi Ollie v2 on macOS.** macOS has no Linux SocketCAN interface, so meatcan uses C++ and libusb to access the GS USB CAN firmware directly.

- Target: [MeatPi Ollie v2](https://github.com/meatpiHQ/meatpi_ollie_v2), USB ID `1209:2323`
- Supported: Classic CAN data frames, 11/29-bit IDs, up to 8 data bytes; receiving and displaying RTR frames
- Not supported: SLCAN firmware, CAN FD, RTR transmission
- Running meatcan requires neither Python nor a virtual environment.

## Install

Homebrew builds and installs the latest code from `main`. Homebrew 7 requires the scoped trust command below. Skip the first line on older versions without `brew trust`.

```sh
brew trust --formula 42kko/meatcan/meatcan
brew tap 42kko/meatcan https://github.com/42kko/MeatPI_CAN_MACOS.git
brew install --HEAD 42kko/meatcan/meatcan
```

Stop CAN before updating:

```sh
meatcan down
brew update
brew upgrade --fetch-HEAD 42kko/meatcan/meatcan
meatcan --version
```

To uninstall, run `meatcan down` followed by `brew uninstall meatcan`.

## Send and receive

Start CAN at the same bitrate as the peer. The default is **25 kbps**.

```sh
meatcan up --bitrate 500k
meatcan status
```

Receive in one terminal and send from another:

```sh
# Terminal 1: display received frames
meatcan dump

# Terminal 2: send one frame
meatcan send 123#01020304

# Stop CAN
meatcan down
```

Pressing `Ctrl+C` in `dump` leaves CAN running. Use `down` to stop CAN itself. To change the bitrate, run `down`, then `up` with the new rate.

### Frame format

Use `CAN_ID#DATA`. The ID and payload are hexadecimal. Payload bytes use two digits each, up to 16 digits (8 bytes).

```sh
meatcan send 123#01020304        # Standard ID
meatcan send 1ABCDEFF#AABBCCDD   # Extended ID
meatcan send 123#               # Empty payload
```

After the adapter returns a matching transmit-completion response (TX echo), the command prints `MeatCAN TX complete` and the frame. Success exits with code `0`; an error or timeout exits with `1`. Completion does not confirm processing by the peer application. Delivery can be uncertain after a timeout, so the CLI does not automatically replay the frame.

### Repeated and continuous transmission

```sh
meatcan send -r 10 -i 100ms 123#01020304  # Send 10 times
meatcan send -c -i 1s 123#01020304        # Repeat until Ctrl+C
meatcan send -q -r 100 123#01020304       # Hide successful output
```

| Option | Description |
|---|---|
| `-r, --repeat <count>` | Total number of transmissions |
| `-c, --continuous` | Repeat until interrupted; defaults to a 100 ms interval |
| `-i, --interval <time>` | Delay after TX completion before the next transmission: `500us`, `100ms`, `1s`; bare numbers mean ms |
| `-q, --quiet` | Hide successful output and summaries; still display errors |

Continuous transmission increases bus utilization. Use an appropriate interval on a test bus.

## Find the bitrate

Stop any running CAN daemon with `meatcan down` first. `scan` owns the adapter directly and cannot run alongside another scan or the CAN daemon.

| Mode | When to use it | Bus behavior |
|---|---|---|
| `scan` | A peer is transmitting; also works with two nodes | ACKs valid frames; a wrong rate can produce Error Frames |
| `scan --passive` | Observing two other nodes already communicating | Receives without transmitting frames, ACKs, or Error Frames |
| `scan --active` | A peer only acknowledges and does not transmit | Sends a probe; a wrong rate can cause retransmissions and Error Frames |

```sh
meatcan scan                                  # Default candidates
meatcan scan --rates 125k,250k,500k           # Selected candidates
meatcan scan --passive --rates 125k,250k,500k # Receive without responding
meatcan scan --rate 500k --timeout 3s         # Allow more time at one rate
meatcan scan --active --rate 500k             # Check with a probe
```

Default candidate order: `500k, 250k, 125k, 1m, 800k, 100k, 50k, 25k, 20k, 10k`. Default and passive modes listen for one second per candidate and stop at the first rate receiving one valid frame.

| Option | Description |
|---|---|
| `-r, --rates <list>` | Comma-separated candidates, replacing the default list |
| `--rate <rate>` | Add one candidate; may be repeated |
| `-t, --timeout <time>` | Listening time per candidate: positive integer with `s`, `ms`, or `us`; bare numbers mean ms; maximum 24 hours |
| `-m, --min-frames <count>` | Required valid receptions; defaults to 1; unused in active mode |

**Use active mode on a test bus with a narrow candidate list.** It requires `--rate` or `--rates`. For each candidate it requests one lowest-priority extended frame with an empty payload, then waits for its TX echo for the shorter of `--timeout` and 100 ms. The adapter does not support one-shot mode, so hardware may retransmit if no ACK arrives.

Keep these limits in mind:

- Default and passive modes need actual traffic. Passive mode sends no ACKs, which can prevent detection when only MeatPi and a transmitter are connected.
- Increase `--timeout` for infrequent messages. Set `--min-frames 2` or higher to require more receptions; they need not be distinct messages.
- Wiring, termination, and CAN H/L faults can also prevent detection. A match does not establish long-term reliability.
- Scan stops CAN when it exits. Use the printed `meatcan up --bitrate ...` command to start normal operation.
- Exit codes: match `0`, no match or error `1`, `Ctrl+C` interruption `130`.

## Status and troubleshooting

`up`, `status`, and `down` display the connection state, bitrate, traffic counters, and last error. `waiting` means adapter connection or initialization is pending; `ready` means USB/CAN initialization completed. It does not establish that the peer or wiring is working. An unsupported bitrate may leave the daemon waiting; check its last error.

| Symptom | Check |
|---|---|
| Waiting for adapter | GS USB firmware, USB cable, device ID `1209:2323`; close other programs using the same USB adapter |
| TX timeout | Active peer, matching bitrate, CAN H/L, common reference potential, termination at both ends |
| USB overflow or disconnect | Logs and USB connection; overflow alone does not prove excess CAN traffic |
| Old behavior after an update | Run `down`, then `up` again |

Daemon logs are in `/tmp/meatcan-<user UID>/daemon.log`. Scan errors appear in its terminal. See the [validation record](docs/VALIDATION.md) for hardware test coverage.

## Development

### Build from source

You need macOS, Xcode Command Line Tools, and Homebrew.

```sh
xcode-select --install  # Skip if already installed
brew install cmake libusb
git clone https://github.com/42kko/MeatPI_CAN_MACOS.git
cd MeatPI_CAN_MACOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/meatcan --help
```

To install on your PATH:

```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

Add the PATH setting to your shell configuration for future terminals. Rebuild and reinstall to update a manual installation.

### Architecture

```text
meatcan dump ── receive subscription ──┐
                                      ├── daemon ── libusb ── MeatPi
meatcan send ── transmit request ──────┘

meatcan scan ── libusb ── MeatPi  (while the daemon is stopped)
```

The implementation uses C++17 and libusb. The daemon keeps ownership of USB, so closing a receive view leaves CAN and ACK responses running. Installation does not configure automatic login startup.

`--mock` is a test-only mode without hardware access. See the [validation record](docs/VALIDATION.md) for USB initialization and scan test coverage.

- Help: `meatcan -h`, `meatcan send -h`, `meatcan scan -h`
- [Validation record](docs/VALIDATION.md)
- [Homebrew and release maintenance](docs/MAINTAINING.md)
