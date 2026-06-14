# Triangle

동일한 무지개 삼각형을 4가지 그래픽스 환경에서 구현한 예제 모음.

| 버전 | 환경 | 언어 |
|------|------|------|
| OpenGL | VS2022 + GLFW + GLEW + GLM | C++ |
| DirectX 12 | VS2022 + D3D12 + DirectXMath | C++ |
| Three.js | 브라우저 단일 HTML | JavaScript |
| R3F + WebGPU | Vite + React + three/webgpu + TSL | TypeScript |

---

## OpenGL

### 패키지 설치
VS2022 패키지 관리자 콘솔:
```powershell
Install-Package glfw
Install-Package GLEW-static
Install-Package glm
```

### 주의사항
- 플랫폼 **x64** 필수 (`빌드 > 구성 관리자`)
- `GLEW-static` 사용 시 코드 상단에 `#define GLEW_STATIC` 필요

### 실행
1. 빈 C++ 프로젝트 생성
2. `opengl/main.cpp` 추가
3. `F5`

> ESC로 종료

---

## DirectX 12

### 주의사항
- D3D12는 Windows SDK 내장 — 별도 NuGet 불필요
- `d3d12.lib`, `dxgi.lib`, `d3dcompiler.lib`은 `#pragma comment`로 자동 링크
- 서브시스템: **콘솔 (`/SUBSYSTEM:CONSOLE`)** + 진입점 `main()` 사용
- 플랫폼 **x64** 필수

### 실행
1. 빈 C++ 프로젝트 생성
2. `directx12/main.cpp` 추가
3. `F5`

> ESC로 종료

---

## Three.js

별도 설치 없음. CDN으로 로드.

### 실행
`threejs/index.html`을 브라우저에서 바로 열기.

---

## R3F + WebGPU

### 요구사항
- Node.js 18+
- Chrome 113+ / Edge 113+  
  (WebGPU 미지원 환경은 자동으로 WebGL2 폴백)

### 세팅
```bash
npm create vite@latest my-app -- --template react-ts
cd my-app
npm install
npm install three @react-three/fiber
npm install -D @types/three
```

### 실행
```bash
npm run dev
# → http://localhost:5173
```
