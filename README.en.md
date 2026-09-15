# meatcan

[한국어](README.md) | [English](README.en.md)

`meatcan` is a C++ CLI for using the **MeatPi Ollie v2 GS USB CAN firmware** on macOS. It talks to the adapter through libusb and does not create a SocketCAN network interface on macOS.

Target device: [MeatPi Ollie v2](https://github.com/meatpiHQ/meatpi_ollie_v2), USB ID `1209:2323`. The SLCAN serial firmware is not supported. The current scope is Classic CAN data frames with 11-bit or 29-bit IDs and up to 8 data bytes. CAN FD and RTR frames are not supported.

## Build from source

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

To install it on your PATH:

```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

Add the final PATH line to your shell configuration to keep it in new terminal sessions. Rebuild and reinstall to update a manually installed binary.

## Usage

```sh
# Start the background daemon. It waits if the adapter is unavailable.
meatcan up --bitrate 500k
meatcan status

# Terminal 1: receive frames. Ctrl+C stops only this subscription.
meatcan dump

# Terminal 2: send one frame and show the confirmed frame.
meatcan send 123#01020304

# Stop the CAN controller and background daemon.
meatcan down

# Show help.
meatcan -h
```

The default bitrate for `up` is **25 kbps**. Values such as `25k`, `250k`, `500k`, and `1m` are accepted. A bitrate that cannot be produced from the adapter clock and timing limits is rejected. After starting the daemon, `up` prints the connection state, backend, bitrate, traffic counters, and last error in an aligned status block. Run `down` before changing the bitrate.

`send` accepts a hexadecimal CAN ID followed by an even-length hexadecimal payload of up to 16 characters. Use `123#` for an empty payload. An extended-ID example is `1ABCDEFF#01020304`. A send waits for the adapter's matching echo. Success returns exit code `0`; an error or timeout returns `1`. An echo timeout does not prove whether the other application processed the frame, so an uncertain transmission is never replayed automatically.

On success, `send` prints `MeatCAN TX complete` and the frame whose echo was confirmed.

### Repeated and continuous transmission

```sh
# Send 10 frames with a 100 ms interval.
meatcan send -r 10 -i 100ms 123#01020304

# Send every second until Ctrl+C.
meatcan send -c -i 1s 123#01020304

# Send 100 frames without successful output.
meatcan send -q -r 100 123#01020304

# Show send-specific help.
meatcan send -h
```

- `-r, --repeat <count>` sends the same frame the specified total number of times.
- `-c, --continuous` sends until `Ctrl+C`. Its default interval is 100 ms when `-i` is omitted.
- `-i, --interval <time>` sets the delay between frames. It accepts `500us`, `100ms`, and `1s`. A value without a unit is interpreted as milliseconds.
- `-q, --quiet` hides successful output and summaries while keeping errors visible.

Continuous transmission can significantly increase CAN bus utilization. Start on a test bus and use `-i` to set an appropriate interval.

### Bitrate scanning

`scan` applies candidate bitrates one at a time in listen-only mode and detects the rate that receives valid CAN frames. It does not transmit frames, ACKs, or Error Frames while scanning. Scanning cannot run while `meatcan up` owns the adapter, so run `meatcan down` first.

```sh
# Stop the daemon first if it is running.
meatcan down

# Scan all default candidates.
meatcan scan

# Scan only selected candidates.
meatcan scan --rates 125k,250k,500k

# Listen to one candidate for three seconds.
meatcan scan --rate 500k --timeout 3s

# Accept one valid frame as a match.
meatcan scan --rate 25k --min-frames 1

# Show scan-specific help.
meatcan scan -h
```

The default candidates, in order, are `10k, 20k, 25k, 50k, 100k, 125k, 250k, 500k, 800k, 1m`. The default listening window is one second per candidate. The first candidate receiving two valid frames is reported as a match, and scanning stops. Specifying `--rates` or `--rate` replaces the default list with your candidates.

- `-r, --rates <list>` sets comma-separated candidate rates.
- `--rate <rate>` adds one candidate and can be repeated.
- `-t, --timeout <time>` sets the listening window per candidate. Use a positive integer with `s`, `ms`, or `us`, such as `3s`, `500ms`, or `500000us`. A bare number means milliseconds. The maximum is 24 hours.
- `-m, --min-frames <count>` sets the number of valid frames required to confirm a match.

Another node must actively transmit frames while scanning. Since MeatPi does not acknowledge frames in listen-only mode, scan a bus where a transmitter and another normal node can already communicate and acknowledge traffic. If MeatPi is the transmitter's only peer, missing ACKs can cause retransmissions or error states that affect detection.

Increase `--timeout` for infrequent messages. Wiring, termination, or CAN H/L problems can prevent detection; no match does not prove that the bitrate is wrong. `--min-frames` counts valid receptions, not distinct messages. A match does not establish long-term bus reliability.

Scanning opens the adapter directly and cannot run alongside a daemon or another scan. It fails if the adapter is absent or a candidate bitrate cannot be configured exactly. USB initialization time is outside `--timeout`; an in-progress USB read may extend the listening window by about 10 ms. Exit codes are `0` for a match, `1` for no match or an error, and `130` for `Ctrl+C`. Scanning stops CAN when it exits and does not automatically start a daemon at the detected rate. Use the printed `meatcan up --bitrate ...` command to start normal operation.

`State waiting` and `Adapter waiting` mean the daemon is waiting for the USB adapter or for initialization. `State ready` and `Adapter connected` mean USB and CAN initialization completed. `down` reports the state, adapter, bitrate, traffic counters, and last error from immediately before shutdown. A `ready` state does not guarantee correct bus wiring or an active peer. Stop any Python program that directly owns the same GS USB adapter before running `meatcan`.

## Architecture

```text
meatcan dump ── receive subscription ──┐
                                      ├── daemon ── libusb ── MeatPi
meatcan send ── transmit request ──────┘

# Only while the daemon is stopped
meatcan scan ── libusb (listen-only) ── MeatPi
```

One daemon owns the USB adapter. Multiple terminals can use `dump` and `send`, and stopping a `dump` subscription leaves CAN running. `down` stops CAN. In normal CAN mode, the controller can acknowledge valid frames even when no dump subscriber is active.

The runtime directory is `/tmp/meatcan-<user UID>` with mode `0700`. It contains `daemon.sock` and `daemon.log`. Development and tests can isolate a runtime by setting the same `MEATCAN_RUNTIME_DIR` for every command. Installation does not configure automatic login startup.

## Homebrew installation and updates

The repository currently provides a HEAD formula that builds the `main` branch. Homebrew 7 requires the scoped formula trust command before adding the tap. Skip the first line on older Homebrew versions that do not provide `brew trust`.

```sh
brew trust --formula 42kko/meatcan/meatcan
brew tap 42kko/meatcan https://github.com/42kko/MeatPI_CAN_MACOS.git
brew install --HEAD 42kko/meatcan/meatcan
```

To update:

```sh
meatcan down
brew update
brew upgrade --fetch-HEAD 42kko/meatcan/meatcan
meatcan --version
meatcan up --bitrate 500k
```

To remove it, run `meatcan down` followed by `brew uninstall meatcan`. This repository acts as a custom tap and is not registered in the official Homebrew repositories. See the [maintenance guide](docs/MAINTAINING.md) for stable release procedures.

## Troubleshooting

- **Waiting for an adapter:** Verify the GS USB firmware, USB connection, and USB ID `1209:2323`. No other program should own the adapter.
- **TX timeout:** Check that the peer is active and uses the same bitrate. Verify CAN H/L, the common reference potential, and termination at both ends of the bus. Let the application decide whether to retry an uncertain transmission.
- **USB overflow or disconnect:** Inspect `daemon.log`. USB buffer size and CAN frame size are separate concepts. The implementation validates the known GS USB layout and received byte count instead of assuming one frame per USB packet.
- **Old process after an update:** Run `down` before updating and `up` after the update.

Physical receive, transmit, sustained traffic, and reconnection tests remain separate from mock tests. `--mock` exists for automated testing and never transmits on a real CAN bus.

## Development and release

- C++17, CMake, and libusb
- Tests: `ctest --test-dir build --output-on-failure`
- GitHub Actions: macOS build and hardware-free tests
- Homebrew formula: `Formula/meatcan.rb`
- Version and release process: [docs/MAINTAINING.md](docs/MAINTAINING.md)
