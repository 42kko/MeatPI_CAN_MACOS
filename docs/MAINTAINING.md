# meatcan 유지관리

## 소스와 설치본의 관계

수정은 Git 저장소에서 하고, 설치본은 새 소스를 빌드해서 교체합니다. Homebrew Cellar 안의 파일을 직접 수정하지 않습니다. 관리 프로세스가 예전 실행파일을 사용하지 않도록 갱신 전 `meatcan down`, 갱신 후 `meatcan up --bitrate ...`을 실행합니다.

이 저장소는 C++ 소스와 `Formula/meatcan.rb`를 함께 관리하는 custom tap입니다. 저장소 이름이 `homebrew-meatcan`이 아니므로 처음 tap할 때 URL을 명시합니다. Formula에 아직 존재하지 않는 릴리스 URL이나 임의의 체크섬을 넣지 않습니다.

## 개발 버전 공개

1. 변경 사항을 검토하고 아래 테스트를 실행합니다.
2. GitHub의 `main`에 커밋을 push합니다.
3. 사용자는 HEAD 설치 또는 업데이트를 실행합니다. Homebrew 7에서는 tap 전에 해당 Formula만 `brew trust --formula`로 신뢰하도록 등록합니다. `trust` 명령이 없는 구버전에서는 해당 줄을 생략하세요.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

```sh
brew trust --formula 42kko/meatcan/meatcan
brew tap 42kko/meatcan https://github.com/42kko/MeatPI_CAN_MACOS.git
brew install --HEAD 42kko/meatcan/meatcan
brew test 42kko/meatcan/meatcan

# 이후 HEAD 갱신
meatcan down
brew update
brew upgrade --fetch-HEAD 42kko/meatcan/meatcan
```

GitHub Actions는 macOS 빌드와 장치 없는 테스트만 수행합니다. USB 하드웨어 회귀 검사는 별도로 합니다. CI는 배포 태그 생성·릴리스 게시·Formula push를 자동 실행하지 않습니다.

## 안정 버전 릴리스

아래는 버전 `0.1.0` 예시입니다. 실제 릴리스에 맞는 버전을 사용합니다.

1. CMake 프로젝트 버전과 `meatcan --version`을 일치시키고 테스트합니다.
2. USB 연결 대기·재연결, RX/TX 동시 사용, 상대 ACK 부재, 연속 수신, 정상 종료를 실장치로 확인하고 릴리스 노트에 실제 결과를 적습니다.
3. 소스 커밋을 push한 후 태그를 게시합니다. 공개한 태그는 이동하거나 덮어쓰지 않습니다.

```sh
git tag -a v0.1.0 -m "meatcan 0.1.0"
git push origin main
git push origin v0.1.0
```

4. GitHub에서 해당 태그의 릴리스 노트를 작성합니다. 소스 아카이브 URL이 접근 가능한 뒤 다음 명령으로 Formula를 갱신합니다. 스크립트는 실제 아카이브를 다운로드해 SHA-256을 계산하며, 다운로드 실패 시 Formula를 수정하지 않습니다.

```sh
python3 scripts/update_formula.py 0.1.0
git diff -- Formula/meatcan.rb
```

5. 변경된 Formula를 검토하고 tap checkout에 같은 변경을 적용해 설치·테스트합니다. 현재 소스에서 Formula만 복사하여 로컬 검증할 수 있습니다.

```sh
cp Formula/meatcan.rb "$(brew --repo 42kko/meatcan)/Formula/meatcan.rb"
meatcan down
brew reinstall --build-from-source 42kko/meatcan/meatcan
brew test 42kko/meatcan/meatcan
```

기존에 `--HEAD`로 설치했다면, 안정 버전 전환 시 `brew uninstall meatcan` 후 `brew install 42kko/meatcan/meatcan`을 사용합니다.

6. Formula 변경을 `main`에 커밋하고 push합니다. 로컬 tap의 검증용 변경도 커밋 내용과 같게 유지합니다. 사용자는 이후 일반 `brew upgrade`를 사용할 수 있습니다.

```sh
brew update
brew upgrade 42kko/meatcan/meatcan
```

배포용 Python 스크립트는 관리자가 릴리스할 때만 사용합니다. 설치된 meatcan 실행에는 Python이 필요 없습니다. 현재 패키지는 소스 빌드 방식이며 미리 빌드한 Homebrew bottle은 제공하지 않습니다.

## 라이선스와 호환성

라이선스는 저장소 소유자가 선택합니다. 선택 후 LICENSE와 Formula의 `license` 항목을 함께 갱신합니다. GS USB 프로토콜 동작을 변경할 때는 프레임 길이·패딩·에코·오류 프레임을 회귀 검사하고, 펌웨어와 확인한 macOS 버전을 기록합니다.

## 공식 참고 문서

- [MeatPi Ollie v2: GS USB 대상 장치](https://github.com/meatpiHQ/meatpi_ollie_v2#gs-usb-recommended)
- [Homebrew custom tap](https://docs.brew.sh/Taps)
- [Homebrew Formula 작성](https://docs.brew.sh/Formula-Cookbook)
- [Homebrew 명령 및 HEAD 업데이트](https://docs.brew.sh/Manpage)
