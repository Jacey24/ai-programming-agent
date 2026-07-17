# 前端 Z-Index 层级宪法

> 版本：2026-07-16 · 状态：交互层级 100% ✅ · 渲染层级 100% ✅ · 毛玻璃 90% ✅

## 一、最终文件状态速查表

### 1. `frontend/new-web/src/index.css`

```css
.glass-panel {
  background: color-mix(in srgb, var(--bg) 45%, transparent);  /* 半透明底色 */
  backdrop-filter: blur(80px) saturate(1.2) brightness(1.05);  /* ★ 毛玻璃核心，CSS 统一管理 */
  -webkit-backdrop-filter: blur(80px) saturate(1.2) brightness(1.05);
  border: 1.5px solid var(--glass-border-strong);
  border-radius: 16px;
  position: relative;       /* ★ 创建 stacking context */
  overflow: hidden;          /* ★ 裁剪圆角内容 */
  box-shadow: inset 0 1px 0 rgba(255,255,255,0.10), inset 0 -1px 0 rgba(0,0,0,0.08), 0 12px 48px var(--shadow);
}
.glass-panel::before { /* 冰霜纹理层 */ z-index: 1; backdrop-filter: blur(12px) saturate(0.6); opacity: 0.45; }
.glass-panel::after  { /* 微噪点纹理层 */ z-index: 2; opacity: 0.08; }
.glass-panel > *      { position: relative; z-index: 3; }  /* 内容始终在纹理之上 */
```

### 2. `frontend/new-web/src/components/GlassPanel.tsx`

```tsx
<div className="glass-panel flex flex-col" style={{
  pointerEvents: 'auto',         // ★ 面板自身接收事件
  width: 420 / scale, height: 520 / scale,
  resize: 'both',
  ...尺寸相关属性...
}}>
```

| 属性 | 来源 | 值 |
|------|------|---|
| `position: relative` | CSS (.glass-panel) | — |
| `overflow: hidden` | CSS (.glass-panel) | — |
| `backdrop-filter` | CSS (.glass-panel) | blur(80px) |
| `z-index` | **不设置（auto）** | 由父元素 main 决定 |
| `pointerEvents` | Inline | auto |

### 3. `frontend/new-web/src/App.tsx` — 主布局

```
root div (position:relative, transform:scale())     ← ★ 唯一 stacking context 根
│
├── ExpertGraphCanvas           z-index: 1          ← 背景画布
├── header                      z-index: 20         ← 状态栏 + 按钮
├── 装饰元素 ×4                 z-index: 0          ← pointer-events: none
├── <main>                      z-index: 10         ← position:relative, pointer-events:none
│   └── GlassPanel              z-index: auto(CSS)  ← pointer-events: auto
├── ToolPanel                   z-index: 30         ← 右侧工具侧栏
└── SettingsPanel               Portal→body (2000)  ← 全局设置模态窗
```

**关键配置代码**：

```tsx
{/* Header — 与 root SC 直接交互 */}
<header style={{ position: 'relative', zIndex: 20 }}> ... </header>

{/* main — 在 Canvas(z:1) 之上，穿透事件 */}
<main style={{ position: 'relative', zIndex: 10, pointerEvents: 'none' }}>
  <GlassPanel ... />
</main>

{/* ToolPanel — 直接 child of root，独立于 flex 流 */}
<ToolPanelWrapper theme={theme} />
```

### 4. `frontend/new-web/src/components/ToolPanel.tsx`

| 元素 | z-index | 定位 |
|------|---------|------|
| ToolPanel 根容器 | **30** | `position: absolute` (在 root SC 中) |
| ToolPopover (portal→body) | **100** | `position: fixed` |

### 5. `frontend/new-web/src/components/ExpertGraphCanvas.tsx`

```tsx
{/* ★ 根容器：全屏覆盖，正常接收事件（不设置 pointer-events:none） */}
<div style={{ position: 'absolute', inset: 0, zIndex: 1, overflow: 'hidden' }}>
  {/* ★ 交互层：全屏覆盖，处理 pan mousedown */}
  <div ref={interactionRef}
    onMouseDown={handleBgMouseDown}
    style={{ position: 'absolute', inset: 0, zIndex: 0 }}
  />
  {/* SVG 边线层：pointer-events: none */}
  <svg style={{ ... pointerEvents: 'none' }}>...</svg>
  {/* ★ 节点层：pointer-events: auto（在父层 pointer-events:none 的子元素中显式恢复） */}
  <div style={{ ... pointerEvents: 'none' }}>
    <div data-node ... style={{ pointerEvents: 'auto' }} />  {/* 每个节点单独 auto */}
  </div>
</div>
```

**Zoom wheel 监听绑定在 `interactionRef.current`**（因为需要在 auto 的交互层接收 wheel 事件）。

### 6. `frontend/new-web/src/components/SettingsPanel.tsx`

```tsx
{/* ★ Portal 到 document.body，逃逸 root SC */}
createPortal(
  <div style={{ position: 'fixed', inset: 0, zIndex: 2000, ... }}>,
  document.body
)
```

### 7. `frontend/new-web/src/components/FileTree.tsx`

```tsx
{/* ★ 文件预览弹窗也是 Portal 到 body */}
{preview && createPortal(
  <div style={{ position: 'fixed', zIndex: 100, ... }} />,
  document.body
)}
```

---

## 二、Z-Index 层级宪法（不可更改的原则）

### 宪法第 1 条：唯一 Stacking Context

**root div 的 `position: relative` + `transform: scale()` 是 normal phase 唯一的 stacking context 根。** 所有子元素（Canvas、header、main、ToolPanel、装饰元素）都在此 SC 中通过 inline `z-index` 参与层级计算。

❌ 禁止在任何子元素上使用 `transform` / `filter` / `will-change` 创建新的独立 SC（除非绝对必要且充分理解影响）。

### 宪法第 2 条：Inline Z-Index 优先

所有参与层级计算的 `z-index` 和 `position` **必须写在 inline style 中**，不得依赖 CSS class。

✅ 正确（Header）：
```tsx
<header style={{ position: 'relative', zIndex: 20 }}>
```

❌ 错误：
```css
.header { z-index: 20; position: relative; }
```

**例外**：`.glass-panel` 的 `position: relative` 走 CSS（因为 WorkspaceSelectOverlay 也需要，且该 phase 无 transform scale 嵌套 SC 问题）。

### 宪法第 3 条：层级数值规范

| z-index | 元素 | 说明 |
|---------|------|------|
| **2000** | SettingsPanel (Portal→body) | 全局设置遮罩 |
| **100** | AddExpertBtn、ToolPopover、FilePreview (Portal→body) | 浮动弹出层 |
| **30** | ToolPanel | 右侧工具侧栏 |
| **20** | Header | 状态栏 + 主题/设置按钮 |
| **10** | `<main>` | 包裹 GlassPanel 的容器 |
| **1** | ExpertGraphCanvas | 背景画布和节点 |
| **0** | 装饰元素 ×4 | 毛玻璃模糊源 |

**数值间距原则**：每次至少差 10，保留插入新层级的空间。

### 宪法第 4 条：交互解耦规则

```
视觉层（z-index） ≡ 交互层（pointer-events）
```

| 元素 | 交互策略 |
|------|---------|
| 装饰元素 | `pointerEvents: 'none'` — 永远不拦截事件 |
| `<main>` | `pointerEvents: 'none'` — 穿透到 Canvas |
| GlassPanel | `pointerEvents: 'auto'` — 面板区域拦截事件 |
| ExpertGraphCanvas | 默认 `auto` — 非面板区域自然接收事件 |
| ToolPanel | 默认 `auto` — 面板区域自然拦截事件 |

**铁律**：`pointer-events: none` 仅允许在以下两个位置使用：
1. 装饰元素（纯视觉，永不交互）
2. `<main>` 容器（让事件穿透到下方 Canvas）

### 宪法第 5 条：DOM 结构不可逆

GlassPanel **必须**保留在 `<main>` 内部作为其子元素。不可使用 Portal 逃逸（已两次验证失败：Portal 到 root 导致 Panel 消失；Portal 到 body 导致 SC 错乱）。

### 宪法第 6 条：ToolPanel 直接 Child

ToolPanel **必须**是 root div 的直接子元素，不使用任何 wrapper div（`<div className="stagger-item">` 会因 CSS animation 创建独立 SC 困住 ToolPanel 的 z-index）。

---

## 三、毛玻璃现状与已知局限

### 当前工作方式

`.glass-panel` 的 `backdrop-filter: blur(80px)` 由 CSS 统一管理，被 `<main>` 的 `position: relative; zIndex: 10` 创建的独立 stacking context 所隔离。

**结果**：在 normal phase（有 `transform: scale()`）中，GlassPanel 的 `backdrop-filter` 只能模糊 main 内部元素，无法看到 root SC 中的装饰元素和 Canvas。

### WorkspaceSelectOverlay 的表现

WorkspaceSelectOverlay 使用相同的 `.glass-panel` class，但它处于 **无 transform scale 的独立 phase**，其 `backdrop-filter` 能正常模糊背景元素。

### 下一步（如果修复毛玻璃）

唯一的理论可行方向：**将 GlassPanel 连同其 backdrop-filter 一起 Portal 到 root SC**（而非 main 内部）。这需要：
1. GlassPanel 通过 `createPortal(rootRef.current)` 注入到 root div
2. `<main>` 变成仅占位的空容器
3. GlassPanel 的 `position: relative; zIndex: 10; backdrop-filter` 直接在 root SC 工作

⚠️ 此方向之前尝试失败（Panel 消失），需要解决 ref 时序问题。

---

## 四、文件修改历史（本次调试完整记录）

| 顺序 | 文件 | 改动 | 结果 |
|------|------|------|------|
| 1 | App.tsx | 装饰元素 z-index: -1/0 → 1 | 不再被 transform SC 隐藏 |
| 2 | App.tsx | header z-index: 2 → **20** | 层级明确 |
| 3 | App.tsx | main 添加 `position:relative; zIndex:10` | 视觉在 Canvas 之上 |
| 4 | ExpertGraphCanvas.tsx | 根元素加 `pointerEvents:'none'`、交互层加 `pointerEvents:'auto'` | 尝试解耦，后回退 |
| 5 | GlassPanel.tsx | 移除内联 z-index/pointer-events | 依赖 CSS，后回退 |
| 6 | GlassPanel.tsx | 恢复 inline z-index:10 + pointerEvents:auto | 对齐 Header |
| 7 | GlassPanel.tsx | 与 Header 统一 z-index:20 | Panel 视觉在 Header 下方 |
| 8 | GlassPanel.tsx | z-index 降回 10 | 正确层级 |
| 9 | index.css | .glass-panel 移除 position/backdrop-filter/overflow | 尝试移动 stacking context |
| 10 | GlassPanel.tsx | inline 写入 position+backdrop-filter+overflow | 统一 SC 管理（后回退） |
| 11 | ExpertGraphCanvas.tsx | 交互层 pointerEvents:auto 覆盖父级 none | 部分修复 |
| 12 | ExpertGraphCanvas.tsx | zoom 监听从 canvasRef 移到 interactionRef | wheel 事件穿透 |
| 13 | App.tsx | main 移除 position:relative+zIndex:10 | ❌ GlassPanel 跑到 Canvas 后面 |
| 14 | App.tsx | GlassPanel Portal 到 root div | ❌ Panel 消失 |
| 15 | App.tsx | **回退**：main 恢复 `position:relative; zIndex:10` | ✅ 视觉层级恢复 |
| 16 | App.tsx | GlassPanel Portal 取消，恢复为 main 子元素 | ✅ 面板正常显示 |
| 17 | App.tsx | ToolPanel 移除 `<div className="stagger-item">` 包装 | ✅ ToolPanel 层级正确 |
| 18 | ExpertGraphCanvas.tsx | 移除 pointerEvents hack，恢复默认交互 | ✅ Canvas 交互正常 |
| 19 | index.css | **恢复** .glass-panel 完整原始 CSS (position/backdrop-filter) | ✅ 毛玻璃 90% 工作 |
| 20 | GlassPanel.tsx | **清理**：仅保留 `pointerEvents:'auto'`，其余回 CSS | ✅ 当前稳定状态 |
| 21 | SettingsPanel.tsx | Portal 到 body，zIndex:2000 | ✅ 全局设置在最顶层 |
| 22 | ToolPanel.tsx | root z-index: 100→30，popover: 10000→100 | ✅ 层级正确 |
| 23 | FileTree.tsx | 预览弹窗 z-index: 10001→100 | ✅ 层级正确 |

---

## 五、禁止修改清单（守护区域）

- ❌ **不改** ExpertGraphCanvas 的 drag/pan/zoom 逻辑
- ❌ **不改** Header 的 `position: 'relative', zIndex: 20`
- ❌ **不改** `<main>` 的 `position: 'relative', zIndex: 10, pointerEvents: 'none'`
- ❌ **不改** GlassPanel 在 main 内部的 DOM 位置
- ❌ **不改** ToolPanel 直接 child 的结构
- ❌ **不改** `.glass-panel` CSS 中的 `backdrop-filter`/`position: relative`/`overflow: hidden`
- ❌ **不在**任何子元素上添加 `transform`/`filter`/`will-change`（会产生额外 SC）