#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "three_bundle_gz.h"

// ================= ตั้งค่า Wi-Fi Access Point ของบอร์ด =================
const char* AP_SSID = "SmartParking_Mall";
const char* AP_PASS = "12345678";

// ================= ตั้งค่าขาอุปกรณ์ =================
const int pingPin     = 5;   // ขา Trig (GPIO 5 / D1)
const int inPin       = 4;   // ขา Echo (GPIO 4 / D2)
const int redLedPin   = D5;  // ไฟ LED สีแดง ต่อที่ D5 (GPIO 14)
const int greenLedPin = D6;  // ไฟ LED สีเขียว ต่อที่ D6 (GPIO 12)

const int DIST_THRESHOLD_CM = 10; // ระยะเข้าใกล้ <= 10 ซม. ถือว่ามีรถจอด

ESP8266WebServer server(80);

// สถานะลานจอด 20 ช่อง (0 = A1 ใช้เซ็นเซอร์จริง, 1..19 = A2..D5 จำลองได้)
bool slots[20] = {
  false, true,  false, true,  false, // แถว A (A1 - A5)
  true,  false, false, true,  true,  // แถว B (B1 - B5)
  false, true,  true,  false, false, // แถว C (C1 - C5)
  true,  false, true,  false, false  // แถว D (D1 - D5)
};

long currentCm = 0;
unsigned long lastMeasureMs = 0;

long microsecondsToCentimeters(long microseconds);
void readParkingSensor();
void handleRoot();
void handleThreeJs();
void handleStatus();
void handleToggle();

// ================= หน้าเว็บ 3D Three.js เก็บใน Flash (PROGMEM) =================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Smart Parking 3D - ชั้น 1</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    html, body {
      width: 100%; height: 100%; overflow: hidden;
      background: #070b14;
      color: #f8fafc;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    }
    #canvas-wrap { position: fixed; inset: 0; }

    /* HUD บน */
    .hud-top {
      position: fixed; top: 12px; left: 12px; right: 12px;
      display: flex; flex-wrap: wrap; justify-content: space-between; align-items: flex-start;
      gap: 10px; pointer-events: none; z-index: 10;
    }
    .panel {
      pointer-events: auto;
      background: rgba(15, 23, 42, 0.82);
      backdrop-filter: blur(10px);
      -webkit-backdrop-filter: blur(10px);
      border: 1px solid rgba(148, 163, 184, 0.25);
      border-radius: 14px;
      padding: 10px 14px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.45);
    }
    .title-row { display: flex; align-items: center; gap: 8px; }
    .title-row h1 { font-size: 1.05rem; font-weight: 800; letter-spacing: 0.2px; }
    .badge-3d {
      background: linear-gradient(135deg, #3b82f6, #06b6d4);
      color: #fff; font-size: 0.7rem; font-weight: 800;
      padding: 2px 8px; border-radius: 999px;
    }
    .sub { font-size: 0.74rem; color: #94a3b8; margin-top: 2px; }

    .stats-row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    .stat-pill {
      display: flex; flex-direction: column; align-items: center;
      min-width: 68px; padding: 4px 10px;
      border-radius: 10px;
      background: rgba(30, 41, 59, 0.75);
      border: 1px solid rgba(148, 163, 184, 0.2);
    }
    .stat-pill .lbl { font-size: 0.65rem; color: #94a3b8; }
    .stat-pill .val { font-size: 1.15rem; font-weight: 800; }
    .c-free { color: #4ade80; }
    .c-busy { color: #f87171; }
    .c-dist { color: #38bdf8; }

    .conn-pill {
      display: inline-flex; align-items: center; gap: 6px;
      font-size: 0.75rem; font-weight: 700;
      padding: 5px 10px; border-radius: 999px;
      background: rgba(16, 185, 129, 0.16);
      border: 1px solid rgba(16, 185, 129, 0.45);
      color: #34d399;
    }
    .conn-pill.offline {
      background: rgba(239, 68, 68, 0.16);
      border-color: rgba(239, 68, 68, 0.45);
      color: #f87171;
    }
    .dot { width: 8px; height: 8px; border-radius: 50%; background: currentColor; }

    /* HUD ล่าง */
    .hud-bottom {
      position: fixed; bottom: 12px; left: 12px; right: 12px;
      display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center;
      gap: 10px; pointer-events: none; z-index: 10;
    }
    .btn-group { display: flex; flex-wrap: wrap; gap: 6px; pointer-events: auto; }
    .cam-btn {
      background: rgba(15, 23, 42, 0.85);
      color: #e2e8f0;
      border: 1px solid rgba(148, 163, 184, 0.3);
      border-radius: 10px;
      padding: 7px 12px;
      font-size: 0.76rem;
      font-weight: 700;
      cursor: pointer;
      backdrop-filter: blur(8px);
      transition: all 0.15s ease;
    }
    .cam-btn:hover { background: #2563eb; border-color: #60a5fa; color: #fff; }
    .hint {
      pointer-events: auto;
      background: rgba(15, 23, 42, 0.78);
      border: 1px solid rgba(148, 163, 184, 0.2);
      border-radius: 10px;
      padding: 6px 12px;
      font-size: 0.73rem;
      color: #cbd5e1;
      backdrop-filter: blur(8px);
    }
    @media (max-width: 640px) {
      .title-row h1 { font-size: 0.92rem; }
      .stat-pill { min-width: 56px; padding: 3px 7px; }
      .stat-pill .val { font-size: 1rem; }
      .hint { display: none; }
    }
  </style>
</head>
<body>
  <div id="canvas-wrap"></div>

  <div class="hud-top">
    <div class="panel">
      <div class="title-row">
        <h1>ลานจอดรถห้าง ชั้น 1 <span class="badge-3d">3D LIVE</span></h1>
        <span id="connPill" class="conn-pill"><span class="dot"></span><span id="connTxt">เชื่อมต่อ...</span></span>
      </div>
      <div class="sub">20 ช่องจอด (4 แถว × 5 คัน) • แตะช่อง A2–D5 เพื่อจำลองรถเข้า/ออก</div>
    </div>

    <div class="panel stats-row">
      <div class="stat-pill"><span class="lbl">ว่าง (เขียว)</span><span id="freeCnt" class="val c-free">-</span></div>
      <div class="stat-pill"><span class="lbl">มีรถ (แดง)</span><span id="busyCnt" class="val c-busy">-</span></div>
      <div class="stat-pill"><span class="lbl">เซ็นเซอร์ A1</span><span id="distVal" class="val c-dist">- cm</span></div>
    </div>
  </div>

  <div class="hud-bottom">
    <div class="btn-group">
      <button class="cam-btn" onclick="setCameraView('iso')">🎥 มุมมอง 3D</button>
      <button class="cam-btn" onclick="setCameraView('top')">🗺️ มุมมองด้านบน</button>
      <button class="cam-btn" onclick="setCameraView('a1')">📡 โฟกัสช่อง A1</button>
    </div>
    <div class="hint">
      ลากเพื่อหมุน 3D • ซูมเข้า-ออก • ช่อง <b>A1</b> อ่านจากเซ็นเซอร์ Ultrasonic จริง (≤ 10 ซม.)
    </div>
  </div>

  <script src="/three.js"></script>
  <script>
    const names = [
      "A1","A2","A3","A4","A5",
      "B1","B2","B3","B4","B5",
      "C1","C2","C3","C4","C5",
      "D1","D2","D3","D4","D5"
    ];
    const carPalette = [
      0xef4444, 0x38bdf8, 0xf1f5f9, 0xf59e0b, 0x64748b,
      0xe2e8f0, 0x3b82f6, 0xf43f5e, 0xa855f7, 0x10b981,
      0xf97316, 0xf8fafc, 0x6366f1, 0x14b8a6, 0xeab308,
      0xcbd5e1, 0x0ea5e9, 0xec4899, 0x94a3b8, 0x2563eb
    ];

    // ================= ตั้งค่าฉาก Three.js =================
    const wrap = document.getElementById("canvas-wrap");
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x090e1a);
    scene.fog = new THREE.Fog(0x090e1a, 35, 85);

    const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 150);
    camera.position.set(17, 21, 23);

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    wrap.appendChild(renderer.domElement);

    const controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.maxPolarAngle = Math.PI / 2.12;
    controls.minDistance = 8;
    controls.maxDistance = 52;
    controls.target.set(0, 0.5, 1.5);

    // ================= แสงสว่าง =================
    scene.add(new THREE.AmbientLight(0xdbeafe, 0.68));
    const dirLight = new THREE.DirectionalLight(0xffffff, 0.9);
    dirLight.position.set(14, 28, 16);
    dirLight.castShadow = true;
    dirLight.shadow.mapSize.width = 1024;
    dirLight.shadow.mapSize.height = 1024;
    dirLight.shadow.camera.near = 5;
    dirLight.shadow.camera.far = 70;
    dirLight.shadow.camera.left = -22;
    dirLight.shadow.camera.right = 22;
    dirLight.shadow.camera.top = 22;
    dirLight.shadow.camera.bottom = -22;
    scene.add(dirLight);

    // แสงเติมอีกด้านเพื่อให้สีรถและป้ายชัด
    const fillLight = new THREE.DirectionalLight(0x93c5fd, 0.35);
    fillLight.position.set(-16, 14, -14);
    scene.add(fillLight);

    // ================= พื้นลานจอดห้าง =================
    const floorGeo = new THREE.BoxGeometry(22, 0.4, 31.6);
    const floorMat = new THREE.MeshStandardMaterial({ color: 0x1e293b, roughness: 0.85, metalness: 0.1 });
    const floorMesh = new THREE.Mesh(floorGeo, floorMat);
    floorMesh.position.set(0, -0.2, 0.6);
    floorMesh.receiveShadow = true;
    scene.add(floorMesh);

    // ขอบอาคารสีเข้มรอบชั้น
    const borderMat = new THREE.MeshStandardMaterial({ color: 0x334155, roughness: 0.7 });
    const curbTop = new THREE.Mesh(new THREE.BoxGeometry(22.4, 0.35, 0.5), borderMat);
    curbTop.position.set(0, 0.17, -15.2);
    scene.add(curbTop);
    const curbBot = curbTop.clone();
    curbBot.position.set(0, 0.17, 16.4);
    scene.add(curbBot);

    // เลนถนน & เส้นประสีเหลือง
    function addLane(zCenter) {
      const road = new THREE.Mesh(
        new THREE.PlaneGeometry(21, 3.6),
        new THREE.MeshStandardMaterial({ color: 0x263143, roughness: 0.9 })
      );
      road.rotation.x = -Math.PI / 2;
      road.position.set(0, 0.015, zCenter);
      road.receiveShadow = true;
      scene.add(road);

      const dashMat = new THREE.MeshBasicMaterial({ color: 0xfacc15 });
      for (let x = -8.5; x <= 8.5; x += 2.2) {
        const dash = new THREE.Mesh(new THREE.PlaneGeometry(1.1, 0.12), dashMat);
        dash.rotation.x = -Math.PI / 2;
        dash.position.set(x, 0.02, zCenter);
        scene.add(dash);
      }
    }
    addLane(-12.9); // ทางเข้า-ออกบน
    addLane(-4.6);  // เลน 1 (ระหว่างแถว A และ B)
    addLane(8.2);   // เลน 2 (ระหว่างแถว C และ D)

    // เกาะกลางระหว่างแถว B กับ C
    const divider = new THREE.Mesh(
      new THREE.BoxGeometry(19, 0.16, 0.4),
      new THREE.MeshStandardMaterial({ color: 0x475569, roughness: 0.7 })
    );
    divider.position.set(0, 0.08, 1.8);
    divider.receiveShadow = true;
    scene.add(divider);

    // เสาอาคารห้าง (แถบเหลือง-ดำ)
    function addPillar(x, z) {
      const grp = new THREE.Group();
      const col = new THREE.Mesh(
        new THREE.BoxGeometry(0.65, 3.6, 0.65),
        new THREE.MeshStandardMaterial({ color: 0x475569, roughness: 0.6 })
      );
      col.position.y = 1.8;
      col.castShadow = true;
      grp.add(col);
      const stripe = new THREE.Mesh(
        new THREE.BoxGeometry(0.68, 0.55, 0.68),
        new THREE.MeshStandardMaterial({ color: 0xfacc15, roughness: 0.5 })
      );
      stripe.position.y = 0.55;
      grp.add(stripe);
      grp.position.set(x, 0, z);
      scene.add(grp);
    }
    [-9.6, 0, 9.6].forEach(px => {
      addPillar(px, 1.8);
      addPillar(px, -10.8);
      addPillar(px, 14.5);
    });

    // ป้ายพื้นถนน (ทางเข้า / ทางออก / ข้อความช่องจอด)
    function makeTextPlane(text, bg, fg, w, h, fontSize) {
      const cvs = document.createElement("canvas");
      cvs.width = 256; cvs.height = 128;
      const ctx = cvs.getContext("2d");
      if (bg) {
        ctx.fillStyle = bg;
        ctx.fillRect(0, 0, 256, 128);
      }
      ctx.fillStyle = fg;
      ctx.font = "bold " + fontSize + "px sans-serif";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(text, 128, 64);
      const tex = new THREE.CanvasTexture(cvs);
      const mat = new THREE.MeshBasicMaterial({ map: tex, transparent: !bg });
      const mesh = new THREE.Mesh(new THREE.PlaneGeometry(w, h), mat);
      mesh.rotation.x = -Math.PI / 2;
      return mesh;
    }

    const inSign = makeTextPlane("ENTRANCE ➜", "#166534", "#ffffff", 3.2, 1.1, 34);
    inSign.position.set(-7.2, 0.025, -12.9);
    scene.add(inSign);

    const outSign = makeTextPlane("EXIT ➜", "#991b1b", "#ffffff", 2.6, 1.1, 38);
    outSign.position.set(7.4, 0.025, -12.9);
    scene.add(outSign);

    // ================= ฟังก์ชันสร้างโมเดลรถ 3 มิติ =================
    function createCarMesh(colorHex) {
      const car = new THREE.Group();

      // ตัวถังล่าง
      const bodyMat = new THREE.MeshStandardMaterial({ color: colorHex, roughness: 0.35, metalness: 0.45 });
      const body = new THREE.Mesh(new THREE.BoxGeometry(1.75, 0.56, 3.55), bodyMat);
      body.position.y = 0.44;
      body.castShadow = true;
      body.receiveShadow = true;
      car.add(body);

      // ห้องโดยสาร + กระจกรถ
      const cabin = new THREE.Mesh(
        new THREE.BoxGeometry(1.42, 0.48, 1.95),
        new THREE.MeshStandardMaterial({ color: 0x0f172a, roughness: 0.2, metalness: 0.6 })
      );
      cabin.position.set(0, 0.94, -0.15);
      cabin.castShadow = true;
      car.add(cabin);

      // หลังคาสีเดียวกับตัวรถ
      const roof = new THREE.Mesh(new THREE.BoxGeometry(1.36, 0.08, 1.55), bodyMat);
      roof.position.set(0, 1.19, -0.15);
      car.add(roof);

      // ล้อทั้ง 4
      const wheelGeo = new THREE.CylinderGeometry(0.3, 0.3, 0.22, 16);
      const wheelMat = new THREE.MeshStandardMaterial({ color: 0x111827, roughness: 0.8 });
      const rimMat   = new THREE.MeshStandardMaterial({ color: 0xcbd5e1, metalness: 0.8, roughness: 0.2 });
      const wheelPos = [
        [-0.88, 0.3,  1.05],
        [ 0.88, 0.3,  1.05],
        [-0.88, 0.3, -1.05],
        [ 0.88, 0.3, -1.05]
      ];
      wheelPos.forEach(p => {
        const w = new THREE.Mesh(wheelGeo, wheelMat);
        w.rotation.z = Math.PI / 2;
        w.position.set(p[0], p[1], p[2]);
        w.castShadow = true;
        car.add(w);

        const r = new THREE.Mesh(new THREE.CylinderGeometry(0.16, 0.16, 0.24, 12), rimMat);
        r.rotation.z = Math.PI / 2;
        r.position.set(p[0], p[1], p[2]);
        car.add(r);
      });

      // ไฟหน้า & ไฟท้าย
      const headMat = new THREE.MeshBasicMaterial({ color: 0xfef08a });
      const tailMat = new THREE.MeshBasicMaterial({ color: 0xef4444 });
      [-0.55, 0.55].forEach(x => {
        const hl = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.14, 0.06), headMat);
        hl.position.set(x, 0.46, 1.78);
        car.add(hl);

        const tl = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.14, 0.06), tailMat);
        tl.position.set(x, 0.46, -1.78);
        car.add(tl);
      });

      return car;
    }

    // ================= สร้างช่องจอดทั้ง 20 ช่อง (4 แถว × 5 คัน) =================
    const rowConfigs = [
      { z: -8.6, facing: 0 },        // แถว A หันหน้าเข้าเลน 1 (+Z)
      { z: -0.6, facing: Math.PI },  // แถว B หันหน้าเข้าเลน 1 (-Z)
      { z:  4.2, facing: 0 },        // แถว C หันหน้าเข้าเลน 2 (+Z)
      { z: 12.2, facing: Math.PI }   // แถว D หันหน้าเข้าเลน 2 (-Z)
    ];

    const slotObjs = [];
    const clickableMeshes = [];

    // โครงรางไฟเพดานเหนือแต่ละแถว
    const railMat = new THREE.MeshStandardMaterial({ color: 0x64748b, metalness: 0.7, roughness: 0.3 });
    rowConfigs.forEach(rc => {
      const rail = new THREE.Mesh(new THREE.BoxGeometry(16.8, 0.1, 0.12), railMat);
      rail.position.set(0, 3.25, rc.z);
      scene.add(rail);
    });

    for (let r = 0; r < 4; r++) {
      for (let c = 0; c < 5; c++) {
        const idx = r * 5 + c;
        const x = (c - 2) * 3.4;
        const z = rowConfigs[r].z;
        const facing = rowConfigs[r].facing;

        const bayGrp = new THREE.Group();
        bayGrp.position.set(x, 0, z);
        bayGrp.rotation.y = facing;

        // พื้นช่องจอด (กดคลิกได้)
        const padMat = new THREE.MeshStandardMaterial({
          color: 0x16a34a, transparent: true, opacity: 0.22, roughness: 0.8
        });
        const pad = new THREE.Mesh(new THREE.PlaneGeometry(2.65, 4.1), padMat);
        pad.rotation.x = -Math.PI / 2;
        pad.position.y = 0.025;
        pad.userData = { slotIndex: idx };
        bayGrp.add(pad);
        clickableMeshes.push(pad);

        // เส้นตีกรอบสีขาว (หรือสีฟ้าสำหรับ A1 Live Sensor)
        const lineColor = (idx === 0) ? 0x38bdf8 : 0xe2e8f0;
        const lineMat = new THREE.MeshBasicMaterial({ color: lineColor });
        const lineL = new THREE.Mesh(new THREE.PlaneGeometry(0.1, 4.2), lineMat);
        lineL.rotation.x = -Math.PI / 2; lineL.position.set(-1.35, 0.03, 0);
        bayGrp.add(lineL);
        const lineR = lineL.clone();
        lineR.position.set(1.35, 0.03, 0);
        bayGrp.add(lineR);
        const lineBack = new THREE.Mesh(new THREE.PlaneGeometry(2.8, 0.1), lineMat);
        lineBack.rotation.x = -Math.PI / 2; lineBack.position.set(0, 0.03, -2.05);
        bayGrp.add(lineBack);

        // แท่นหยุดล้อ
        const stopper = new THREE.Mesh(
          new THREE.BoxGeometry(1.9, 0.12, 0.18),
          new THREE.MeshStandardMaterial({ color: idx === 0 ? 0x0284c7 : 0xfacc15 })
        );
        stopper.position.set(0, 0.06, -1.65);
        bayGrp.add(stopper);

        // ป้ายชื่อช่องบนพื้น (A1..D5)
        const label = makeTextPlane(
          idx === 0 ? "A1 LIVE" : names[idx],
          null,
          idx === 0 ? "#38bdf8" : "#cbd5e1",
          2.0, 0.9, idx === 0 ? 44 : 52
        );
        label.position.set(0, 0.035, 1.55);
        bayGrp.add(label);

        // ดวงไฟเพดานเหนือช่องจอด (เขียว/แดง เรืองแสง)
        const lampMat = new THREE.MeshStandardMaterial({
          color: 0x22c55e,
          emissive: 0x22c55e,
          emissiveIntensity: 1.6,
          roughness: 0.2
        });
        const lamp = new THREE.Mesh(new THREE.SphereGeometry(0.24, 16, 12), lampMat);
        lamp.position.set(0, 3.05, 0);
        bayGrp.add(lamp);

        // ขาแขวนดวงไฟ
        const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.04, 0.04, 0.22, 8), railMat);
        stem.position.set(0, 3.16, 0);
        bayGrp.add(stem);

        // โมเดลรถ 3 มิติ
        const car = createCarMesh(carPalette[idx]);
        car.visible = false;
        car.position.set(0, 0, 2.2);
        car.traverse(ch => { if (ch.isMesh) ch.userData = { slotIndex: idx }; });
        bayGrp.add(car);
        clickableMeshes.push(car);

        scene.add(bayGrp);
        slotObjs.push({
          occupied: false,
          animZ: 2.2,
          padMat: padMat,
          lampMat: lampMat,
          car: car
        });
      }
    }

    // ================= อัปเดตสถานะจากบอร์ด ESP8266 =================
    function applyState(data) {
      let busy = 0;
      data.slots.forEach((occ, idx) => {
        const s = slotObjs[idx];
        const isOcc = !!occ;
        if (isOcc) busy++;
        s.occupied = isOcc;

        if (isOcc) {
          s.padMat.color.setHex(0xef4444);
          s.padMat.opacity = 0.28;
          s.lampMat.color.setHex(0xef4444);
          s.lampMat.emissive.setHex(0xef4444);
          s.car.visible = true;
        } else {
          s.padMat.color.setHex(0x22c55e);
          s.padMat.opacity = 0.20;
          s.lampMat.color.setHex(0x22c55e);
          s.lampMat.emissive.setHex(0x22c55e);
        }
      });

      document.getElementById("busyCnt").textContent = busy;
      document.getElementById("freeCnt").textContent = 20 - busy;
      document.getElementById("distVal").textContent =
        (data.cm > 0 ? data.cm + " cm" : "ไม่มีวัตถุ");
    }

    async function fetchStatus() {
      const pill = document.getElementById("connPill");
      const txt  = document.getElementById("connTxt");
      try {
        const res = await fetch("/api/status", { cache: "no-store" });
        const data = await res.json();
        applyState(data);
        pill.classList.remove("offline");
        txt.textContent = "ออนไลน์";
      } catch (e) {
        pill.classList.add("offline");
        txt.textContent = "หลุดการเชื่อมต่อ";
      }
    }

    async function toggleSlot(idx) {
      if (idx === 0) return; // A1 ใช้เซ็นเซอร์จริง
      try {
        const res = await fetch("/api/toggle?id=" + idx, { cache: "no-store" });
        const data = await res.json();
        applyState(data);
      } catch (e) {}
    }

    // ================= คลิกช่องจอดในฉาก 3D =================
    const raycaster = new THREE.Raycaster();
    const pointer = new THREE.Vector2();
    let downPos = { x: 0, y: 0 };

    renderer.domElement.addEventListener("pointerdown", e => {
      downPos = { x: e.clientX, y: e.clientY };
    });
    renderer.domElement.addEventListener("pointerup", e => {
      const dx = e.clientX - downPos.x;
      const dy = e.clientY - downPos.y;
      if (Math.hypot(dx, dy) > 6) return; // ลากกล้องอยู่ ไม่นับเป็นคลิก

      const rect = renderer.domElement.getBoundingClientRect();
      pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
      pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

      raycaster.setFromCamera(pointer, camera);
      const hits = raycaster.intersectObjects(clickableMeshes, true);
      for (const h of hits) {
        if (h.object.userData && h.object.userData.slotIndex !== undefined) {
          toggleSlot(h.object.userData.slotIndex);
          break;
        }
      }
    });

    // ================= ปุ่มเปลี่ยนมุมกล้อง =================
    let camAnim = null;
    function setCameraView(mode) {
      if (mode === "iso") {
        camAnim = { pos: new THREE.Vector3(17, 21, 23), target: new THREE.Vector3(0, 0.5, 1.5) };
      } else if (mode === "top") {
        camAnim = { pos: new THREE.Vector3(0, 35, 2.2), target: new THREE.Vector3(0, 0, 2.0) };
      } else if (mode === "a1") {
        camAnim = { pos: new THREE.Vector3(-10.5, 5.5, -2.5), target: new THREE.Vector3(-6.8, 0.6, -8.6) };
      }
    }

    window.addEventListener("resize", () => {
      camera.aspect = window.innerWidth / window.innerHeight;
      camera.updateProjectionMatrix();
      renderer.setSize(window.innerWidth, window.innerHeight);
    });

    // ================= Animation Loop =================
    function animate() {
      requestAnimationFrame(animate);

      // อนิเมชันรถขับเข้า/ออกช่องจอดอย่างนุ่มนวล
      slotObjs.forEach(s => {
        const targetZ = s.occupied ? 0.1 : 2.8;
        s.animZ += (targetZ - s.animZ) * 0.14;
        s.car.position.z = s.animZ;
        if (!s.occupied && s.animZ > 2.65) {
          s.car.visible = false;
        }
      });

      if (camAnim) {
        camera.position.lerp(camAnim.pos, 0.1);
        controls.target.lerp(camAnim.target, 0.1);
        if (camera.position.distanceTo(camAnim.pos) < 0.15) camAnim = null;
      }

      controls.update();
      renderer.render(scene, camera);
    }

    fetchStatus();
    setInterval(fetchStatus, 400);
    animate();
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(pingPin, OUTPUT);
  pinMode(inPin, INPUT);
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);

  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, HIGH);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress ip = WiFi.softAPIP();
  Serial.println("\n=== Smart Parking 3D ESP8266 Started ===");
  Serial.print("Wi-Fi SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password  : ");
  Serial.println(AP_PASS);
  Serial.print("Open URL  : http://");
  Serial.println(ip);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/three.js", HTTP_GET, handleThreeJs);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/toggle", HTTP_GET, handleToggle);
  server.begin();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastMeasureMs >= 200) {
    lastMeasureMs = now;
    readParkingSensor();
  }
}

void readParkingSensor() {
  digitalWrite(pingPin, LOW);
  delayMicroseconds(2);
  digitalWrite(pingPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(pingPin, LOW);

  long duration = pulseIn(inPin, HIGH, 30000);
  currentCm = microsecondsToCentimeters(duration);

  bool a1Occupied = (currentCm > 0 && currentCm <= DIST_THRESHOLD_CM);
  slots[0] = a1Occupied;

  if (a1Occupied) {
    digitalWrite(redLedPin, HIGH);
    digitalWrite(greenLedPin, LOW);
  } else {
    digitalWrite(redLedPin, LOW);
    digitalWrite(greenLedPin, HIGH);
  }
}

String buildStatusJson() {
  String json = "{\"cm\":";
  json += String(currentCm);
  json += ",\"slots\":[";
  for (int i = 0; i < 20; i++) {
    json += (slots[i] ? "1" : "0");
    if (i < 19) json += ",";
  }
  json += "]}";
  return json;
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleThreeJs() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "application/javascript", (const char*)THREE_BUNDLE_GZ, THREE_BUNDLE_GZ_LEN);
}

void handleStatus() {
  server.send(200, "application/json", buildStatusJson());
}

void handleToggle() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 1 && id < 20) {
      slots[id] = !slots[id];
    }
  }
  server.send(200, "application/json", buildStatusJson());
}

long microsecondsToCentimeters(long microseconds) {
  return microseconds / 29 / 2;
}
