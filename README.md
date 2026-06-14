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


### 실행
1. 빈 C++ 프로젝트 생성
2. 패키지 설치
3. 소스코드 추가 후 실행

---

## DirectX 12

### 실행
1. 빈 C++ 프로젝트 생성
2. 소스코드 추가 후 실행

---

## Three.js

### 실행
`index.html`을 브라우저에서 바로 열기.
or 서버로 열기

---

## R3F + WebGPU

### 세팅
```bash
npm create vite@latest my-app -- --template react-ts
cd my-app
npm install
npm install three @react-three/fiber
npm install -D @types/three
```
이후 App.tsx파일 교체

### 실행
```bash
npm run dev
# → http://localhost:5173
```
