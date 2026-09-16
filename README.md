# MovieUpscaling / Window GPU Preview — Phase 1–5

Windows 11에서 실행 중인 창을 선택하여 Windows Graphics Capture로 캡처하고, D3D11 GPU에서 크롭·확대·RCAS 샤프닝한 뒤 별도 Preview 창에 출력하는 C++20/Win32 앱입니다. 기존 캡처 수명 관리와 프레임 도착 기반 렌더링을 유지하며 Phase 5에 RCAS, 수동 크롭과 A/B 비교를 추가했습니다.

## 실행

이번에 검증한 실행 파일: `build/phase5/Release/WindowGPUPreview.exe`. 기존 `build/Release` 실행 파일이 사용 중이어서 별도 빌드 디렉터리를 사용했습니다.

1. 캡처할 브라우저/VLC/프로그램 창을 열고 최소화를 해제합니다.
2. 앱에서 **Refresh** → **Source window** 선택 → **Start**.
3. Preview 크기를 조절하거나 **Fullscreen** / Preview 더블클릭으로 전체화면을 전환합니다.
4. **Esc**는 전체화면을 종료합니다. **Always on top**은 Preview에 적용됩니다.
5. 다른 모니터를 사용하려면 Preview를 해당 모니터로 옮긴 다음 전체화면을 켭니다. 현재 모니터 전체 크기를 사용합니다.
6. **Stop**은 캡처를 해제하고 Preview를 검정으로 지웁니다. Preview 닫기도 캡처를 중지하며 Start로 다시 열 수 있습니다.

### Upscale / Output

**Upscale**과 **Output**은 캡처 중에도 변경할 수 있습니다. 다음 캡처 프레임부터 적용하고 캡처 세션은 재시작하지 않습니다. 정지 영상에서 WGC가 새 프레임을 보내지 않으면 `pending next frame`으로 표시합니다. **Sharpen/F3, 크롭 조절/F2, Fit/프리셋은 마지막 GPU 프레임을 한 번 다시 그려 즉시 적용합니다**. 이때 대기 중이던 출력 설정도 함께 적용됩니다. 가이드만 변경하면 Crop Edit 중에만 다시 그립니다. 반복 렌더 루프는 없으며 설정은 Stop/Start 동안 유지되고 앱을 종료하면 기본값으로 돌아갑니다.

| Upscale | 실제 구현 |
|---|---|
| Off / Native | 업스케일 패스 생략. 선택한 출력 공간의 픽셀과 입력 픽셀을 1:1로 배치 |
| Bilinear | GPU 하드웨어 bilinear 샘플링 |
| Bicubic | Catmull–Rom, a=-0.5, 4×4 separable cubic 필터 |
| Lanczos-2 | sinc(x) × sinc(x/2), 반경 2, 4×4 커널 및 가중치 정규화 |
| FSR EASU (기본값) | AMD FSR 1 FP32 EASU를 기반으로 한 12-tap edge-adaptive spatial 필터 |

**Output**: Auto(기본값), 1920×1080, 2560×1440, 3840×2160.

- Auto는 Preview가 있는 모니터의 현재 데스크톱 해상도를 사용합니다. 패널 EDID의 최대 해상도를 강제로 설정하거나 디스플레이 모드를 변경하지 않습니다. 모니터 해상도를 권장/native 모드로 설정하면 해당 해상도로 출력합니다.
- Output은 GPU 처리 목표 해상도입니다. 작은 Preview 창에서도 4K를 선택하면 실제 4K 중간 텍스처를 생성하고 창 크기로 축소 표시합니다. 상태창의 **Preview**는 SwapChain 해상도로 별도 표시됩니다.
- 입력 비율을 유지하며 출력 공간의 나머지는 검정입니다. 예: 1280×720→3840×2160은 3.00x, 640×480→3840×2160은 2880×2160 콘텐츠와 좌우 여백, 4.50x입니다. 콘텐츠 크기는 정수 픽셀로 반올림하므로 비율 오차는 최대 약 1픽셀입니다.
- Native는 출력 공간 중앙에 원본 크기로 배치합니다. 원본이 출력 공간보다 크면 중앙을 기준으로 잘립니다. 작은 Preview 창은 이 전체 출력 공간을 축소해서 보여주므로 화면상 물리 픽셀 1:1과는 다를 수 있습니다.
- 1:1 크기에서는 모든 모드가 불필요한 필터 패스를 생략합니다.
- EASU는 확대용입니다. 입력이 출력보다 크면 bilinear로 축소하며 상태창에 **FSR EASU (bilinear downscale)**로 명시합니다. Bicubic/Lanczos는 고정 반경 필터여서 큰 폭의 축소에 필요한 가변 반경 anti-alias 필터는 제공하지 않습니다.

### EASU 구현

`src/shaders/upscale_easu.hlsl`은 AMD 공개 EASU의 네 주변 지점에 대한 밝기 기울기 추정, 방향 정규화, 비등방성 radial Lanczos 근사 커널, 가변 negative lobe, 최근접 2×2 색상 범위 제한을 구현합니다. Bilinear/Bicubic을 FSR로 이름만 바꾼 모드가 아닙니다.

원본 대비 차이: RGB clamped load 사용, FP32 reciprocal/rsqrt와 0 나눗셈 방지, 3x 같은 정수 배율에서 부동소수점 오차로 픽셀 구간이 바뀌지 않도록 경계 좌표 보정. 따라서 AMD reference의 근사 산술과 bit-exact한 결과를 목표로 하지는 않습니다. RCAS는 아래의 독립적인 후처리 패스로 적용됩니다.

AMD SDK 전체를 포함하지 않습니다. 저작권과 MIT 허가는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에 있으며 빌드 시 실행 파일 옆에도 복사합니다. 배포 시 함께 포함하세요.

### RCAS Sharpen

- **Sharpen 0–100**, 기본값 **25**. 모든 업스케일 모드 뒤에 적용되며 0이면 RCAS 텍스처를 만들거나 패스를 실행하지 않습니다.
- `rcas.hlsl`은 AMD FSR 1 RCAS의 십자형 5-tap, 채널별 limiting solution, 적응형 negative lobe, 밝기 기반 noise attenuation을 구현합니다. 일반 unsharp mask로 대체하지 않았습니다.
- 추가적인 주변 색상 범위 ±0.02 clamp를 제거했습니다. RCAS의 채널별 적응형 limiter, -0.1875 lobe 제한과 최종 SDR [0,1] 제한은 유지합니다.
- `t = strength / 100`, lobe 배율은 `pow(t, 0.65)`입니다. 25/50/75/100에서 약 0.406/0.637/0.829/1.000입니다. AMD의 밝기 기반 noise attenuation 계수는 `0.5 - 0.15*t`로 조정하여 강한 설정에서도 가는 선 주변 링잉을 억제합니다. RGB 전체 대비를 곱하는 방식이 아닙니다.
- **25 = 약한 선명도 개선, 50 = 뚜렷함, 75 = 강함, 100 = 확인용으로 공격적인 설정**입니다. 일반적인 권장 범위는 **20–40**, 기본값은 25입니다. 100은 일부 텍스트/체커 무늬에 과한 윤곽을 만들 수 있습니다.
- 이 처리는 압축 잡음을 복원하거나 제거하는 denoiser가 아닙니다. 노이즈가 심한 영상에는 낮은 강도를 사용하세요. 고해상도 처리 결과를 작은 Preview로 축소하면 샤프닝 차이가 덜 보일 수 있습니다.
- 강도만 바꾸면 constant buffer만 갱신하며 텍스처를 재할당하지 않습니다. **F3**으로 끄거나 마지막 0이 아닌 강도를 복원합니다. 캡처 중 정지 영상에서도 GPU에 보관된 동일 프레임을 한 번 다시 처리합니다. Stop/Clear 이후에는 이전 프레임을 다시 표시하지 않습니다.
- 상태에 `Sharpen: RCAS N`과 `RCAS: Active` / `RCAS: Bypassed`를 표시합니다. 크롭 편집 중에도 Bypassed입니다. 정지 화면에서 설정 변경 직후 완료된 GPU query가 없으면 시간은 N/A이며 CPU 시간을 대신 표시하지 않습니다.
- 개발 진단용으로 실행 전에 PowerShell에서 `$env:MOVIEUPSCALING_DEBUG_RCAS='1'`을 설정하면 RCAS 배율을 2배로 만듭니다. 상태는 `RCAS: Active (DEBUG BOOST)`입니다. 기본값은 꺼짐이며 정확히 `1`일 때만 켜집니다. 0 bypass는 그대로이고, boost의 lobe는 -0.23으로 제한하여 분모가 0에 접근하지 않게 합니다. 이 모드는 reference 강도를 초과하므로 일반 시청용이 아닙니다. 해제 후 앱을 다시 실행하세요: `Remove-Item Env:MOVIEUPSCALING_DEBUG_RCAS`.
- 선명도 비교는 **Output 2560×1440, Preview 2560×1440**처럼 처리 크기와 표시 크기를 맞추세요. 3840×2160 처리 결과를 2560×1440으로 표시하면 마지막 bilinear 축소가 샤프닝 차이를 줄입니다. 출력 스케일링 방식 자체는 변경하지 않았습니다.

### 수동 크롭 / 편집

1. **Crop → Manual**을 선택합니다.
2. Left / Top / Right / Bottom 슬라이더로 각 가장자리에서 제거할 비율을 **0–45%** 범위에서 조절합니다. 좌표는 입력 크기에 따라 정수 픽셀로 반올림됩니다. Fit/프리셋 또는 16:9 Lock으로 픽셀 크롭을 사용하면 슬라이더 범위를 **0–99%**로 넓히고, 반대쪽 가장자리를 넘지 않게 제한합니다. 이 경우 표시된 퍼센트는 반올림된 위치이며 실제 크기는 `Current Crop`에 표시합니다.
3. **Edit Crop (F2)**를 누르면 전체 WGC 프레임과 청록색 크롭 경계를 표시하고 바깥을 어둡게 합니다. Crop Off에서 F2를 누르면 Manual도 함께 켭니다.
4. **Finish Crop (F2)**를 누르면 선택 영역만 확대·샤프닝합니다. 편집 중에는 업스케일/RCAS/A/B를 잠시 생략하고 원본 전체를 표시합니다.

예: 1920×1080에서 Top 10%, Bottom 5% → 1920×918. 크롭 후의 비율로 출력 공간에 맞추며 검정 여백을 넣습니다. 기존 퍼센트 크롭은 창 크기가 변하면 같은 비율로 새 영역을 계산합니다. Fit/프리셋/잠금 조절로 만든 픽셀 크롭은 위치·크기를 유지하되, 입력이 줄면 프레임 안으로 이동/축소합니다. 잠금이 켜져 있으면 축소 시 16:9도 유지합니다. 아주 작은 창에서도 최소 1×1 영역을 보장합니다.

크롭용 별도 CPU 영상이나 별도 크롭 렌더 패스는 없습니다. 원본 GPU SRV의 offset와 영역 크기를 shader에 전달합니다. Bicubic/Lanczos/EASU/RCAS의 주변 샘플도 크롭 경계를 넘지 않도록 clamp합니다. Native/1:1에서 원본 SRV를 그대로 전달할 때도 영역 좌표를 유지합니다. 편집 표시가 입력이나 처리 텍스처에 기록되지 않습니다.

Crop Off는 전체 창으로 돌아갑니다. 브라우저 DOM, video 요소나 자동 영상 영역 탐지는 사용하지 않습니다.

### Input Guide / 720p 프리셋

이 기능은 **캡처된 렌더링 픽셀 크기**를 맞추는 시각적 보조 도구입니다. `Processing input: ~720p`는 서비스의 인코딩된 영상 해상도를 뜻하지 않습니다. 예를 들어 서비스가 720p 영상을 브라우저에서 1920×1080으로 그리면 WGC에는 1920×1080 렌더링 픽셀이 보입니다. 앱은 스트림 메타데이터나 브라우저 내부를 읽지 않습니다.

1. 캡처를 시작하고 **Edit Crop (F2)**를 켭니다.
2. **Input Guide**에서 Off(기본값), 1280×720, 1920×1080, Custom을 선택합니다. Custom은 폭·높이 1–16384 정수를 입력하고 포커스를 옮기면 적용됩니다. 빈 값/범위 밖 값은 이전 유효 값으로 복원합니다.
3. Preview의 **청록색 실선**은 현재 크롭, **노란색 점선**은 목표 크기입니다. 왼쪽 위의 `CROP` / `TARGET` 숫자도 GPU 오버레이입니다. 가이드는 크롭 중심을 기준으로 배치하고 가능한 경우 입력 안으로 이동합니다. 목표가 입력보다 크면 목표 크기를 축소하지 않고 화면 밖 부분을 잘라 표시하며 상태에 알립니다.
4. 우측 패널에 Captured Window, Current Crop, Processing input, Target Guide, 부호 있는 Difference와 판정을 표시합니다. **GOOD**은 폭·높이 모두 ±16픽셀, **CLOSE**는 두 축 모두 목표의 8% 이내(최소 32픽셀), 나머지는 **OFF TARGET**입니다. 예: 1274×712 → GOOD, 1210×680 → CLOSE, 1530×860 → OFF TARGET.
5. **Fit 720p**는 현재 WGC 프레임 안에서 중앙 1280×720을 정수 좌표로 지정합니다. 1366×768이면 좌측 43 / 상단 24픽셀입니다. 입력이 더 작으면 가능한 가장 큰 16:9 영역을 사용하며 `Source smaller than 720p target`을 표시합니다. 브라우저 창이나 화면 배율은 변경하지 않습니다.
6. **Aspect Lock → 16:9**는 한쪽 가장자리 조절 시 반대 축 크기를 조정하고 중앙 정렬합니다. 입력 경계를 넘으면 안으로 이동/축소하며 반올림 때문에 최대 약 1픽셀 비율 오차가 생길 수 있습니다. 이미 목표 근처인 크롭은 아래 프리셋에서 그대로 보존하고 이후 가장자리 조절부터 잠금을 적용합니다.
7. **720p → 1440p** 버튼은 Guide 1280×720, Lock 16:9, Output 2560×1440, EASU, RCAS 25를 적용합니다. 기존 크롭이 720p GOOD이면 위치·크기를 보존하고, 아니면 Fit 720p를 적용합니다. Fit/프리셋은 첫 캡처 프레임이 도착하면 활성화됩니다.
8. **Finish Crop (F2)**로 나오면 가이드와 숫자 오버레이가 없어지고 선택한 크롭만 EASU → RCAS로 처리됩니다. 가이드는 입력/중간 영상에 기록되지 않습니다.

`Processing input`은 720p/1080p/1440p 기준의 GOOD 범위에서 해당 근삿값을 표시하고 그 밖에는 `custom pixel size`를 표시합니다. 입력이 약 720p이고 Preview 모니터가 2560×1440이면 `Recommended Output: 2560 x 1440`, 입력이 약 1080p이고 모니터가 4K이면 `3840 x 2160`을 안내합니다. 추천만으로 Output을 자동 변경하지 않습니다.

### A/B 비교 / 분할선

- **Compare → Split Vertical**: 왼쪽은 bilinear·RCAS 없음, 오른쪽은 선택한 업스케일+RCAS.
- **Split Horizontal**: 위쪽은 기준, 아래쪽은 처리 결과.
- **Split 0–100%** 슬라이더 또는 Preview의 청록색 분할선을 드래그합니다. 기본값은 50%, Compare 기본값은 Off입니다. 0%는 전부 처리 결과, 100%는 전부 기준 화면입니다.
- **F1**은 Off와 마지막 비교 방향을 전환합니다. 양쪽은 동일한 캡처 프레임과 동일한 크롭·화면 비율을 사용합니다. Native 선택 시 비교 위치를 맞추기 위해 양쪽 모두 같은 native 콘텐츠 배치를 사용합니다.
- 기준 화면은 presentation shader에서 원본 SRV를 hardware bilinear 샘플링합니다. 추가 WGC 세션, 기준용 출력 텍스처나 별도 bilinear 패스는 없습니다. 비교 합성과 분할선은 presentation에만 존재합니다.
- F1/F2/F3는 앱 또는 Preview와 그 자식 컨트롤이 키 입력을 받을 때만 처리합니다. 전역 단축키를 등록하지 않습니다. Esc와 더블클릭 전체화면 동작도 유지합니다.
- 모든 옵션은 다음 WGC 프레임에 적용됩니다. 원본이 최소화되거나 프레임을 보내지 않으면 조정 내용도 다음 프레임을 기다립니다. Stop→Start에서 설정은 유지되며 파일 저장 기능은 없습니다.

## 빌드

- Windows 11, Visual Studio 2022의 **Desktop development with C++** 워크로드
- MSVC, Windows SDK 10.0.19041 이상 권장(검증 버전: 10.0.22621.0)
- CMake 3.24 이상. MinGW는 지원하지 않습니다.
- SDK에 포함된 C++/WinRT 헤더 사용. NuGet/AMD SDK/NVIDIA SDK 다운로드 불필요.

```powershell
cmake -S . -B build/phase5 -G "Visual Studio 17 2022" -A x64
cmake --build build/phase5 --config Release --parallel
.\build\phase5\Release\WindowGPUPreview.exe
```

Visual Studio에서 폴더를 열거나 `build/phase5/WindowGPUPreview.sln`을 열어 빌드할 수도 있습니다. 다른 CMake 빌드 디렉터리를 사용해도 됩니다. HLSL은 CMake 구성 시 헤더에 포함되고 앱 초기화 시 컴파일되므로 실행 파일 옆에 shader 파일이 필요하지 않습니다.

## 아키텍처

```text
EnumWindows → HWND 선택
  → IGraphicsCaptureItemInterop::CreateForWindow
  → Direct3D11CaptureFramePool (BGRA8, 2 buffers, free-threaded)
  → FrameArrived: 원자적 알림 병합 + PostMessage
  → UI thread: TryGetNextFrame, 오래된 대기 프레임 폐기
  → IDirect3DDxgiInterfaceAccess → ID3D11Texture2D
  → CopySubresourceRegion (GPU → GPU, SRV용 텍스처 재사용)
  → source SRV
  → crop sampling coordinates (no extra crop pass)
  → selected upscale pixel shader → content-sized BGRA8 GPU texture / SRV
  → RCAS (strength > 0) → reusable BGRA8 GPU texture / SRV
  → presentation: processed + optional bilinear baseline from the SAME source SRV
  → black letterbox / A-B divider / crop-edit overlay
  → DXGI flip-discard SwapChain → Present(1, 0)
```

- 캡처와 출력이 같은 D3D11 device를 사용합니다. 하드웨어 기본 어댑터를 사용하며 제조사 전용 기능에 의존하지 않습니다.
- WGC 콜백은 텍스처나 immediate context를 건드리지 않습니다. 모든 GPU 명령·pool 재생성은 UI 스레드에서 처리합니다.
- 콜백은 공유 알림 상태만 보유합니다. 중지 시 비활성화하고 이벤트를 해제하며 세대 번호로 이전 세션의 메시지를 무시합니다.
- 프레임 도착과 명시적인 샤프닝/크롭/가이드 편집 변경에 렌더링합니다. 1초 UI 타이머는 통계만 갱신합니다. 정적인 창은 프레임 도착이 멈출 수 있으며, 편집 시에는 마지막 GPU 입력을 한 번 사용합니다. 이 재표시는 Render FPS에만 포함되고 Capture FPS를 늘리지 않습니다.
- 프레임의 `ContentSize`가 바뀌면 프레임을 닫은 후 pool을 재생성합니다. 커진 콘텐츠가 이전 텍스처보다 큰 과도기 프레임은 건너뜁니다.
- production 코드에는 staging/readback/영상 데이터 Map이 없습니다. GPU 내부 복사 1회는 WGC 텍스처의 bind flag에 의존하지 않고 SRV를 확보하기 위해 사용합니다.
- `GPUUpscaler`는 캡처 API와 독립적인 렌더러 구성 요소입니다. shader/constant buffer/query는 초기화 시 생성하고, 입력 SRV용 텍스처는 입력 크기가 바뀔 때, 업스케일 중간 텍스처는 콘텐츠 출력 크기가 바뀔 때만 재생성합니다. 모드 변경만으로 텍스처를 할당하지 않습니다.
- 중간 텍스처는 콘텐츠 영역만 포함합니다. 검정 여백은 presentation에서 처리해 여백에 고비용 필터를 실행하지 않습니다. Native/1:1 + RCAS 0은 입력 SRV와 크롭 좌표를 presentation에 직접 전달합니다. RCAS 텍스처는 처리할 콘텐츠 크기가 바뀔 때만 재생성합니다. 크롭 위치·RCAS 강도·분할 위치 변경만으로 텍스처를 만들지 않습니다.
- `Present(1, 0)`으로 모니터 동기화를 사용합니다. UI 이동/크기 조절의 모달 루프나 Present 대기로 프레임이 버려질 수 있으며, 렌더 전용 스레드는 후속 최적화 대상입니다.

## 현재 구현과 이후 단계

| 단계 | 상태 |
|---|---|
| 1: Win32 UI, D3D11 device, Preview SwapChain | 구현 |
| 2: HWND 선택, Windows Graphics Capture | 구현 |
| 3: GPU 텍스처 Preview, aspect ratio, resize, stop/restart | 구현 |
| 4: Off/Bilinear/Bicubic/Lanczos/FSR EASU 선택 및 출력 설정 | 구현 및 GPU/캡처 중 전환 테스트 통과 |
| 5: RCAS, 수동 크롭/편집, A/B/분할선/로컬 단축키 | 구현, GPU 및 실제 앱 조작 자동 검증 통과 |
| 6: 색상/설정 UI, 모니터 선택 목록 | 후속 작업 — 기본 fullscreen/topmost는 구현 |
| 7: GPU timestamp, overlay, 성능 최적화 | 같은 프레임의 업스케일/RCAS/합계 시간 구현. 디버그 영상 overlay와 전체 지연 최적화는 후속 작업 |

`src/capture`, `src/renderer`, `src/ui`, `src/utils`, `src/shaders`로 분리했습니다. `common.hlsl`을 CMake에서 각 shader 앞에 붙여 포함합니다. 업스케일 shader, `rcas.hlsl`, `composite.hlsl`은 앱 초기화 시 컴파일되며 프레임마다 컴파일하지 않습니다. `GPUCompositor`는 이전 presentation 처리를 확장한 구성 요소이고 WGC 캡처 구현은 변경하지 않았습니다.

## 통계와 제한

- Input/Output 해상도, 실제 수신 프레임 처리 FPS, 성공한 Present FPS, GPU 이름을 표시합니다.
- Captured window, Crop region, Upscaler, 콘텐츠 확대 배율, Preview 해상도, RCAS 강도, 비교 모드를 표시합니다. `Input`은 이전 버전과 동일하게 캡처 창 해상도입니다.
- `GPUTimer`는 업스케일 시작/RCAS 시작/RCAS 종료의 세 timestamp와 disjoint query로 **같은 프레임**의 두 구간을 측정합니다. 6개 query 세트를 재사용하고 `GetData(DONOTFLUSH)`로 완료된 값만 읽습니다. 대기/Flush/영상 readback 없이 진행하며 미완료 시 새 측정을 건너뛸 수 있습니다. 모드·크롭·강도·입출력 크기 변경 이전의 결과와 disjoint 결과는 무시합니다.
- **Upscale GPU / RCAS GPU / Post-process total**은 GPU 입력 복사, presentation의 A/B 합성, WGC, 화면 scanout을 포함하지 않습니다. 비활성 패스는 0ms로 표시하며 합계에서도 제외합니다. 편집 중·설정 대기 중·완료된 query가 없으면 N/A입니다. GPU 구간에는 패스 설정/명령 사이 GPU 대기 시간도 포함될 수 있습니다. CPU submit 시간과 구분됩니다.
- 기존 `GPUUpscaler` 내부 단일 패스 타이밍 API도 Phase 4 테스트 호환을 위해 유지했습니다. 앱 상태의 합계는 그 값과 다른 프레임 값을 합산하지 않습니다.
- `CPU submit + Present`는 마지막 프레임의 CPU 경과 시간이며 **GPU frame time 또는 end-to-end latency가 아닙니다**.
- `Discarded queued frames`는 앱이 최신 프레임을 선택하며 명시적으로 버린 프레임의 누적 수입니다. WGC 내부 손실이나 모든 누락 프레임을 측정하는 값은 아닙니다.
- 이번 버전은 SDR BGRA8입니다. HDR의 정확한 색상/톤 매핑은 지원하지 않습니다.
- 캡처 대상은 창 전체입니다. 수동 크롭만 지원하며 브라우저 영상 영역 자동 감지는 구현하지 않았습니다.
- Windows의 캡처 테두리를 유지합니다. 보호/DRM 콘텐츠, 보안 화면, 캡처 제외 창은 검정 또는 캡처 실패가 될 수 있습니다. 우회 기능은 없습니다. 검정 화면만으로 DRM 여부를 확정하지 않습니다.
- 최소화된 창은 새 프레임을 생성하지 않을 수 있습니다. 원본을 복원하세요.
- GPU device loss는 오류를 표시하고 캡처를 중지합니다. 드라이버 리셋 후에는 앱을 다시 실행하세요.
- 720p→4K 60fps와 추가 지연 16/33ms는 목표입니다. 아래 측정은 업스케일 패스의 처리 비용이며 실제 브라우저 영상의 end-to-end FPS/지연 보장은 아닙니다.

## 검증

이 작업 환경의 **AMD Radeon RX 6700 XT**, MSVC 19.37, Windows SDK 22621에서 Release 빌드와 아래 검증을 수행했습니다.

잠금 해제된 로컬 Windows 데스크톱에서:

```powershell
ctest --test-dir build/phase5 -C Release -V
```

- `capture_smoke`: 자체 테스트 창을 캡처하고 중앙 픽셀 검증, GPU 출력, 원본/Preview 크기 변경, 재시작, 원본 종료, 반복 Stop을 확인합니다. **픽셀 readback은 테스트 코드에만 존재합니다.**
- `app_smoke`: 실제 앱을 실행해 테스트 창 목록 선택 → Start → 수신/출력 통계 → 전체화면/Esc → topmost → Stop/재시작 → Preview 닫기 → 앱 종료를 확인합니다.
- `upscale_gpu`: 720p→1080p/1440p/4K의 모든 모드, bilinear/cubic/sinc 수학 참조와의 픽셀 비교, EASU의 2×2 색상 범위 제한, 서로 다른 필터 출력, 경계 상수색, Native/1:1 우회, 리소스 재사용, EASU 축소 fallback을 확인합니다. 설치된 D3D11 debug layer에서 warning/error가 없는지도 검사합니다.
- `upscale_capture_ui`: 실제 1280×720 창을 WGC로 캡처하면서 5개 모드×3개 출력 해상도 전환, 640×480 원본 크기 변경, Preview 크기 변경, 비율 유지, Auto 모니터 해상도, Stop→설정 변경→Start를 검증합니다.
- `phase5_gpu`: RCAS 0 bypass/강도 변화/상수색/1080p·1440p·4K/리소스 재사용, float 출력의 NaN·Inf·범위, no/top/asymmetric/16:9 크롭의 모든 업스케일 모드, crop 좌표 참조, A/B 두 방향과 0/25/50/75/100%, 오버레이 분리, paired GPU timing을 검사합니다. D3D11 debug layer 경고·오류도 검사합니다.
- `phase5_capture_ui`: 실제 새 앱과 자체 품질 테스트 창에서 Manual/크롭 슬라이더/편집 버튼/F2, 크롭 중 원본 리사이즈, RCAS/F3, A/B/F1, 분할선 드래그, Stop/Start를 조작합니다. 설정 조작 내내 같은 WGC 세대 번호가 유지되는지 검사합니다. 이는 실제 앱의 UI 자동화 검증이며 사람의 장시간 실영상 시청 평가를 대체하지 않습니다.
- 생성된 `build/phase5/validation/*.bmp`의 앱 UI, 크롭 경계/바깥 어둡게 표시, 수직·수평 비교 및 EASU-only/RCAS 결과를 시각 확인했습니다. 테스트만 이미지 readback과 캡처 파일 출력을 수행합니다.
- 개발 시 환경 변수 `MOVIEUPSCALING_DEBUG_D3D=1`로 앱의 D3D11 debug device를 요청할 수 있습니다. SDK debug layer가 없으면 일반 hardware device로 동작합니다.
- 테스트 중 자체 테스트 창과 Preview가 잠깐 표시됩니다. 다른 사용자의 창이나 콘텐츠를 캡처하지 않습니다.
- 샌드박스/비대화형 서비스 세션에서는 WGC 활성화가 실패할 수 있습니다. 일반 로컬 데스크톱에서 실행하세요.
- 데스크톱 테스트끼리 창을 혼동하거나 GPU 부하를 간섭하지 않도록 CTest `RUN_SERIAL`을 사용합니다. 여러 물리 모니터 간 이동과 HDR은 자동 검증 범위에 포함되지 않습니다.

### RCAS 선명도 수정 검증

RCAS 선명도 수정 시점에 RX 6700 XT / Release에서 기존 6개와 새 4개를 포함한 **CTest 10개 모두 통과**했습니다. RCAS 검증은 다음과 같습니다.

- `rcas_perceptual`: 동일한 720p 합성 정지 프레임의 글자, 대각선, 1–2픽셀 선, 체커, 그라데이션과 평탄한 색상 영역을 사용합니다. EASU → RCAS → compositor를 **1440p 1:1**로 검증합니다. 고정 엣지 마스크에서 RMS 대비가 25→50→100으로 증가하는지, 100의 평균 변화량이 3/255 이상인지, 로컬 overshoot가 40/255 이하인지 검사합니다. 평탄 영역의 동일성, 그라데이션 안정성, float NaN/Inf/SDR 범위, 0의 정확한 bypass/무할당, 리소스 재사용과 debug boost도 검사합니다.
- 측정된 엣지 RMS는 **0: 21.92, 25: 23.83, 50: 25.86, 100: 33.00**입니다. 0 대비 평균 엣지 변화량은 25에서 **1.40/255**, 100에서 **7.16/255**였습니다. 100의 최대 로컬 overshoot는 **34/255**이고 평탄 영역은 동일했습니다. 이는 합성 차트의 회귀 기준이며 모든 영상의 주관적 품질 점수는 아닙니다.
- 4K 처리 → 1440p presentation의 평균 엣지 변화량은 **4.22/255**로, 1440p 1:1의 **7.16/255**보다 작았습니다. 이 테스트로 마지막 축소에 의한 효과 감소와 RCAS bypass를 구분합니다.
- `rcas_runtime`: **실제 WGC 프레임 한 장만 받은 후 캡처를 중지**하고 production `D3DRenderer`의 동일한 draw 함수로 강도 0/25/100/0/100을 처리합니다. 실제 1440p SwapChain을 Present 직전에 테스트 코드로 읽어서 RCAS 결과가 도달하는지 확인합니다. EASU SRV를 실수로 RCAS 대신 넘기면 강도 차이 검사가 실패합니다. GPU 프레임 재사용, Stop/Clear 시 무효화도 검사합니다.
- `rcas_paused_ui`, `rcas_debug_ui`: 실제 앱을 실행하여 동일한 정지 차트의 WGC 캡처, 1440p 출력/Preview, 0/25/100 슬라이더, F3 즉시 bypass/복원과 debug 상태를 확인합니다. 캡처 세션이 추가로 생성되지 않는지도 검사합니다. UI 테스트는 자동 조작이며 실제 영화 장면에 대한 장시간 시청 평가는 수행하지 않았습니다.
- `rcas_perceptual`과 `rcas_runtime`은 설치된 D3D11 debug layer의 warning/error가 있으면 실패합니다. UI 테스트도 debug device를 요청합니다. GPU 픽셀 readback/파일 저장 코드는 모두 `tests/`에만 있습니다.
- `build/phase5/validation/`에 `rcas-0.bmp`, `rcas-25.bmp`, `rcas-50.bmp`, `rcas-100.bmp`, `rcas-split.bmp`(왼쪽 0 / 오른쪽 100), `rcas-source.bmp`, `rcas-debug-100.bmp`, `rcas-4k-to-1440p.bmp`를 저장합니다. `runtime-rcas-0/25/100.bmp`는 WGC 입력이 실제 렌더러/SwapChain을 거친 결과입니다. 이 이미지에서 선명도 차이와 평탄 영역을 시각 확인했습니다.

### Input Guide 검증

RX 6700 XT / Release에서 기존 10개와 입력 가이드 3개를 포함한 **CTest 13개 모두 통과**했습니다. 실제 Chrome 검증도 포함하며, GPU 검증의 D3D11 debug layer 경고·오류는 없었습니다.

- `input_guide`: 720p/1080p/Custom 목표, GOOD/CLOSE/OFF TARGET과 경계값, 큰/정확한/작은 입력의 Fit 및 중앙 정렬, 1픽셀부터 16K까지의 크기에서 16:9 잠금·경계 제한·리사이즈, Processing input/추천을 검사합니다. 실제 compositor의 편집 가이드가 원본 픽셀을 바꾸지 않고 일반 출력에는 전혀 남지 않는지 GPU 결과를 비교합니다. D3D11 debug warning/error가 있으면 실패합니다.
- `rcas_runtime`을 확장하여 이미 GOOD인 비대칭 크롭 보존, 프리셋의 실제 renderer 설정, 편집 종료 후 크롭된 EASU/RCAS 출력에서 가이드 제거를 검사합니다.
- `input_guide_ui`: 실제 앱에서 세 가지 가이드, Custom 입력, Fit/GOOD, 잠금, 프리셋, 편집 종료와 작은 입력을 조작합니다. 기존 캡처 세션을 유지하며 원본 창의 위치·크기가 바뀌지 않는지 확인합니다.
- `input_guide_chrome`: Chrome이 설치되어 있으면 CMake가 등록합니다. 별도의 테스트 프로필과 로컬 HTML 페이지로 실제 Chrome 창을 열고, WGC 캡처 → Crop Edit → 720p Guide → Fit → GOOD → 프리셋 → 편집 종료/RCAS 재개를 검증합니다. 기존 사용자 브라우저 프로필을 사용하거나 DOM/인코딩 정보에 접근하지 않습니다. 이 환경에서 실제 Chrome의 **1522×953** 캡처를 **1280×720**으로 맞추고 GOOD 표시와 브라우저 창 크기 유지가 확인됐습니다.
- 검증 이미지: `build/phase5/validation/guide-720.bmp`, `guide-1080.bmp`, `guide-custom.bmp`, `guide-runtime-edit.bmp`, `guide-ui.bmp`, `guide-chrome-ui.bmp`, `guide-chrome-source.bmp`. Chrome 테스트 파일/별도 프로필도 이 검증 디렉터리 안에 생성됩니다.

### GPU 패스 벤치마크 (Phase 4–5 최초 측정)

`upscale_gpu`는 GPU에서 매번 다른 움직이는 패턴을 생성하고 180프레임의 720p→4K EASU를 실행합니다. 워밍업 이후 완료된 timestamp 표본의 중앙값/p95를 출력합니다. 테스트용 offscreen 제출 `Flush`와 짧은 pacing은 테스트 코드에만 있습니다.

최초 통과한 Release 측정: **중앙값 2.881ms, p95 2.904ms, 159개 표본**, RX 6700 XT, D3D11 debug layer 활성화. 이 수치는 단일 합성 영상 실행 결과이고 GPU 클럭/다른 앱 부하에 따라 변합니다. WGC와 Present를 포함한 60fps 실영상 재생은 별도로 측정해야 합니다.

Phase 5 최초 통과 측정: **EASU 중앙값 2.962ms, RCAS 25 중앙값 1.025ms, 합계 중앙값 3.987ms**, RX 6700 XT, Release 및 debug layer 활성화. `phase5_gpu`가 GPU에서 150개의 서로 다른 움직이는 패턴을 생성하고 워밍업 이후 같은 프레임의 세 timestamp를 수집합니다. RCAS 비용은 이 실행에서 EASU의 약 35%였습니다. 이는 합성 영상의 GPU 후처리 구간이며 4K 전체화면 실영상의 end-to-end 60fps나 추가 지연을 보장하는 수치가 아닙니다.

## API 참고

- [HWND에서 GraphicsCaptureItem 생성](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [CreateFreeThreaded: 작업 스레드에서 프레임 도착 알림](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded?view=winrt-26100)
- [Microsoft Win32 캡처 예제](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/ScreenCaptureforHWND)
- [AMD FSR 1 EASU/RCAS 공개 알고리즘](https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h)
- [D3D11 timestamp/disjoint 유효성](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint)
