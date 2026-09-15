# meatcan

[한국어](README.md) | [English](README.en.md)

macOS에서 **MeatPi Ollie v2의 GS USB CAN 펌웨어**를 사용하는 C++ CLI입니다. libusb로 장치에 접근하며, macOS에 SocketCAN 네트워크 인터페이스를 만들지 않습니다.

설치된 `meatcan` 실행에는 Python이나 가상환경이 필요하지 않습니다. 저장소의 Python 파일은 자동 테스트와 Homebrew 릴리스 관리에만 사용됩니다.

대상 장치: [MeatPi Ollie v2](https://github.com/meatpiHQ/meatpi_ollie_v2), USB ID `1209:2323`. SLCAN 시리얼 펌웨어는 지원하지 않습니다. 현재 범위는 Classic CAN 데이터 프레임(11/29비트 ID, 최대 8바이트)이며 CAN FD와 RTR은 지원하지 않습니다.

## 빠른 시작: 소스 빌드

macOS, Xcode Command Line Tools, Homebrew가 필요합니다.

```sh
xcode-select --install  # 이미 설치되어 있으면 생략
brew install cmake libusb
git clone https://github.com/42kko/MeatPI_CAN_MACOS.git
cd MeatPI_CAN_MACOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/meatcan --help
```

PATH에 설치하려면:

```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

마지막 PATH 설정을 사용하는 셸의 설정 파일에 추가하면 다음 터미널에도 적용됩니다. 직접 설치한 실행파일의 갱신은 다시 빌드·설치해서 수행합니다.

## 사용법

```sh
# 백그라운드 프로세스 시작. 장치가 없으면 연결을 기다립니다.
meatcan up --bitrate 500k
meatcan status

# 터미널 1: 수신. Ctrl+C는 구독만 종료합니다.
meatcan dump

# 터미널 2: 한 프레임 송신. 성공하면 프레임을 확인해 줍니다.
meatcan send 123#01020304

# CAN 컨트롤러와 백그라운드 프로세스 종료
meatcan down

# 도움말
meatcan -h
```

`up`의 기본 속도는 **25 kbps**입니다. `25k`, `250k`, `500k`, `1m`처럼 지정할 수 있으며 장치의 클럭·타이밍 범위에서 만들 수 없는 속도는 오류로 보고합니다. `up`은 daemon 시작 후 연결 상태, backend, bitrate, 송수신 카운터와 마지막 오류를 정렬된 형식으로 바로 출력합니다. 속도를 바꾸려면 `down` 후 새 속도로 `up`을 실행하세요.

`send`는 16진수 CAN ID와 짝수 길이의 16진수 데이터(최대 16자리)를 받습니다. 빈 데이터는 `123#`입니다. 확장 ID 예시는 `1ABCDEFF#01020304`입니다. 송신은 장치의 고유 에코를 기다리며 성공은 종료 코드 `0`, 오류·타임아웃은 `1`입니다. 에코 타임아웃은 상대 애플리케이션의 처리 여부를 뜻하지 않으며, 완료가 불명확한 메시지를 자동 재전송하지 않습니다.

송신이 성공하면 `MeatCAN TX complete`과 에코가 확인된 프레임을 출력합니다.

### 반복·연속 송신

```sh
# 총 10회, 100 ms 간격으로 송신
meatcan send -r 10 -i 100ms 123#01020304

# Ctrl+C를 누를 때까지 1초마다 송신
meatcan send -c -i 1s 123#01020304

# 성공 출력 없이 100회 송신
meatcan send -q -r 100 123#01020304

# send 전용 도움말
meatcan send -h
```

- `-r, --repeat <count>`: 지정한 총 횟수만큼 같은 프레임을 송신합니다.
- `-c, --continuous`: `Ctrl+C`를 누를 때까지 계속 송신합니다. 간격을 생략하면 기본값은 100 ms입니다.
- `-i, --interval <time>`: 프레임 간 간격입니다. `500us`, `100ms`, `1s` 형식을 지원합니다. 단위가 없으면 ms로 해석합니다.
- `-q, --quiet`: 성공 메시지와 요약을 숨기고 오류는 계속 표시합니다.

연속 송신은 CAN 버스 점유율을 높일 수 있습니다. 테스트 버스에서 시작하고 `-i`로 필요한 간격을 지정하세요.

### Bitrate 탐색

`scan`은 기본적으로 일반 수신 모드로 후보 bitrate를 하나씩 적용하고 정상 CAN 프레임이 수신되는 속도를 찾습니다. 이 모드는 정상 프레임에 ACK하며, 잘못된 후보에서는 Error Frame으로 버스에 영향을 줄 수 있습니다. 버스에 전혀 응답하지 않으려면 `--passive`, 상대가 송신하지 않고 ACK만 한다면 제한된 후보와 `--active` probe를 사용하세요. `meatcan up` 상태에서는 실행할 수 없으므로 먼저 `meatcan down`을 실행하세요.

```sh
# 관리 프로세스가 실행 중이라면 먼저 종료
meatcan down

# 기본 후보 전체 탐색
meatcan scan

# 지정한 후보만 탐색
meatcan scan --rates 125k,250k,500k

# 정상 통신 중인 다중 노드 버스를 응답 없이 관찰
meatcan scan --passive --rates 125k,250k,500k

# 한 개의 후보를 3초 동안 확인
meatcan scan --rate 500k --timeout 3s

# 한 프레임만 받아도 후보로 확정
meatcan scan --rate 25k --min-frames 1

# 수신 프레임 없이 ACK만 하는 상대를 probe로 확인
meatcan scan --active --rate 500k

# scan 전용 도움말
meatcan scan -h
```

기본 후보는 `500k, 250k, 125k, 1m, 800k, 100k, 50k, 25k, 20k, 10k` 순서입니다. 흔한 속도를 먼저 검사해 잘못된 후보에서 버스에 영향을 주는 시간을 줄입니다. 기본 수신 시간은 후보당 1초이고, 유효한 프레임 1개를 받으면 첫 번째 일치 속도를 출력하고 종료합니다. `--rates` 또는 `--rate`를 지정하면 기본 목록 대신 지정한 후보를 사용합니다. 정상 다중 노드 버스에서는 `--min-frames 2` 이상으로 판정을 강화할 수 있습니다.

- `-r, --rates <list>`: 쉼표로 구분한 후보를 지정합니다.
- `--rate <rate>`: 후보 하나를 추가합니다. 반복해 사용할 수 있습니다.
- `-t, --timeout <time>`: 후보당 수신 시간입니다. `3s`, `500ms`, `500000us`처럼 양의 정수와 단위를 지정하며, 단위가 없으면 ms입니다. 최대 24시간입니다.
- `-m, --min-frames <count>`: 기본 모드와 `--passive`에서 bitrate 확정에 필요한 유효 프레임 수입니다. `--active`에서는 사용하지 않습니다.
- `--passive`: listen-only 모드로 관찰하며 ACK와 Error Frame을 보내지 않습니다. 이미 서로 통신하는 다른 두 노드가 필요합니다.
- `--active`: 후보마다 최저 우선순위 extended zero-DLC probe를 하나 보내고, `min(--timeout, 100ms)` 수신 창에서 현재 후보의 TX echo로 일치 여부를 확인합니다. `--rate` 또는 `--rates`가 필수입니다. 이 장치는 one-shot 모드를 지원하지 않으므로 잘못된 후보에서는 probe 재전송과 Error Frame이 발생할 수 있습니다. 테스트 버스에서 예상 속도를 `--rates 500k,250k`처럼 좁혀 사용하세요.

기본 모드와 `--passive`는 상대 노드가 실제로 프레임을 송신해야 탐색할 수 있습니다. 기본 모드는 정상 프레임에 ACK하므로 2노드 구성에서도 사용할 수 있습니다. `--passive`의 MeatPi는 ACK를 주지 않으므로 송신 노드와 ACK를 줄 다른 정상 노드가 이미 통신 중이어야 합니다.

메시지 주기가 길면 `--timeout`을 늘리세요. 배선, 종단저항, CAN H/L 문제로 검출에 실패할 수 있으며, 미검출 결과만으로 bitrate 불일치를 확정할 수 없습니다. `--min-frames`는 서로 다른 메시지 개수가 아닌 유효 프레임 수신 횟수이며, 탐색 성공도 장시간 통신 품질을 보장하지 않습니다.

탐색은 후보마다 장치를 열고, 수신 창이 끝나면 CAN 정지 성공을 확인한 뒤 USB를 닫습니다. 정지가 실패하면 검출 성공이나 다음 후보 진행 대신 오류를 보고합니다. 불일치 후보 뒤에만 100 ms 동안 안정화하며, 검출 성공 시에는 이 대기 없이 종료합니다. 한 handle에서 CAN을 반복 재설정하거나 정지 직후 다시 열면 일부 Ollie v2에서 USB I/O 오류와 장치 연결 해제가 발생할 수 있어 후보 전환마다 다시 엽니다. `--active`도 잘못된 속도의 재전송 상태를 다음 후보로 넘기지 않도록 같은 정리 절차를 사용합니다. daemon이나 다른 scan과 동시에 실행할 수 없습니다. USB 장치가 없거나 후보 속도를 정확히 설정할 수 없으면 오류로 종료합니다. USB 초기화와 후보 전환 시간은 `--timeout`에 포함되지 않으며, 진행 중인 USB 읽기로 수신 시간이 약 10 ms 늘어날 수 있습니다. 성공은 종료 코드 `0`, 미검출·오류는 `1`, `Ctrl+C` 중단은 `130`입니다. 종료 시 CAN을 정지하며, 검출된 속도로 자동 시작하지 않습니다. 출력된 `meatcan up --bitrate ...` 명령으로 일반 송수신을 시작하세요.

`State waiting`, `Adapter waiting`은 USB 장치 연결 또는 초기화 대기, `State ready`, `Adapter connected`는 USB/CAN 초기화 완료를 뜻합니다. `down`은 끄기 직전의 state, adapter, bitrate, 송수신 프레임 수와 마지막 오류를 출력합니다. `ready`는 선로의 정상 상태나 상대 연결까지 보장하지는 않습니다. 동일 USB 장치를 직접 사용하는 다른 프로그램은 종료한 뒤 실행합니다.

## 동작 구조

```text
meatcan dump ── 수신 구독 ──┐
                           ├── 관리 프로세스 ── libusb ── MeatPi
meatcan send ── 송신 요청 ──┘

# 관리 프로세스가 정지한 상태에서만 실행
meatcan scan ── libusb (receive + ACK) ── MeatPi
```

관리 프로세스 하나가 USB 장치를 소유합니다. `dump`와 `send`를 여러 터미널에서 사용할 수 있으며 `dump`를 종료해도 CAN은 유지됩니다. `down`이 CAN을 정지합니다. 일반 CAN 모드에서는 수신 구독자가 없어도 컨트롤러가 정상 프레임에 ACK를 응답할 수 있습니다.

런타임 디렉터리는 `/tmp/meatcan-<사용자 UID>`이며 권한은 `0700`입니다. `daemon.sock`과 `daemon.log`가 여기에 생성됩니다. 개발·테스트에서는 모든 명령에 동일한 `MEATCAN_RUNTIME_DIR` 환경 변수를 지정하여 경로를 분리할 수 있습니다. 로그인 시 자동 시작하는 서비스는 설치하지 않습니다.

## Homebrew 설치·업데이트

아래 명령은 이 저장소와 Formula가 GitHub에 공개된 뒤 사용할 수 있습니다. 현재 Formula는 소스의 `main`을 빌드하는 HEAD 설치를 제공합니다. Homebrew 7에서는 tap 전에 아래와 같이 이 Formula의 신뢰를 등록합니다. `trust` 명령이 없는 구버전 Homebrew에서는 첫 줄을 생략하세요.

```sh
brew trust --formula 42kko/meatcan/meatcan
brew tap 42kko/meatcan https://github.com/42kko/MeatPI_CAN_MACOS.git
brew install --HEAD 42kko/meatcan/meatcan
```

```sh
meatcan down
brew update
brew upgrade --fetch-HEAD 42kko/meatcan/meatcan
meatcan --version
meatcan up --bitrate 500k
```

제거는 `meatcan down` 후 `brew uninstall meatcan`입니다. Homebrew 공식 저장소 등록 없이 이 저장소 자체를 custom tap으로 사용합니다. 안정 버전 배포 절차는 [유지관리 문서](docs/MAINTAINING.md)를 참고하세요.

## 문제 진단

- **장치 대기:** GS USB 펌웨어, USB 연결, `1209:2323` 검색 여부를 확인합니다. 다른 프로그램이 USB를 점유하고 있지 않아야 합니다.
- **송신 타임아웃:** 상대 CAN 활성화, 동일 bitrate, CAN H/L 및 공통 기준 전위, 선로 양 끝의 종단 설정을 확인합니다. 타임아웃 이후 프레임의 전달 여부가 불명확할 수 있으므로 재시도는 애플리케이션에서 판단합니다.
- **USB Overflow/연결 해제:** `daemon.log`를 확인합니다. USB 읽기 버퍼와 CAN 프레임 해석은 별개입니다. 패킷이 프레임 하나라고 가정하지 않고, 알려진 GS USB 레이아웃과 수신 길이를 검증합니다. Overflow만으로 CAN 선로 과부하라고 단정할 수 없습니다.
- **설치 후 예전 프로세스:** 업데이트 전에 `down`, 업데이트 후 `up`을 실행합니다.

실장치 수신·송신, 연속 트래픽, 재연결은 모의 테스트와 별도로 검증해야 합니다. `--mock`은 자동 테스트용이며 실제 CAN 송신을 하지 않습니다.

## 개발 및 배포

- C++17 / CMake / libusb
- 자동 테스트: `ctest --test-dir build --output-on-failure`
- GitHub Actions: macOS 빌드·장치 없이 실행하는 테스트
- Homebrew Formula: `Formula/meatcan.rb`
- 버전·배포 관리: [docs/MAINTAINING.md](docs/MAINTAINING.md)
