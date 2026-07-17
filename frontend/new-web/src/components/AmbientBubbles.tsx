import { useCallback, useEffect, useRef, useState } from 'react';

interface BubbleDef {
  id: number;
  baseX: number;
  baseY: number;
  baseSize: number;
  gradientCSS: string;
  mass: number;          // 惯性质量（越大越难推动）
}

interface BubblePhysics {
  x: number;   // current position (fraction of viewport)
  y: number;
  vx: number;  // velocity in %/s
  vy: number;
  size: number;
  opacity: number;
}

// ── 渐变风格库 ──
const GRADIENT_STYLES = [
  (h: number, s: number, l: number) => `radial-gradient(circle at 30% 30%, hsl(${h},${s}%,${l}%) 0%, hsl(${(h+30)%360},${s-10}%,${l-15}%) 100%)`,
  (h: number, s: number, l: number) => `radial-gradient(circle at 70% 40%, hsl(${h},${s}%,${l}%) 0%, hsl(${(h-20+360)%360},${s}%,${l-10}%) 70%, hsl(${(h+50)%360},${s-5}%,${l-20}%) 100%)`,
  (h: number, s: number, l: number) => `linear-gradient(135deg, hsl(${h},${s}%,${l}%) 0%, hsl(${(h+60)%360},${s-15}%,${l-5}%) 100%)`,
  (h: number, s: number, l: number) => `linear-gradient(200deg, hsl(${h},${s}%,${l}%) 0%, hsl(${(h-40+360)%360},${s}%,${l-15}%) 50%, hsl(${(h+20)%360},${s-20}%,${l-5}%) 100%)`,
  (h: number, s: number, l: number) => `conic-gradient(from ${h}deg, hsl(${h},${s}%,${l}%) 0%, hsl(${(h+120)%360},${s-5}%,${l-10}%) 50%, hsl(${h},${s}%,${l}%) 100%)`,
  (h: number, s: number, l: number) => `radial-gradient(ellipse at 40% 60%, hsl(${h},${s}%,${l+5}%) 0%, hsl(${(h+45)%360},${s-10}%,${l-20}%) 60%, transparent 100%)`,
  (h: number, s: number, l: number) => `radial-gradient(circle at 60% 30%, hsl(${h},${s+5}%,${l+5}%) 0%, hsl(${(h-30+360)%360},${s}%,${l-10}%) 50%, hsl(${(h+70)%360},${s-15}%,${l-20}%) 100%)`,
  (h: number, s: number, l: number) => `linear-gradient(45deg, hsl(${h},${s}%,${l}%) 0%, transparent 40%), linear-gradient(225deg, hsl(${(h+90)%360},${s-10}%,${l-5}%) 0%, transparent 40%), hsl(${(h+180)%360},${s-5}%,${l-10}%)`,
];

const HUE_SPACING = 360 / 12;

function seedBubbles(count: number): BubbleDef[] {
  const bubbles: BubbleDef[] = [];
  for (let i = 0; i < count; i++) {
    const hue = (HUE_SPACING * i + 15) % 360;
    const styleIdx = i % GRADIENT_STYLES.length;
    const sat = 55 + Math.random() * 25;
    const light = 45 + Math.random() * 20;
    bubbles.push({
      id: i,
      baseX: 0.1 + Math.random() * 0.8,
      baseY: 0.08 + Math.random() * 0.84,
      baseSize: 160 + Math.random() * 280,
      gradientCSS: GRADIENT_STYLES[styleIdx](hue, sat, light),
      mass: 0.8 + Math.random() * 1.6,  // heavier bubbles are harder to push
    });
  }
  return bubbles;
}

// ── 物理参数 ──
const SPRING_K = 1.2;        // 回弹弹簧系数
const DAMPING = 2.5;         // 摩擦力/阻尼
const MOUSE_FORCE_FACTOR = 0.35; // 鼠标力缩放
const MOUSE_FORCE_RADIUS = 0.3;  // 鼠标力影响半径（viewport分数）
const BURST_FORCE = 2.5;     // burst 初始力系数

const BREATHE_MIN = 0.75;
const BREATHE_MAX = 1.3;
const BREATHE_PERIOD = 8000;

interface MouseState {
  x: number;
  y: number;
  vx: number;  // mouse velocity in %/s
  vy: number;
  lastX: number;
  lastY: number;
  lastTime: number;
}

export function AmbientBubbles() {
  const [bubbleDefs] = useState(() => seedBubbles(12));

  // Physics state refs (never copies to React, read directly in rAF)
  const physicsRef = useRef<BubblePhysics[]>(
    bubbleDefs.map(d => ({
      x: d.baseX, y: d.baseY, vx: 0, vy: 0,
      size: 1, opacity: 0.6,
    }))
  );

  // Mouse state
  const mouseRef = useRef<MouseState>({
    x: 0.5, y: 0.5, vx: 0, vy: 0,
    lastX: 0.5, lastY: 0.5, lastTime: 0,
  });

  const lastMoveTime = useRef(0);
  const burstActiveRef = useRef(false);
  const burstTimerRef = useRef(0);
  const rafRef = useRef(0);

  // Snapshot for rendering (updated via rAF, throttled)
  const [renderSnap, setRenderSnap] = useState(() =>
    physicsRef.current.map((p, i) => ({ ...p, ...bubbleDefs[i] }))
  );
  const rafSnapRef = useRef(renderSnap);

  // Mouse move handler — capture velocity
  const handleMouseMove = useCallback((e: MouseEvent) => {
    const ms = mouseRef.current;
    const now = performance.now();
    const dt = now - ms.lastTime;

    if (dt > 0) {
      ms.vx = (e.clientX / window.innerWidth - ms.lastX) / (dt / 1000);
      ms.vy = (e.clientY / window.innerHeight - ms.lastY) / (dt / 1000);
    }
    ms.x = e.clientX / window.innerWidth;
    ms.y = e.clientY / window.innerHeight;
    ms.lastX = ms.x;
    ms.lastY = ms.y;
    ms.lastTime = now;
    lastMoveTime.current = now;
  }, []);

  const handleBurst = useCallback(() => {
    burstActiveRef.current = true;
    burstTimerRef.current = 0;
    const phys = physicsRef.current;
    for (let i = 0; i < phys.length; i++) {
      const angle = Math.random() * Math.PI * 2;
      phys[i].vx += Math.cos(angle) * BURST_FORCE;
      phys[i].vy += Math.sin(angle) * BURST_FORCE;
    }
  }, []);

  useEffect(() => {
    window.addEventListener('mousemove', handleMouseMove, { passive: true });
    window.addEventListener('ambient-burst', handleBurst);
    return () => {
      window.removeEventListener('mousemove', handleMouseMove);
      window.removeEventListener('ambient-burst', handleBurst);
    };
  }, [handleMouseMove, handleBurst]);

  // ── Physics loop (rAF) ──
  useEffect(() => {
    const defs = bubbleDefs;
    const phys = physicsRef.current;
    let snapVersion = 0;
    let frameIdx = 0;

    const tick = (now: number) => {
      frameIdx++;
      const dtRaw = 16.67 / 1000; // assume ~60fps, use fixed timestep for stability
      const idle = now - lastMoveTime.current > 2500;
      const burstCount = burstActiveRef.current ? burstTimerRef.current : 100;
      const ms = mouseRef.current;

      let anyChanged = false;

      for (let i = 0; i < phys.length; i++) {
        const d = defs[i];
        const p = phys[i];
        const prevX = p.x, prevY = p.y, prevSz = p.size, prevOp = p.opacity;

        // ── 力计算 ──

        // 1. 弹簧力：回到 base 位置
        const springFx = (d.baseX - p.x) * SPRING_K;
        const springFy = (d.baseY - p.y) * SPRING_K;

        // 2. 阻尼力：与速度相反
        const dampFx = -p.vx * DAMPING;
        const dampFy = -p.vy * DAMPING;

        // 3. 鼠标力：仅当鼠标活跃时
        let mouseFx = 0, mouseFy = 0;
        if (!idle && burstCount > 80) {
          const dx = p.x - ms.x;
          const dy = p.y - ms.y;
          const dist = Math.sqrt(dx * dx + dy * dy);
          if (dist < MOUSE_FORCE_RADIUS && dist > 0.001) {
            // 距离越近力越大（inverse falloff with smooth clamp）
            const distFactor = 1 - dist / MOUSE_FORCE_RADIUS; // 1 at center, 0 at edge
            const distSmooth = distFactor * distFactor; // quadratic falloff for softer transition
            const mouseSpeed = Math.sqrt(ms.vx * ms.vx + ms.vy * ms.vy);
            const forceMag = distSmooth * mouseSpeed * MOUSE_FORCE_FACTOR * d.mass;

            // Force direction: away from mouse (bubbles are pushed by mouse "wind")
            if (dist > 0.0001) {
              mouseFx = (dx / dist) * forceMag;
              mouseFy = (dy / dist) * forceMag;
            }
          }
        }

        // 4. Burst remnants
        let burstFx = 0, burstFy = 0;
        if (burstCount < 40) {
          burstFx = p.vx * (1 - burstCount / 40) * 0.8;
          burstFy = p.vy * (1 - burstCount / 40) * 0.8;
        }

        // ── 加速度 = 合力 / 质量 ──
        const ax = (springFx + dampFx + mouseFx + burstFx) / d.mass;
        const ay = (springFy + dampFy + mouseFy + burstFy) / d.mass;

        // ── 速度积分 (半隐式欧拉，更稳定) ──
        p.vx += ax * dtRaw;
        p.vy += ay * dtRaw;
        p.x += p.vx * dtRaw;
        p.y += p.vy * dtRaw;

        // ── 边界 clamp ──
        p.x = Math.max(-0.2, Math.min(1.2, p.x));
        p.y = Math.max(-0.2, Math.min(1.2, p.y));

        // ── 呼吸缩放 ──
        const breathePhase = (now % BREATHE_PERIOD) / BREATHE_PERIOD * Math.PI * 2 + d.baseX;
        const breathe = (Math.sin(breathePhase) + 1) / 2;
        p.size = BREATHE_MIN + breathe * (BREATHE_MAX - BREATHE_MIN);
        p.opacity = 0.5 + Math.sin(breathePhase * 0.5) * 0.06;

        if (Math.abs(p.x - prevX) > 0.00002 || Math.abs(p.y - prevY) > 0.00002 ||
            Math.abs(p.size - prevSz) > 0.001 || Math.abs(p.opacity - prevOp) > 0.001) {
          anyChanged = true;
        }
      }

      burstTimerRef.current = burstCount + frameIdx % 2; // increment every other frame
      if (burstCount >= 100) burstActiveRef.current = false;

      // ── 复制快照到渲染（每 2 帧） ──
      if (anyChanged && frameIdx % 2 === 0) {
        snapVersion++;
        const snap = phys.map((p, i) => ({ ...p, ...defs[i] }));
        rafSnapRef.current = snap;
        setRenderSnap(snap);
      }

      rafRef.current = requestAnimationFrame(tick);
    };

    rafRef.current = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(rafRef.current);
  }, [bubbleDefs]);

  return (
    <div style={{ position: 'absolute', inset: 0, zIndex: -1, pointerEvents: 'none', overflow: 'hidden' }}>
      {renderSnap.map(b => (
        <div
          key={b.id}
          style={{
            position: 'absolute',
            left: `${b.x * 100}%`,
            top: `${b.y * 100}%`,
            width: b.baseSize * b.size,
            height: b.baseSize * b.size,
            background: b.gradientCSS,
            opacity: b.opacity,
            borderRadius: '50%',
            transform: 'translate(-50%, -50%)',
            filter: 'blur(8px)',
            willChange: 'left, top',
          }}
        />
      ))}

      {/* ── 毛玻璃：降低不透明度 ── */}
      <div
        style={{
          position: 'absolute', inset: 0, zIndex: 0,
          backdropFilter: 'blur(100px) saturate(1.5) brightness(1.1)',
          WebkitBackdropFilter: 'blur(100px) saturate(1.5) brightness(1.1)',
          background: 'color-mix(in srgb, var(--bg) 15%, transparent)',
        }}
      />
    </div>
  );
}

export function triggerAmbientBurst() {
  window.dispatchEvent(new CustomEvent('ambient-burst'));
}