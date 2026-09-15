# 검증 기록

2026-09-15, macOS 로컬 개발 환경 기준입니다.

## 자동 검증

- CMake Release 빌드 성공, CTest 2/2 통과
- CLI/daemon 통합 검사 140개 통과: 시작·종료, 동시 클라이언트, 입력 검증, 지연/누락/불일치 에코, 연결 종료
- 별도 프로토콜 검사 31개 통과
- Mock 12,000프레임에서 송수신 카운터 일치, 느린 구독자 분리 후 daemon 응답 유지
- 발견된 속도 정수 오버플로 및 종료된 클라이언트 fd 재사용 문제는 수정 후 재검증
- Homebrew Formula Ruby 구문 및 릴리스 SHA-256 갱신 스크립트 검사 통과

## 실제 장치: 검증 미완료

MeatPi Ollie v2 GS USB `1209:2323`을 확인했습니다. 기능 조회 결과 CAN 클럭 36 MHz, BRP 1~1024, USB IN 최대 패킷 512 bytes였습니다. 기존 Python으로 gateway가 미리 보낸 두 프레임을 읽었을 때 각각 20 bytes 패킷으로 수신됐습니다.

C++ 최초 초기화 시 USB control timeout이 발생했고, 이후 Python의 GET_CONFIGURATION 및 USB reset도 timeout이 발생했습니다. 재연결 후 현재 검증 시점에는 해당 USB ID가 열거되지 않아 daemon의 연결 대기 상태를 확인했습니다. 따라서 C++ 실제 CAN 송수신, 물리 재연결 후 복구, 사용자 보고 Overflow 재현·해결은 완료로 간주하지 않습니다.

## 실장치 확인 순서

1. GS USB 펌웨어, 동일 bitrate, CAN H/L 및 선로 양 끝 종단 설정을 확인합니다.
2. `meatcan up --bitrate 25k` 후 `meatcan status`가 `state=ready`인지 확인합니다.
3. `meatcan dump`를 실행하고 상대 장치에서 프레임을 보냅니다.
4. 별도 터미널에서 `meatcan send 123#01020304`를 실행하고 상대 장치 수신까지 확인합니다.
5. 상대가 먼저 송신한 뒤 daemon을 시작하는 조건, 연속 수신, USB 재연결을 비교합니다. 완료가 불명확한 송신은 자동 재시도하지 않습니다.
6. `meatcan down`으로 장치와 daemon을 종료합니다.
