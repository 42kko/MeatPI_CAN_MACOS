# 검증 기록

2026-09-15, macOS 로컬 개발 환경 기준입니다.

## 자동 검증

- CMake Release 빌드 성공, 최신 CTest 3/3 통과
- CLI/daemon 통합 검사 157개 통과: 시작·종료, 동시 클라이언트, 반복·연속·간격 송신, `Ctrl+C` 종료, 입력 검증, 지연/누락/불일치 에코
- USB shim 초기화·정리·scan 회귀 검사 102개 통과
- 별도 프로토콜 검사 26개 통과
- Mock 12,000프레임에서 송수신 카운터 일치, 느린 구독자 분리 후 daemon 응답 유지
- 발견된 속도 정수 오버플로 및 종료된 클라이언트 fd 재사용 문제는 수정 후 재검증
- Homebrew Formula Ruby 구문 및 릴리스 SHA-256 갱신 스크립트 검사 통과
- 실제 custom tap 연결 및 Homebrew HEAD 설치 성공 (`HEAD-6b642b4`)
- `brew test 42kko/meatcan/meatcan` 통과, `/opt/homebrew/bin/meatcan --version` 확인

## Bitrate scan 자동 검증

최신 CTest 3/3과 총 285개 검사가 통과했습니다. CLI/daemon 157개, USB shim 102개, 프로토콜 26개입니다. 자동 검사는 실장치 대신 USB shim을 사용했습니다.

- 기본 후보 10개와 순서, 사용자 후보·중복 제거
- 기본 normal receive/ACK, 명시적 LISTEN_ONLY, active probe 모드 전달
- 최소 정상 프레임 수, 오류 프레임·송신 에코·잘못된 표준 ID 제외
- 무수신, 잘못된 속도·시간·최소 개수, listen-only 미지원 오류, scan 모드 충돌
- 후보별 USB open/CAN 종료/USB close, 동시 scan 소유권 잠금
- CAN STOP 실패 시 검출 성공·다음 후보 진행 금지 및 USB 자원 정리
- 후보마다 다른 probe echo ID를 사용해 이전 후보의 stale echo로 잘못 검출하는 경우 방지
- SIGTERM 중단 시 종료 코드 130, 장치 정리와 잠금 재사용

실제 25 kbps 2노드 버스에서 gateway를 100 ms마다 CAN down/up한 뒤 지속 송신해도 LISTEN_ONLY는 RX 0이었습니다. 앞서 관찰한 최초 1프레임은 stale 데이터일 가능성을 배제할 수 없습니다. 따라서 기본 scan은 normal receive/ACK 모드와 1프레임 임계값을 사용하며, 버스에 응답하지 않는 동작은 명시적 `--passive`로 제공합니다. 상대가 송신하지 않는 경우를 위한 `--active` probe는 명시적 후보와 `min(--timeout, 100ms)` 창을 사용합니다. `--active`에서는 `--min-frames`를 사용하지 않습니다. 정상 다중 노드 버스에서는 `--min-frames 2` 이상을 사용할 수 있습니다.

최종 실장치 검사에서는 gateway가 매 송신 후 CAN 인터페이스를 내려 TX 큐를 비우고 25 kbps로 다시 올리도록 구성했습니다. 기본 `meatcan scan`이 `500k, 250k, 125k, 1m, 800k, 100k, 50k`를 제외한 뒤 25 kbps에서 정상 프레임 1개를 받고 검출에 성공했습니다. 불일치 후보의 USB close 후 100 ms 안정화를 적용한 상태에서 scan 종료 후에도 장치가 `1209:2323`으로 유지됐고, 이어진 `up --bitrate 25k`, `send 123#01020304`, `status`, `down`도 `tx=1`, `last_error=none`으로 성공했습니다.

최신 checked STOP 및 후보별 probe echo 수정 후 실장치에서 다음 검사를 다시 통과했습니다.

- 전체 기본 후보 탐색과 `--active --rate 25k`에서 25 kbps 검출
- 탐색 후 USB 장치 검색 유지 및 `up`·`status`·`down` 성공
- gateway → MeatPi: `dump`에서 `123#11223344`, `001ABCDE#AABBCCDD`, `321#` 수신
- MeatPi → gateway: gateway의 `candump`에서 `126#DEADBEEF` 수신
- 단발 송신, repeat 3회, quiet 2회, continuous 54회 송신 후 최종 `RX=3`, `TX=61`, `last_error=none`

이 결과는 해당 25 kbps 테스트 구성에서의 성공이며, 모든 후보 속도의 통신 성공이나 장시간 안정성을 뜻하지 않습니다.

## 실제 장치: 25 kbps 검증 성공

MeatPi Ollie v2 GS USB `1209:2323`의 CAN 클럭 36 MHz, BRP 1~1024, USB IN 최대 패킷 512 bytes를 확인했습니다. 실제 USB 분리 시 장치가 사라지고, 재연결 시 새 레지스트리 항목으로 나타난 뒤 아래 검증을 수행했습니다.

| 검사 | 결과 |
|---|---|
| 수정된 C++ 초기화 | `state=ready` |
| C++ 양방향 | gateway의 `322#55667788` 수신, `124#AABBCCDD` 송신 후 gateway 수신 확인 |
| C++ 재시작·송신 반복 | 5회 성공 |
| C++ `send -r 3 -i 20ms` | 3회 송신, `tx=3`, 오류 없음 |
| gateway 선행 송신 후 C++ 시작 | `333#DEADBEEF` 수신, `rx=1`, `state=ready` 유지 |
| 선행 송신 검사 이후 추가 송신 | 성공, 해당 검사 중 Overflow 없음 |

이번 확인은 **25 kbps** 기준이며 장시간 부하나 다른 속도의 안정성까지 검증한 결과는 아닙니다.

## 초기화 문제와 변경 사항

사용자는 C++ 시작 시 연결이 끊기는 현상을 보고했습니다. 초기 진단에서는 C++ 제어 요청 timeout 이후 GET_CONFIGURATION 및 GS USB 기능 조회도 timeout 또는 STALL로 실패했습니다. 캐시된 configuration은 1이었으나 재적용만으로는 복구되지 않았습니다.

검증된 초기화 순서에 맞춰 시작 시 별도 CAN STOP, 수신 drain, HOST_FORMAT 요청을 제거하고, CAN 시작에 성공한 경우에만 종료 정리를 수행하도록 수정했습니다. 위 실장치 성공은 이 수정과 실제 USB 분리·재연결 이후 관찰한 결과입니다. **제거한 요청 중 어느 것이 원인이었는지는 분리 검증하지 않았습니다.**

정상 상태에서 시험한 소프트 USB reset은 `Entity not found`를 반환했으나 이후 configuration은 0이었고, 다시 configuration 설정·시작·종료가 가능했습니다. 이전 정지 상태에서의 reset timeout 시험은 사용자 USB 분리와 시간이 겹쳐 복구 인과관계를 판단할 수 없습니다. 소프트 reset을 확실한 복구 방법으로 간주하지 않습니다.

이전 초기화 변경을 Homebrew `HEAD-ac69e7b`로 갱신했을 때 설치 과정의 CMake 빌드와 CTest가 성공했습니다. 설치본 `/opt/homebrew/bin/meatcan`에서 `up --bitrate 25k` 후 `state=ready`, `send 127#10203040` 종료 코드 0, `tx=1`, `last_error=none`을 확인했습니다.

## 실장치 확인 순서

1. GS USB 펌웨어, 동일 bitrate, CAN H/L 및 선로 양 끝 종단 설정을 확인합니다.
2. `meatcan up --bitrate 25k` 후 `meatcan status`가 `state=ready`인지 확인합니다.
3. `meatcan dump`를 실행하고 상대 장치에서 프레임을 보냅니다.
4. 별도 터미널에서 `meatcan send 123#01020304`를 실행하고 상대 장치 수신까지 확인합니다.
5. 상대가 먼저 송신한 뒤 daemon을 시작하는 조건, 연속 수신, USB 재연결을 비교합니다. 완료가 불명확한 송신은 자동 재시도하지 않습니다.
6. `meatcan down`으로 장치와 daemon을 종료합니다.
