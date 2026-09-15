# Window GPU Preview — Phase 1–4

Windows 11에서 실행 중인 창을 선택하여 Windows Graphics Capture로 캡처하고, D3D11 GPU에서 확대해 별도 Preview 창에 출력하는 C++20/Win32 앱입니다. 기존 캡처 수명 관리와 프레임 도착 기반 렌더링을 유지하며 Phase 4 업스케일 패스를 추가했습니다.

## 실행

빌드된 실행 파일: `build/Release/WindowGPUPreview.exe`

1. 캡처할 브라우저/VLC/프로그램 창을 열고 최소화를 해제합니다.
2. 앱에서 **Refresh** → **Source window** 선택 → **Start**.
3. Preview 크기를 조절하거나 **Fullscreen** / Preview 더블클릭으로 전체화면을 전환합니다.
4. **Esc**는 전체화면을 종료합니다. **Always on top**은 Preview에 적용됩니다.
5. 다른 모니터를 사용하려면 Preview를 해당 모니터로 옮긴 다음 전체화면을 켭니다. 현재 모니터 전체 크기를 사용합니다.
6. **Stop**은 캡처를 해제하고 Preview를 검정으로 지웁니다. Preview 닫기도 캡처를 중지하며 Start로 다시 열 수 있습니다.

### Upscale / Output

**Upscale**과 **Output**은 캡처 중에도 변경할 수 있습니다. 다음 캡처 프레임부터 적용하고 캡처 세션은 재시작하지 않습니다. 정지 영상에서 WGC가 새 프레임을 보내지 않으면 `pending next frame`으로 표시합니다. 설정만 변경했다고 동일 프레임을 반복 처리하지 않습니다. 설정은 Stop/Start 동안 유지되며 앱을 종료하면 기본값으로 돌아갑니다.

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

원본 대비 차이: RGB clamped load 사용, FP32 reciprocal/rsqrt와 0 나눗셈 방지, 3x 같은 정수 배율에서 부동소수점 오차로 픽셀 구간이 바뀌지 않도록 경계 좌표 보정. 따라서 AMD reference의 근사 산술과 bit-exact한 결과를 목표로 하지는 않습니다. **RCAS나 별도 sharpening은 구현하지 않았습니다.**

AMD SDK 전체를 포함하지 않습니다. 저작권과 MIT 허가는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에 있으며 빌드 시 실행 파일 옆에도 복사합니다. 배포 시 함께 포함하세요.

## 빌드

- Windows 11, Visual Studio 2022의 **Desktop development with C++** 워크로드
- MSVC, Windows SDK 10.0.19041 이상 권장(검증 버전: 10.0.22621.0)
- CMake 3.24 이상. MinGW는 지원하지 않습니다.
- SDK에 포함된 C++/WinRT 헤더 사용. NuGet/AMD SDK/NVIDIA SDK 다운로드 불필요.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
.\build\Release\WindowGPUPreview.exe
```

Visual Studio에서 폴더를 열거나 `build/WindowGPUPreview.sln`을 열어 빌드할 수도 있습니다. HLSL은 CMake 구성 시 헤더에 포함되고 앱 초기화 시 컴파일되므로 실행 파일 옆에 shader 파일이 필요하지 않습니다.

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
  → selected upscale pixel shader → content-sized BGRA8 GPU texture / SRV
  → presentation shader + black letterbox / native crop
  → DXGI flip-discard SwapChain → Present(1, 0)
```

- 캡처와 출력이 같은 D3D11 device를 사용합니다. 하드웨어 기본 어댑터를 사용하며 제조사 전용 기능에 의존하지 않습니다.
- WGC 콜백은 텍스처나 immediate context를 건드리지 않습니다. 모든 GPU 명령·pool 재생성은 UI 스레드에서 처리합니다.
- 콜백은 공유 알림 상태만 보유합니다. 중지 시 비활성화하고 이벤트를 해제하며 세대 번호로 이전 세션의 메시지를 무시합니다.
- 프레임 도착에만 렌더링합니다. 1초 UI 타이머는 통계만 갱신합니다. 정적인 창은 프레임 도착이 멈출 수 있습니다.
- 프레임의 `ContentSize`가 바뀌면 프레임을 닫은 후 pool을 재생성합니다. 커진 콘텐츠가 이전 텍스처보다 큰 과도기 프레임은 건너뜁니다.
- production 코드에는 staging/readback/영상 데이터 Map이 없습니다. GPU 내부 복사 1회는 WGC 텍스처의 bind flag에 의존하지 않고 SRV를 확보하기 위해 사용합니다.
- `GPUUpscaler`는 캡처 API와 독립적인 렌더러 구성 요소입니다. shader/constant buffer/query는 초기화 시 생성하고, 입력 SRV용 텍스처는 입력 크기가 바뀔 때, 업스케일 중간 텍스처는 콘텐츠 출력 크기가 바뀔 때만 재생성합니다. 모드 변경만으로 텍스처를 할당하지 않습니다.
- 중간 텍스처는 콘텐츠 영역만 포함합니다. 검정 여백은 presentation에서 처리해 여백에 고비용 필터를 실행하지 않습니다. Native/1:1에서는 입력 SRV를 presentation에 직접 전달합니다.
- `Present(1, 0)`으로 모니터 동기화를 사용합니다. UI 이동/크기 조절의 모달 루프나 Present 대기로 프레임이 버려질 수 있으며, 렌더 전용 스레드는 후속 최적화 대상입니다.

## 현재 구현과 이후 단계

| 단계 | 상태 |
|---|---|
| 1: Win32 UI, D3D11 device, Preview SwapChain | 구현 |
| 2: HWND 선택, Windows Graphics Capture | 구현 |
| 3: GPU 텍스처 Preview, aspect ratio, resize, stop/restart | 구현 |
| 4: Off/Bilinear/Bicubic/Lanczos/FSR EASU 선택 및 출력 설정 | 구현 및 GPU/캡처 중 전환 테스트 통과 |
| 5: RCAS/sharpen 강도 및 halo clamp | 후속 작업 |
| 6: 색상/설정 UI, 모니터 선택 목록 | 후속 작업 — 기본 fullscreen/topmost는 구현 |
| 7: GPU timestamp, overlay, 성능 최적화 | 업스케일 GPU 시간 구현. overlay와 전체 지연 최적화는 후속 작업 |

`src/capture`, `src/renderer`, `src/ui`, `src/utils`, `src/shaders`로 분리했습니다. `common.hlsl`을 CMake에서 각 shader 앞에 붙여 포함합니다. `present.hlsl`, `upscale_bicubic.hlsl`, `upscale_lanczos.hlsl`, `upscale_easu.hlsl`은 앱 초기화 시 컴파일되며 프레임마다 컴파일하지 않습니다.

## 통계와 제한

- Input/Output 해상도, 실제 수신 프레임 처리 FPS, 성공한 Present FPS, GPU 이름을 표시합니다.
- Upscaler, 콘텐츠 확대 배율, Preview 해상도, **Upscale GPU time**을 추가했습니다.
- GPU 시간은 업스케일 `Draw` 전후 D3D11 timestamp와 disjoint query로 측정합니다. 6개 query 세트를 재사용하고 `GetData(DONOTFLUSH)`로 완료된 값만 읽습니다. 대기/flush/영상 readback 없이 진행하며 미완료 시 새 측정을 건너뛸 수 있습니다. 모드·입출력 크기 변경 이전의 결과와 disjoint 결과는 무시합니다.
- GPU 시간은 GPU 내부 입력 복사, presentation, WGC, 화면 scanout을 포함하지 않는 **업스케일 패스만의 시간**입니다. Native/1:1은 `N/A (bypass)`, 유효한 결과가 아직 없으면 `N/A`입니다.
- `CPU submit + Present`는 마지막 프레임의 CPU 경과 시간이며 **GPU frame time 또는 end-to-end latency가 아닙니다**.
- `Discarded queued frames`는 앱이 최신 프레임을 선택하며 명시적으로 버린 프레임의 누적 수입니다. WGC 내부 손실이나 모든 누락 프레임을 측정하는 값은 아닙니다.
- 이번 버전은 SDR BGRA8입니다. HDR의 정확한 색상/톤 매핑은 지원하지 않습니다.
- 캡처 대상은 창 전체입니다. 브라우저 영상 영역 자동 감지/자르기는 구현하지 않았습니다.
- Windows의 캡처 테두리를 유지합니다. 보호/DRM 콘텐츠, 보안 화면, 캡처 제외 창은 검정 또는 캡처 실패가 될 수 있습니다. 우회 기능은 없습니다. 검정 화면만으로 DRM 여부를 확정하지 않습니다.
- 최소화된 창은 새 프레임을 생성하지 않을 수 있습니다. 원본을 복원하세요.
- GPU device loss는 오류를 표시하고 캡처를 중지합니다. 드라이버 리셋 후에는 앱을 다시 실행하세요.
- 720p→4K 60fps와 추가 지연 16/33ms는 목표입니다. 아래 측정은 업스케일 패스의 처리 비용이며 실제 브라우저 영상의 end-to-end FPS/지연 보장은 아닙니다.

## 검증

이 작업 환경의 **AMD Radeon RX 6700 XT**, MSVC 19.37, Windows SDK 22621에서 Release 빌드와 아래 검증을 수행했습니다.

잠금 해제된 로컬 Windows 데스크톱에서:

```powershell
ctest --test-dir build -C Release -V
```

- `capture_smoke`: 자체 테스트 창을 캡처하고 중앙 픽셀 검증, GPU 출력, 원본/Preview 크기 변경, 재시작, 원본 종료, 반복 Stop을 확인합니다. **픽셀 readback은 테스트 코드에만 존재합니다.**
- `app_smoke`: 실제 앱을 실행해 테스트 창 목록 선택 → Start → 수신/출력 통계 → 전체화면/Esc → topmost → Stop/재시작 → Preview 닫기 → 앱 종료를 확인합니다.
- `upscale_gpu`: 720p→1080p/1440p/4K의 모든 모드, bilinear/cubic/sinc 수학 참조와의 픽셀 비교, EASU의 2×2 색상 범위 제한, 서로 다른 필터 출력, 경계 상수색, Native/1:1 우회, 리소스 재사용, EASU 축소 fallback을 확인합니다. 설치된 D3D11 debug layer에서 warning/error가 없는지도 검사합니다.
- `upscale_capture_ui`: 실제 1280×720 창을 WGC로 캡처하면서 5개 모드×3개 출력 해상도 전환, 640×480 원본 크기 변경, Preview 크기 변경, 비율 유지, Auto 모니터 해상도, Stop→설정 변경→Start를 검증합니다.
- 테스트 중 자체 테스트 창과 Preview가 잠깐 표시됩니다. 다른 사용자의 창이나 콘텐츠를 캡처하지 않습니다.
- 샌드박스/비대화형 서비스 세션에서는 WGC 활성화가 실패할 수 있습니다. 일반 로컬 데스크톱에서 실행하세요.
- 데스크톱 테스트끼리 창을 혼동하거나 GPU 부하를 간섭하지 않도록 CTest `RUN_SERIAL`을 사용합니다. 여러 물리 모니터 간 이동과 HDR은 자동 검증 범위에 포함되지 않습니다.

### GPU 패스 벤치마크

`upscale_gpu`는 GPU에서 매번 다른 움직이는 패턴을 생성하고 180프레임의 720p→4K EASU를 실행합니다. 워밍업 이후 완료된 timestamp 표본의 중앙값/p95를 출력합니다. 테스트용 offscreen 제출 `Flush`와 짧은 pacing은 테스트 코드에만 있습니다.

최초 통과한 Release 측정: **중앙값 2.881ms, p95 2.904ms, 159개 표본**, RX 6700 XT, D3D11 debug layer 활성화. 이 수치는 단일 합성 영상 실행 결과이고 GPU 클럭/다른 앱 부하에 따라 변합니다. WGC와 Present를 포함한 60fps 실영상 재생은 별도로 측정해야 합니다.

## API 참고

- [HWND에서 GraphicsCaptureItem 생성](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [CreateFreeThreaded: 작업 스레드에서 프레임 도착 알림](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded?view=winrt-26100)
- [Microsoft Win32 캡처 예제](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/ScreenCaptureforHWND)
- [AMD FSR 1 EASU 공개 알고리즘](https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h)
- [D3D11 timestamp/disjoint 유효성](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint)
