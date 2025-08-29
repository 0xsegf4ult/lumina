#pragma once

uint2 unflatten2D(uint idx, uint dim)
{
	return uint2(idx % dim, idx / dim);
}

uint flatten2D(uint2 coord, uint dim)
{
	return coord.x + coord.y * dim;
}

uint2 remap_lane_8x8(uint lane)
{
        return uint2 (
                bitfieldInsert(bitfieldExtract(lane, 2, 3), lane, 0, 1),
                bitfieldInsert(bitfieldExtract(lane, 3, 3), bitfieldExtract(lane, 1, 2), 0, 2)
        );
}
