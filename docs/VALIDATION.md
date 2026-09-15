# 검증 기록

2026-09-15, macOS 로컬 개발 환경 기준입니다.

## 자동 검증

- CMake Release 빌드 성공, 최신 CTest 3/3 통과
- CLI/daemon 통합 검사 153개 통과: 시작·종료, 동시 클라이언트, 반복·연속·간격 송신, `Ctrl+C` 종료, 입력 검증, 지연/누락/불일치 에코
- USB shim 초기화·정리 회귀 검사 11개 통과
- 별도 프로토콜 검사 31개 통과
- Mock 12,000프레임에서 송수신 카운터 일치, 느린 구독자 분리 후 daemon 응답 유지
- 발견된 속도 정수 오버플로 및 종료된 클라이언트 fd 재사용 문제는 수정 후 재검증
- Homebrew Formula Ruby 구문 및 릴리스 SHA-256 갱신 스크립트 검사 통과
- 실제 custom tap 연결 및 Homebrew HEAD 설치 성공 (`HEAD-6b642b4`)
- `brew test 42kko/meatcan/meatcan` 통과, `/opt/homebrew/bin/meatcan --version` 확인

## 실제 장치: 25 kbps 검증 성공

MeatPi Ollie v2 GS USB `1209:2323`의 CAN 클럭 36 MHz, BRP 1~1024, USB IN 최대 패킷 512 bytes를 확인했습니다. 실제 USB 분리 시 장치가 사라지고, 재연결 시 새 레지스트리 항목으로 나타난 뒤 아래 검증을 수행했습니다.

| 검사 | 결과 |
|---|---|
| 기존 Python 시작·대기·종료 | 성공 |
| Python 양방향 | gateway의 `321#11223344` 수신, `123#01020304` 송신 후 gateway 수신 확인 |
| 수정된 C++ 초기화 | `state=ready` |
| C++ 양방향 | gateway의 `322#55667788` 수신, `124#AABBCCDD` 송신 후 gateway 수신 확인 |
| C++ 재시작·송신 반복 | 5회 성공 |
| C++ `send -r 3 -i 20ms` | 3회 송신, `tx=3`, 오류 없음 |
| gateway 선행 송신 후 C++ 시작 | `333#DEADBEEF` 수신, `rx=1`, `state=ready` 유지 |
| 선행 송신 검사 이후 추가 송신 | 성공, 해당 검사 중 Overflow 없음 |

이번 확인은 **25 kbps** 기준이며 장시간 부하나 다른 속도의 안정성까지 검증한 결과는 아닙니다.

## 초기화 문제와 변경 사항

사용자는 C++ 시작 시 연결이 끊기는 현상을 보고했습니다. 초기 진단에서는 C++ 제어 요청 timeout 이후 Python의 GET_CONFIGURATION 및 GS USB 기능 조회도 timeout 또는 STALL로 실패했습니다. 캐시된 configuration은 1이었으나 재적용만으로는 복구되지 않았습니다.

기존 Python 경로에 맞춰 시작 시 별도 CAN STOP, 수신 drain, HOST_FORMAT 요청을 제거하고, CAN 시작에 성공한 경우에만 종료 정리를 수행하도록 수정했습니다. 위 실장치 성공은 이 수정과 실제 USB 분리·재연결 이후 관찰한 결과입니다. **제거한 요청 중 어느 것이 원인이었는지는 분리 검증하지 않았습니다.**

정상 상태에서 시험한 소프트 USB reset은 `Entity not found`를 반환했으나 이후 configuration은 0이었고, Python에서 configuration 설정·시작·종료가 가능했습니다. 이전 정지 상태에서의 reset timeout 시험은 사용자 USB 분리와 시간이 겹쳐 복구 인과관계를 판단할 수 없습니다. 소프트 reset을 확실한 복구 방법으로 간주하지 않습니다.

최신 변경을 Homebrew `HEAD-ac69e7b`로 갱신했으며 설치 과정의 CMake 빌드와 CTest가 성공했습니다. 설치본 `/opt/homebrew/bin/meatcan`에서 `up --bitrate 25k` 후 `state=ready`, `send 127#10203040` 종료 코드 0, `tx=1`, `last_error=none`을 확인했습니다.

## 실장치 확인 순서

1. GS USB 펌웨어, 동일 bitrate, CAN H/L 및 선로 양 끝 종단 설정을 확인합니다.
2. `meatcan up --bitrate 25k` 후 `meatcan status`가 `state=ready`인지 확인합니다.
3. `meatcan dump`를 실행하고 상대 장치에서 프레임을 보냅니다.
4. 별도 터미널에서 `meatcan send 123#01020304`를 실행하고 상대 장치 수신까지 확인합니다.
5. 상대가 먼저 송신한 뒤 daemon을 시작하는 조건, 연속 수신, USB 재연결을 비교합니다. 완료가 불명확한 송신은 자동 재시도하지 않습니다.
6. `meatcan down`으로 장치와 daemon을 종료합니다.
