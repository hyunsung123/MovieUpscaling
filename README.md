# Window GPU Preview — Phase 1–3

Windows 11에서 실행 중인 창을 선택하여 Windows Graphics Capture로 캡처하고, D3D11 GPU 텍스처를 별도 Preview 창에 출력하는 C++20/Win32 앱입니다.

## 실행

빌드된 실행 파일: `build/Release/WindowGPUPreview.exe`

1. 캡처할 브라우저/VLC/프로그램 창을 열고 최소화를 해제합니다.
2. 앱에서 **Refresh** → **Source window** 선택 → **Start**.
3. Preview 크기를 조절하거나 **Fullscreen** / Preview 더블클릭으로 전체화면을 전환합니다.
4. **Esc**는 전체화면을 종료합니다. **Always on top**은 Preview에 적용됩니다.
5. 다른 모니터를 사용하려면 Preview를 해당 모니터로 옮긴 다음 전체화면을 켭니다. 현재 모니터 전체 크기를 사용합니다.
6. **Stop**은 캡처를 해제하고 Preview를 검정으로 지웁니다. Preview 닫기도 캡처를 중지하며 Start로 다시 열 수 있습니다.

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
  → aspect-fit bilinear pixel shader + black letterbox
  → DXGI flip-discard SwapChain → Present(1, 0)
```

- 캡처와 출력이 같은 D3D11 device를 사용합니다. 하드웨어 기본 어댑터를 사용하며 제조사 전용 기능에 의존하지 않습니다.
- WGC 콜백은 텍스처나 immediate context를 건드리지 않습니다. 모든 GPU 명령·pool 재생성은 UI 스레드에서 처리합니다.
- 콜백은 공유 알림 상태만 보유합니다. 중지 시 비활성화하고 이벤트를 해제하며 세대 번호로 이전 세션의 메시지를 무시합니다.
- 프레임 도착에만 렌더링합니다. 1초 UI 타이머는 통계만 갱신합니다. 정적인 창은 프레임 도착이 멈출 수 있습니다.
- 프레임의 `ContentSize`가 바뀌면 프레임을 닫은 후 pool을 재생성합니다. 커진 콘텐츠가 이전 텍스처보다 큰 과도기 프레임은 건너뜁니다.
- production 코드에는 staging/readback/영상 데이터 Map이 없습니다. GPU 내부 복사 1회는 WGC 텍스처의 bind flag에 의존하지 않고 SRV를 확보하기 위해 사용합니다.
- `Present(1, 0)`으로 모니터 동기화를 사용합니다. UI 이동/크기 조절의 모달 루프나 Present 대기로 프레임이 버려질 수 있으며, 렌더 전용 스레드와 GPU 타임스탬프는 후속 최적화 대상입니다.

## 현재 구현과 이후 단계

| 단계 | 상태 |
|---|---|
| 1: Win32 UI, D3D11 device, Preview SwapChain | 구현 |
| 2: HWND 선택, Windows Graphics Capture | 구현 |
| 3: GPU 텍스처 Preview, aspect ratio, resize, stop/restart | 구현 |
| 4: Off/Bilinear/Bicubic/Lanczos/FSR EASU 선택 및 출력 설정 | 후속 작업 — 현재는 표시용 bilinear만 사용 |
| 5: RCAS/sharpen 강도 및 halo clamp | 후속 작업 |
| 6: 색상/설정 UI, 모니터 선택 목록 | 후속 작업 — 기본 fullscreen/topmost는 구현 |
| 7: GPU timestamp, overlay, 성능 최적화 | 후속 작업 |

`src/capture`, `src/renderer`, `src/ui`, `src/utils`, `src/shaders`로 분리했습니다. 실제 구현 전의 `upscale.hlsl`, `sharpen.hlsl`, `color.hlsl` 가짜 기능 파일은 추가하지 않았습니다. 현재 shader는 `present.hlsl`입니다.

## 통계와 제한

- Input/Output 해상도, 실제 수신 프레임 처리 FPS, 성공한 Present FPS, GPU 이름을 표시합니다.
- `CPU submit + Present`는 마지막 프레임의 CPU 경과 시간이며 **GPU frame time 또는 end-to-end latency가 아닙니다**.
- `Discarded queued frames`는 앱이 최신 프레임을 선택하며 명시적으로 버린 프레임의 누적 수입니다. WGC 내부 손실이나 모든 누락 프레임을 측정하는 값은 아닙니다.
- 이번 버전은 SDR BGRA8입니다. HDR의 정확한 색상/톤 매핑은 지원하지 않습니다.
- 캡처 대상은 창 전체입니다. 브라우저 영상 영역 자동 감지/자르기는 구현하지 않았습니다.
- Windows의 캡처 테두리를 유지합니다. 보호/DRM 콘텐츠, 보안 화면, 캡처 제외 창은 검정 또는 캡처 실패가 될 수 있습니다. 우회 기능은 없습니다. 검정 화면만으로 DRM 여부를 확정하지 않습니다.
- 최소화된 창은 새 프레임을 생성하지 않을 수 있습니다. 원본을 복원하세요.
- GPU device loss는 오류를 표시하고 캡처를 중지합니다. 드라이버 리셋 후에는 앱을 다시 실행하세요.
- 720p→4K 60fps, 추가 지연 16/33ms 목표는 아직 벤치마크하지 않았으며 보장하지 않습니다.

## 검증

이 작업 환경의 **AMD Radeon RX 6700 XT**에서 MSVC Release 빌드와 아래 두 통합 테스트를 통과했습니다. 이는 기능 검증이며 4K 업스케일 성능 벤치마크 결과는 아닙니다.

잠금 해제된 로컬 Windows 데스크톱에서:

```powershell
ctest --test-dir build -C Release -V
```

- `capture_smoke`: 자체 테스트 창을 캡처하고 중앙 픽셀 검증, GPU 출력, 원본/Preview 크기 변경, 재시작, 원본 종료, 반복 Stop을 확인합니다. **픽셀 readback은 테스트 코드에만 존재합니다.**
- `app_smoke`: 실제 앱을 실행해 테스트 창 목록 선택 → Start → 수신/출력 통계 → 전체화면/Esc → topmost → Stop/재시작 → Preview 닫기 → 앱 종료를 확인합니다.
- 테스트 중 자체 테스트 창과 Preview가 잠깐 표시됩니다. 다른 사용자의 창이나 콘텐츠를 캡처하지 않습니다.
- 샌드박스/비대화형 서비스 세션에서는 WGC 활성화가 실패할 수 있습니다. 일반 로컬 데스크톱에서 실행하세요.

## API 참고

- [HWND에서 GraphicsCaptureItem 생성](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [CreateFreeThreaded: 작업 스레드에서 프레임 도착 알림](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded?view=winrt-26100)
- [Microsoft Win32 캡처 예제](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/ScreenCaptureforHWND)
