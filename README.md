# meatcan

[한국어](README.md) | [English](README.en.md)

**MeatPi Ollie v2를 macOS에서 사용하는 GS CAN (`gs_usb`) 기반 CLI입니다.** macOS에는 Linux의 SocketCAN이 없어, C++과 libusb로 GS USB CAN 펌웨어에 직접 접근합니다.

- 대상: [MeatPi Ollie v2](https://github.com/meatpiHQ/meatpi_ollie_v2), USB ID `1209:2323`
- 지원: Classic CAN 데이터 프레임, 11/29비트 ID, 최대 8바이트. RTR 수신·표시도 지원합니다.
- 미지원: SLCAN 펌웨어, CAN FD, RTR 송신
- 실행에 Python이나 가상환경은 필요하지 않습니다.

## 설치

Homebrew로 `main` 브랜치의 최신 코드를 빌드해 설치합니다. Homebrew 7에서는 아래 신뢰 등록이 필요합니다. `trust` 명령이 없는 구버전은 첫 줄을 생략하세요.

```sh
brew trust --formula 42kko/meatcan/meatcan
brew tap 42kko/meatcan https://github.com/42kko/MeatPI_CAN_MACOS.git
brew install --HEAD 42kko/meatcan/meatcan
```

업데이트 전에는 실행 중인 CAN을 종료하세요.

```sh
meatcan down
brew update
brew upgrade --fetch-HEAD 42kko/meatcan/meatcan
meatcan --version
```

삭제는 `meatcan down` 후 `brew uninstall meatcan`입니다.

## 송수신

상대 장치와 같은 속도로 CAN을 켭니다. 속도를 생략하면 **25 kbps**입니다.

```sh
meatcan up --bitrate 500k
meatcan status
```

터미널 하나에서 수신하고, 다른 터미널에서 송신할 수 있습니다.

```sh
# 터미널 1: 수신 로그 표시
meatcan dump

# 터미널 2: 한 프레임 송신
meatcan send 123#01020304

# CAN 종료
meatcan down
```

`dump`에서 `Ctrl+C`를 눌러도 CAN은 계속 켜져 있습니다. CAN 자체를 끄려면 `down`을 사용하세요. 속도를 바꿀 때도 `down` 후 새 속도로 `up`을 실행합니다.

### 메시지 형식

`CAN_ID#DATA` 형식으로 입력합니다. ID와 데이터는 16진수이며 데이터는 2자리씩, 최대 16자리(8바이트)입니다.

```sh
meatcan send 123#01020304        # 표준 ID
meatcan send 1ABCDEFF#AABBCCDD   # 확장 ID
meatcan send 123#               # 데이터 없는 프레임
```

장치의 송신 완료 응답(TX echo)을 받으면 `MeatCAN TX complete`과 프레임을 출력합니다. 성공은 종료 코드 `0`, 오류·타임아웃은 `1`입니다. 완료 응답은 상대 애플리케이션의 처리까지 확인하는 것은 아닙니다. 타임아웃이 나면 실제 전달 여부가 불명확하므로 CLI가 임의로 재전송하지 않습니다.

### 반복·연속 송신

```sh
meatcan send -r 10 -i 100ms 123#01020304  # 총 10회
meatcan send -c -i 1s 123#01020304        # Ctrl+C까지 반복
meatcan send -q -r 100 123#01020304       # 성공 출력 숨김
```

| 옵션 | 설명 |
|---|---|
| `-r, --repeat <count>` | 총 송신 횟수 |
| `-c, --continuous` | 중단할 때까지 반복. 간격을 생략하면 100 ms |
| `-i, --interval <time>` | 송신 완료 응답을 받은 뒤 다음 송신까지 대기 시간. `500us`, `100ms`, `1s`; 단위 생략 시 ms |
| `-q, --quiet` | 성공 출력과 요약을 숨김. 오류는 표시 |

연속 송신은 버스 점유율을 높입니다. 테스트 버스에서 적절한 간격을 지정해 사용하세요.

## 통신 속도 찾기

먼저 실행 중인 CAN을 `meatcan down`으로 종료하세요. `scan`은 장치를 직접 사용하므로 다른 scan이나 CAN 관리 프로세스와 동시에 실행할 수 없습니다.

| 모드 | 사용할 상황 | 버스에 미치는 영향 |
|---|---|---|
| `scan` | 상대가 프레임을 송신 중일 때. 2노드 구성에서도 사용 가능 | 정상 프레임에 ACK. 잘못된 속도에서는 오류 프레임을 보낼 수 있음 |
| `scan --passive` | 다른 두 노드가 이미 정상 통신하는 버스를 관찰할 때 | 송신·ACK·오류 프레임 없이 수신만 수행 |
| `scan --active` | 상대가 송신하지 않고 ACK만 응답할 때 | 시험 프레임을 송신. 잘못된 속도에서는 재전송·오류 프레임 발생 가능 |

```sh
meatcan scan                                  # 기본 후보 탐색
meatcan scan --rates 125k,250k,500k           # 지정한 후보만 탐색
meatcan scan --passive --rates 125k,250k,500k # 수신만으로 탐색
meatcan scan --rate 500k --timeout 3s         # 후보 하나를 길게 확인
meatcan scan --active --rate 500k             # 시험 프레임으로 확인
```

기본 후보 순서는 `500k, 250k, 125k, 1m, 800k, 100k, 50k, 25k, 20k, 10k`입니다. 기본·passive 모드는 후보당 1초 동안 듣고 정상 프레임 1개를 받으면 속도를 출력하고 종료합니다.

| 옵션 | 설명 |
|---|---|
| `-r, --rates <list>` | 쉼표로 구분한 후보 목록. 기본 목록을 대체 |
| `--rate <rate>` | 후보 하나 지정. 여러 번 사용 가능 |
| `-t, --timeout <time>` | 후보당 수신 시간. 양의 정수와 `s`, `ms`, `us`; 단위 생략 시 ms, 최대 24시간 |
| `-m, --min-frames <count>` | 검출에 필요한 수신 횟수. 기본값 1, active에서는 미사용 |

**Active는 테스트 버스에서 예상 후보를 좁혀 사용하세요.** `--rate` 또는 `--rates`가 필수입니다. 후보마다 최저 우선순위의 확장 ID·데이터 없는 프레임을 한 번 요청하고, `--timeout`과 100 ms 중 짧은 시간 동안 해당 TX echo를 기다립니다. 장치가 one-shot을 지원하지 않아 ACK가 없으면 하드웨어가 재전송할 수 있습니다.

탐색 시 알아둘 점:

- 기본·passive 모드는 실제 송신 트래픽이 필요합니다. Passive는 ACK를 주지 않으므로 MeatPi와 송신기만 연결한 구성에서는 검출이 어려울 수 있습니다.
- 메시지 주기가 길면 `--timeout`을 늘리세요. `--min-frames 2` 이상으로 더 많은 수신을 확인할 수 있지만, 서로 다른 메시지인지까지 검사하지는 않습니다.
- 배선·종단저항·CAN H/L 문제도 미검출 원인입니다. 탐색 성공이 장시간 통신 품질까지 보장하지는 않습니다.
- 종료 시 CAN을 정지합니다. 출력된 `meatcan up --bitrate ...` 명령으로 송수신을 시작하세요.
- 종료 코드는 검출 `0`, 미검출·오류 `1`, `Ctrl+C` 중단 `130`입니다.

## 상태 확인과 문제 해결

`up`, `status`, `down`은 연결 상태, 속도, 송수신 수와 마지막 오류를 보여줍니다. `waiting`은 장치 연결·초기화 대기, `ready`는 USB/CAN 초기화 완료입니다. `ready`만으로 상대 연결이나 배선이 정상인지 알 수는 없습니다. 설정할 수 없는 속도라면 `waiting` 상태와 마지막 오류를 확인하세요.

| 증상 | 확인할 것 |
|---|---|
| 장치 연결 대기 | GS USB 펌웨어, USB 케이블, 장치 ID `1209:2323`. 동일 USB 장치를 사용하는 다른 프로그램 종료 |
| 송신 타임아웃 | 상대 CAN 활성화, 같은 속도, CAN H/L·공통 기준 전위·양 끝 종단저항 |
| USB Overflow·연결 해제 | 로그와 USB 연결 상태. Overflow만으로 CAN 트래픽 과부하라고 단정할 수 없음 |
| 업데이트 후 예전 동작 | `down` 후 다시 `up` |

관리 프로세스 로그는 `/tmp/meatcan-<사용자 UID>/daemon.log`에 있습니다. `scan` 오류는 실행한 터미널에 표시됩니다. 실장치 검증 범위는 [검증 기록](docs/VALIDATION.md)을 참고하세요.

## 개발

### 소스 빌드

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

직접 PATH에 설치하려면:

```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

PATH 설정을 셸 설정 파일에 추가하면 새 터미널에도 적용됩니다. 직접 설치한 버전은 다시 빌드·설치해 업데이트합니다.

### 구조

```text
meatcan dump ── 수신 구독 ──┐
                           ├── 관리 프로세스 ── libusb ── MeatPi
meatcan send ── 송신 요청 ──┘

meatcan scan ── libusb ── MeatPi  (관리 프로세스가 정지한 상태)
```

C++17과 libusb를 사용합니다. 관리 프로세스가 USB를 계속 소유하므로 수신 화면을 닫아도 CAN과 ACK 응답은 유지됩니다. 로그인 시 자동 시작하는 서비스는 설치하지 않습니다.

`--mock`은 실장치에 접근하지 않는 테스트 전용 모드입니다. USB 초기화와 scan 검증 결과는 [검증 기록](docs/VALIDATION.md)에 정리했습니다.

- 도움말: `meatcan -h`, `meatcan send -h`, `meatcan scan -h`
- [검증 기록](docs/VALIDATION.md)
- [Homebrew·릴리스 유지관리](docs/MAINTAINING.md)
