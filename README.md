# 삼국지 V with 파워업키트 한글판 — 윈도우 10/11·Wine 실행 패치

📄 Project page: **[삼국지 V with 파워업키트 한글판 — 윈도우 10/11·Wine 실행 패치](https://zirize.github.io/rotk5-patch/)** · More projects: **[zirize.github.io](https://zirize.github.io/)**

삼국지5 파워업키트 한글판(`SAM5.EXE`, 16비트 Windows 프로그램)을 64비트 윈도우와 Linux(Wine)에서
설치 없이, CD 없이 실행하는 패치입니다. 원본 EXE는 고치지 않습니다.
**게임 파일은 들어 있지 않습니다.** 삼국지 IV도 있습니다 → https://zirize.github.io/rotk4-patch/

## 1. 고치는 것

- 64비트 윈도우에서는 16비트 프로그램인 `SAM5.EXE`가 아예 실행되지 않습니다 → otvdm으로 실행합니다.
- "256 컬러 디스플레이 드라이브를 사용해 주십시오" 오류 → 256색 화면으로 보고합니다.
- 화면이 어두워졌다 밝아지는 장면 뒤에 배경이 그려지지 않음 → 다시 그립니다.
- "CD-ROM을 넣어 주십시오", CD 음악 없음 → CD 이미지(BIN) 또는 MP3로 CD 음악을 재생합니다.
- CD가 없으면 오프닝·엔딩 동영상이 안 나옴 → 게임 폴더의 `MOVIE`에서 재생합니다.
- 640×480 작은 창 → 메뉴 맨 왼쪽 "화면"에서 x1/x2/x3 정수배로 확대합니다.

## 2. 준비물

1. 게임: https://nemo838.tistory.com/2565 의
   `SAM5PKK_Win95_220129.7z.001` ~ `.032`와 `SAM5PKK_Win95_220129_Config.7z`.
2. 이 패치: [Releases](https://github.com/zirize/rotk5-patch/releases)의 `rotk5-patch.zip`
   (`rotk5.exe`, `rotk5.dll`).
3. 윈도우만: otvdm v0.9.0 — https://github.com/otya128/winevdm/releases
4. 7-Zip (https://www.7-zip.org).

## 3. 설치와 실행

### 윈도우 10/11

1. `SAM5PKK_Win95_220129.7z.001`을 7-Zip으로 풉니다(나머지 조각은 자동으로 이어집니다).
   `_Config.7z`도 풉니다. 게임 폴더 `SAM5WPK`와 CD 이미지(`Audio.cue`, `Audio.bin`)가 나옵니다.
2. `SAM5WPK` 폴더를 쓰기 가능한 곳에 둡니다 (예: `C:\Games\SAM5WPK`).
   `C:\Program Files` 아래는 저장이 안 되므로 피해 주세요.
3. `rotk5.exe`, `rotk5.dll`을 `SAM5WPK` 폴더에 넣습니다.
4. otvdm zip을 풀어 폴더 이름을 `otvdm`으로 바꾸고 `SAM5WPK` 안에 넣습니다
   (`SAM5WPK\otvdm\otvdmw.exe`). otvdm 설치나 레지스트리 설정은 필요 없습니다.
5. CD 음악: `SAM5WPK\CD` 폴더를 만들고 `Audio.cue`, `Audio.bin`을 복사합니다.
6. 동영상: `SAM5WPK\MOVIE` 폴더를 만들고 CD 이미지의 `OPEN.AVI`, `END.AVI`를 꺼내 넣습니다.
   Python이 있으면 `python tools\iso-extract.py <CD 폴더>\Audio.bin SAM5WPK\MOVIE`.
   `MOVIE`가 없으면 게임은 실제 CD(또는 가상 드라이브에 연결한 이미지)에서 찾습니다.
7. `rotk5.exe`를 실행합니다. `SAM5.EXE`를 직접 실행하면 안 됩니다.

### Linux (Wine)

16비트 프로그램은 **32비트 Wine prefix**(`WINEARCH=win32`)에서만 실행됩니다. 64비트(wow64)
prefix에서는 실행되지 않습니다. Ubuntu의 Wine 10이라면 `wine32` 패키지의 `/usr/bin/wine32`를
씁니다. otvdm은 필요 없습니다.

```sh
export WINEPREFIX=~/.wine-sam5 WINEARCH=win32 LANG=ko_KR.UTF-8
wine32 wineboot -i
wine32 regedit tools/prefix.reg      # 한국어 로캘, 코드페이지 949
# 굴림 글꼴(gulim.ttc)을 $WINEPREFIX/drive_c/windows/Fonts/ 에 복사
cd SAM5WPK                           # rotk5.exe, rotk5.dll, CD/, MOVIE/ 를 넣은 게임 폴더
wine32 rotk5.exe
```

`CD`, `MOVIE` 폴더는 윈도우의 5, 6단계와 같습니다.

### 설정과 문제 해결

- 고른 창 배율은 게임 폴더의 `rotk5.ini`에 저장됩니다. 모니터에 들어가지 않는 배율은
  회색으로 표시되고, 들어가는 가장 큰 배율로 줄어듭니다.
- 오프닝 뒤에 묻는 "640×480 / 800×600"은 게임 화면 크기이며 창 배율과는 별개입니다.
- CD 이미지를 다른 곳에 두려면 `rotk5.ini`에 `[cdaudio]` 절과 `cue=<Audio.cue 경로>`를 적습니다.
- 용량을 줄이려면 `tools/cd2mp3.sh`(bash, lame 필요)로 음악 트랙을 `TrackNN.mp3`로 바꿔
  `CD` 폴더에 `Audio.cue`와 함께 두고 `Audio.bin`은 빼도 됩니다.
- 백신이 `rotk5.exe`나 `rotk5.dll`을 막으면 게임 폴더를 허용해 주세요. `rotk5.exe`는 게임을
  시작하면서 `rotk5.dll`을 게임에 불러 넣는 실행기입니다.
- `rotk5.ini`에 `[debug]` 절과 `log=1`을 넣으면 `rotk5.log`에 동작 기록이 남습니다.

## 4. 동작 원리

`rotk5.exe`가 `SAM5.EXE`를 16비트 실행 환경(윈도우는 otvdm, Linux는 Wine의 winevdm) 위에서
멈춘 채로 시작하고, 그 프로세스에 `rotk5.dll`을 넣은 뒤 실행을 이어 갑니다. `rotk5.dll`은
그 프로세스의 Win32 API를 가로채 화면 색 수, WinG 팔레트 애니메이션, MCI CD 오디오, CD 드라이브
검사와 동영상 경로, 창 크기를 처리합니다. 자세한 내용은
[docs/sam5-internals.md](https://github.com/zirize/rotk5-patch/blob/main/docs/sam5-internals.md).

빌드: `make -C shim` (MinGW-w64 32비트 `i686-w64-mingw32-gcc`).

## 5. 시험한 환경

- Windows 11 64비트, otvdm v0.9.0: 실행, 오프닝 동영상(클릭으로 넘기기), CD 음악(MP3), 창 배율.
- Ubuntu 26.04 LTS, Wine 10.0 (32비트 prefix, `wine32`): 실행, 게임 진행, CD 음악(BIN과 MP3),
  효과음, 동영상, 창 배율.
- 윈도우에서는 함께 묶은 배포본(MP3 음악, 동영상을 미리 꺼내 둔 상태)으로 시험했습니다. 위 순서대로 CD 이미지(BIN)를 그대로 넣는 음악 재생과 `iso-extract.py`로 동영상을 꺼내는 과정은 윈도우에서 시험하지 않았습니다(Wine에서는 시험).
- Windows 10은 시험하지 않았습니다.

## 6. 라이선스

- 이 패치의 코드: MIT 라이선스 (`LICENSE`).
- MP3 재생에 minimp3(https://github.com/lieff/minimp3, CC0)를 씁니다 — `shim/minimp3/`.
- otvdm(GPL 2)은 이 저장소에 들어 있지 않습니다.
- KOEI TECMO와 관계없는 개인 프로젝트입니다. 게임의 저작권은 KOEI TECMO에 있으며, 이 저장소는
  게임 파일, CD 음악, 동영상을 배포하지 않습니다.
