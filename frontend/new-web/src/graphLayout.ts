export interface CanvasSize { width: number; height: number }
export interface LayoutPoint { x: number; y: number }
export interface LayoutRect extends LayoutPoint { width: number; height: number }

export const ADD_EXPERT_SIZE = { width: 104, height: 38 };

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.min(Math.max(value, minimum), Math.max(minimum, maximum));
}

export function defaultAddExpertPosition(canvas: CanvasSize): LayoutPoint {
  return {
    x: clamp(canvas.width - ADD_EXPERT_SIZE.width - 24, 8, canvas.width - ADD_EXPERT_SIZE.width - 8),
    y: clamp(Math.round(canvas.height * 0.22), 16, canvas.height - ADD_EXPERT_SIZE.height - 16),
  };
}

export function correctControlIfOutside(position: LayoutPoint, canvas: CanvasSize): LayoutPoint {
  const completelyOutside = position.x + ADD_EXPERT_SIZE.width < 0 || position.x > canvas.width ||
    position.y + ADD_EXPERT_SIZE.height < 0 || position.y > canvas.height;
  if (!completelyOutside) return position;
  return {
    x: clamp(position.x, 8, canvas.width - ADD_EXPERT_SIZE.width - 8),
    y: clamp(position.y, 8, canvas.height - ADD_EXPERT_SIZE.height - 8),
  };
}

function overlaps(left: LayoutRect, right: LayoutRect, gap = 20): boolean {
  return left.x < right.x + right.width + gap && left.x + left.width + gap > right.x &&
    left.y < right.y + right.height + gap && left.y + left.height + gap > right.y;
}

export function findSafeNodePosition(
  preferred: LayoutPoint,
  occupied: LayoutRect[],
  bounds: LayoutRect,
  nodeSize: { width: number; height: number },
): LayoutPoint {
  const stepX = nodeSize.width + 44;
  const stepY = nodeSize.height + 44;
  const columns = Math.max(1, Math.floor((bounds.width + 44) / stepX));
  const rows = Math.max(1, Math.floor((bounds.height + 44) / stepY));
  const startColumn = clamp(Math.round((preferred.x - bounds.x) / stepX), 0, columns - 1);
  const startRow = clamp(Math.round((preferred.y - bounds.y) / stepY), 0, rows - 1);

  for (let offset = 0; offset < columns * rows; offset += 1) {
    const index = startRow + offset;
    const row = index % rows;
    const column = (startColumn + Math.floor(index / rows)) % columns;
    const candidate = {
      x: clamp(bounds.x + column * stepX, bounds.x, bounds.x + bounds.width - nodeSize.width),
      y: clamp(bounds.y + row * stepY, bounds.y, bounds.y + bounds.height - nodeSize.height),
    };
    const rect = { ...candidate, width: nodeSize.width, height: nodeSize.height };
    if (!occupied.some(other => overlaps(rect, other))) return candidate;
  }

  return {
    x: clamp(preferred.x, bounds.x, bounds.x + bounds.width - nodeSize.width),
    y: clamp(preferred.y, bounds.y, bounds.y + bounds.height - nodeSize.height),
  };
}
