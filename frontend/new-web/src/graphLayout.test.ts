import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { ADD_EXPERT_SIZE, correctControlIfOutside, defaultAddExpertPosition, findSafeNodePosition } from './graphLayout.ts';

describe('expert graph local layout', () => {
  it('places the add control in the visible right-side area', () => {
    const canvas = { width: 960, height: 640 };
    const position = defaultAddExpertPosition(canvas);
    assert.ok(position.x > canvas.width / 2);
    assert.ok(position.x + ADD_EXPERT_SIZE.width <= canvas.width);
    assert.ok(position.y >= 0 && position.y + ADD_EXPERT_SIZE.height <= canvas.height);
  });

  it('preserves a user position unless it is completely outside after resize', () => {
    const canvas = { width: 500, height: 300 };
    const partlyVisible = { x: 480, y: 280 };
    assert.deepEqual(correctControlIfOutside(partlyVisible, canvas), partlyVisible);
    const corrected = correctControlIfOutside({ x: 700, y: 500 }, canvas);
    assert.ok(corrected.x + ADD_EXPERT_SIZE.width <= canvas.width);
    assert.ok(corrected.y + ADD_EXPERT_SIZE.height <= canvas.height);
  });

  it('finds a node position that does not stack on an existing node', () => {
    const occupied = [{ x: 100, y: 100, width: 180, height: 60 }];
    const position = findSafeNodePosition(
      { x: 100, y: 100 }, occupied, { x: 24, y: 24, width: 600, height: 440 }, { width: 180, height: 60 },
    );
    assert.notDeepEqual(position, { x: 100, y: 100 });
  });
});
