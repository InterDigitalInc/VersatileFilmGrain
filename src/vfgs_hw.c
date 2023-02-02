/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2022, InterDigital
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted (subject to the limitations in the disclaimer below) provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of InterDigital nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS
 * LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "vfgs_hw.h"
#include <string.h> // memcpy
#include <assert.h>

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define round(a,s) (((a)+(1<<((s)-1)))>>(s))

#define PATTERN_INTERPOLATION 0

// Note: declarations optimized for code readability; e.g. pattern storage in
//       actual hardware implementation would differ significantly
static int8 pattern[2][VFGS_MAX_PATTERNS+1][64][64] = {0, }; // +1 to simplify interpolation code
static uint8 sLUT[3][256] = {0, };
static uint8 pLUT[3][256] = {0, };
static uint32 rnd = 0xdeadbeef;
static uint32 rnd_up = 0xdeadbeef;
static uint32 line_rnd = 0xdeadbeef;
static uint32 line_rnd_up = 0xdeadbeef;
static uint8 scale_shift = 5+6;
static uint8 bs = 0; // bitshift = bitdepth - 8
static uint8 Y_min = 0;
static uint8 Y_max = 255;
static uint8 C_min = 0;
static uint8 C_max = 255;
static int csubx = 2;
static int csuby = 2;

// Processing pipeline (needs only 2 registers for each color actually, for horizontal deblocking)
static int16 grain[3][32]; // 9 bit needed because of overlap (has norm > 1)
static uint8 scale[3][32];

// Line buffers (software implementation)
static uint8 offset_x[3][256]; //
static uint8 offset_y[3][256]; // max. 4K image width
static int8  sign[3][256];     //
static int8  grain_buf[18][4096]; // TODO: cache-aligned alloc
static int8  over_buf[2][4096];   // TODO: cache-aligned alloc
static uint8 scale_buf[18][4096]; // last 2 lines never read

/** Pseudo-random number generator */
static uint32 prng(uint32 x)
{
	uint32 s = ((x << 30) ^ (x << 2)) & 0x80000000;
	x = s | (x >> 1);
	return x;
}

/** Derive Y x/y offsets from (random) number
 *
 * Bit fields are designed to minimize overlaps across color channels, to
 * decorrelate them as much as possible.
 *
 * 10-bit for 12 or 13 bins makes a reasonably uniform distribution (1.2%
 * probability error).
 *
 * If 8-bit is requested to further simplify the multiplier, at the cost of less
 * uniform probability, the following bitfields can be considered:
 *
 * Y: sign = rnd[31], x = (rnd[7:0]*13 >> 8)*4,   y = (rnd[21:14]*12 >> 8)*4
 * U: sign = rnd[0],  x = (rnd[17:10]*13 >> 8)*2, y = (rnd[31:24]*12 >> 8)*2
 * V: sign = rnd[13], x = (rnd[27:20]*13 >> 8)*2, y = (rnd[11:4]*12 >> 8)*2
 *
 * Note: to fully support cross-component correlation within patterns, we would
 * need to align luma/chroma offsets.
 */
static void get_offset_y(uint32 rnd, int *s, uint8 *x, uint8 *y)
{
	uint32 bf; // bit field

	*s = ((rnd >> 31) & 1) ? -1 : 1;

	bf = (rnd >> 0) & 0x3ff;
	*x = ((bf * 13) >> 10) * 4; // 13 = 8 + 4 + 1 (two adders)

	bf = (rnd >> 14) & 0x3ff;
	*y = ((bf * 12) >> 10) * 4; // 12 = 8 + 4 (one adder)
	// Note: could shift 9 and * 2, to make a multiple of 2 and make use of all
	// pattern samples (when using overlap).
}

static void get_offset_u(uint32 rnd, int *s, uint8 *x, uint8 *y)
{
	uint32 bf; // bit field

	*s = ((rnd >> 2) & 1) ? -1 : 1;

	bf = (rnd >> 10) & 0x3ff;
	*x = ((bf * 13) >> 10) * (4/csubx);

	bf = ((rnd >> 24) & 0x0ff) | ((rnd << 8) & 0x300);
	*y = ((bf * 12) >> 10) * (4/csuby);
}

static void get_offset_v(uint32 rnd, int *s, uint8 *x, uint8 *y)
{
	uint32 bf; // bit field

	*s = ((rnd >> 15) & 1) ? -1 : 1;

	bf = (rnd >> 20) & 0x3ff;
	*x = ((bf * 13) >> 10) * (4/csubx);

	bf = (rnd >> 4) & 0x3ff;
	*y = ((bf * 12) >> 10) * (4/csuby);
}

static void add_grain_block(void* I, int c, int x, int y, int width)
{
	uint8 *I8 = (uint8*)I;
	uint16 *I16 = (uint16*)I;

	int s, s_up;         // random sign flip (current + upper row)
	uint8 ox, oy;        // random offset (current)
	uint8 ox_up, oy_up;  // random offset (upper row)
	uint8 oc1, oc2;      // overlapping coefficients
	uint8 pi;            // pattern index integer part
	int i, j;
	int P;               // Pattern sample (from current pattern index)
#if PATTERN_INTERPOLATION
	int Pn;              // Next-pattern sample (from pattern index+1)
	uint8 pf;            // pattern index fractional part
#endif

	uint8 intensity;
	int flush = 0;
	int subx = c ? csubx : 1;
	int suby = c ? csuby : 1;
	uint8 I_min = c ? C_min : Y_min;
	uint8 I_max = c ? C_max : Y_max;

	if ((y & 1) && suby > 1)
		return;

	assert(!(x & 15));
	assert(width > 128);
	assert(bs == 0 || bs == 2);
	assert(scale_shift + bs >= 8 && scale_shift + bs <= 13);
	// TODO: assert subx, suby, Y/C min/max, max pLUT values, etc

	j = y & 0xf;

	if (y > 15 && j == 0) // first line of overlap
	{
		oc1 = (suby > 1) ? 20 : 12; // current
		oc2 = (suby > 1) ? 20 : 24; // upper
	}
	else if (y > 15 && j == 1) // second line of overlap
	{
		oc1 = 24;
		oc2 = 12;
	}
	else
	{
		oc1 = oc2 = 0;
	}

	// Derive block offsets + sign
	if (c==0)
		get_offset_y(rnd, &s, &ox, &oy);
	else if (c==1)
		get_offset_u(rnd, &s, &ox, &oy);
	else
		get_offset_v(rnd, &s, &ox, &oy);
	oy += j/suby;

	// Same for upper block (overlap)
	if (c==0)
		get_offset_y(rnd_up, &s_up, &ox_up, &oy_up);
	else if (c==1)
		get_offset_u(rnd_up, &s_up, &ox_up, &oy_up);
	else
		get_offset_v(rnd_up, &s_up, &ox_up, &oy_up);
	oy_up += (16 + j)/suby;

	// Make grain pattern
	for (i=0; i<16/subx; i++)
	{
		intensity = bs ? I16[x/subx+i] >> bs : I8[x/subx+i];
		pi = pLUT[c][intensity] >> 4; // pattern index (integer part)
#if PATTERN_INTERPOLATION
		pf = pLUT[c][intensity] & 15; // fractional part (interpolate with next) -- could restrict to less bits (e.g. 2)
#endif

		// Pattern
		P  = pattern[c?1:0][pi  ][oy][ox + i] * s; // We could consider just XORing the sign bit
#if PATTERN_INTERPOLATION
		Pn = pattern[c?1:0][pi+1][oy][ox + i] * s; // But there are equivalent hw tricks, e.g. storing values as sign + amplitude instead of two's complement
#endif

		if (oc1) // overlap
		{
			P  = round(P  * oc1 + pattern[c?1:0][pi  ][oy_up][ox_up + i] * oc2 * s_up, 5);
#if PATTERN_INTERPOLATION
			Pn = round(Pn * oc1 + pattern[c?1:0][pi+1][oy_up][ox_up + i] * oc2 * s_up, 5);
#endif
		}

#if PATTERN_INTERPOLATION
		// Pattern interpolation: P is current, Pn is next, pf is interpolation coefficient
		grain[c][16/subx+i] = round(P * (16-pf) + Pn * pf, 4);
#else
		grain[c][16/subx+i] = P;
#endif

		// Scale sign already integrated above because of overlap
		scale[c][16/subx+i] = sLUT[c][intensity];
	}

	// Scale & output
	do
	{
		if (x > 0)
		{
			int32 g;
			int16 l1, l0, r0, r1;

			if (!flush)
			{
				// Horizontal deblock (across previous block)
				l1 = grain[c][16/subx -2];
				l0 = grain[c][16/subx -1];
				r0 = grain[c][16/subx +0];
				r1 = grain[c][16/subx +1];
				grain[c][16/subx -1] = round(l1 + 3*l0 + r0, 2);
				grain[c][16/subx +0] = round(l0 + 3*r0 + r1, 2);
			}
			for (i=0; i<16/subx; i++)
			{
				// Output previous block (or flush current)
				g = round(scale[c][i] * (int16)grain[c][i], scale_shift);
				if (bs)
					I16[(x-16)/subx+i] = max(I_min<<bs, min(I_max<<bs, I16[(x-16)/subx+i] + g));
				else
					I8[(x-16)/subx+i] = max(I_min, min(I_max, I8[(x-16)/subx+i] + g));
			}
		}

		// Shift pipeline
		for (i=0; i<16/subx && !flush; i++)
		{
			grain[c][i] = grain[c][i+16/subx];
			scale[c][i] = scale[c][i+16/subx];
		}

		if (x + 16 >= width)
		{
			flush ++;
			x += 16;
		}
	} while (flush == 1);
}

/* Public interface ***********************************************************/

void vfgs_add_grain_line(void* Y, void* U, void* V, int y, int width)
{
	// Generate / backup / restore per-line random seeds (needed to make multi-line blocks)
	if (y && (y & 0x0f) == 0)
	{
		// new line of blocks --> backup + copy current to upper
		line_rnd_up = line_rnd;
		line_rnd = rnd;
	}
	rnd_up = line_rnd_up;
	rnd = line_rnd;

	// Process line
	for (int x=0; x<width; x+=16)
	{
		// Process pixels for each color component
		add_grain_block(Y, 0, x, y, width);
		add_grain_block(U, 1, x, y, width);
		add_grain_block(V, 2, x, y, width);

		// Crank random generator
		rnd = prng(rnd);
		rnd_up = prng(rnd_up); // upper block (overlapping)
	}
}

void vfgs_add_grain_stripe(void* Y, void* U, void* V, unsigned y, unsigned width, unsigned height, unsigned stride)
{
	unsigned x, i;
	uint8 *I8;
	uint16 *I16;
	int overlap=0;

	// TODO could assert(height%16) if YUV memory is padded properly
	assert(width>128 && width<=4096 && width<=stride);
	assert((stride & 0x0f) == 0 && stride<=4096);
	assert((y & 0x0f) == 0);
	assert(bs == 0 || bs == 2);
	assert(scale_shift + bs >= 8 && scale_shift + bs <= 13);
	// TODO: assert subx, suby, Y/C min/max, max pLUT values, etc

	// Generate random offsets
	for (x=0; x<width; x+=16)
	{
		int s[3];
		get_offset_y(rnd, &s[0], &offset_x[0][x/16], &offset_y[0][x/16]);
		get_offset_u(rnd, &s[1], &offset_x[1][x/16], &offset_y[1][x/16]);
		get_offset_v(rnd, &s[2], &offset_x[2][x/16], &offset_y[2][x/16]);
		rnd = prng(rnd);
		sign[0][x/16] = s[0];
		sign[1][x/16] = s[1];
		sign[2][x/16] = s[2];
	}

	// Compute stripe height (including overlap for next stripe)
	overlap = (y > 0);
	height = min(18, height-y);

	// Y: get grain & scale
	I8 = (uint8*)Y;
	I16 = (uint16*)Y;
	for (y=0; y<height; y++)
	{
		for (x=0; x<width; x+=16)
		{
			int    s = sign[0][x/16];
			uint8 ox = offset_x[0][x/16];
			uint8 oy = offset_y[0][x/16];
			for (i=0; i<16; i++) // may overflow past right image border but no problem: allocated space is multiple of 16
			{
				uint8 intensity = bs ? I16[x+i] >> bs : I8[x+i];
				uint8 pi = pLUT[0][intensity] >> 4; // pattern index (integer part) / TODO: try also with zero-shift
				// TODO: assert(pi < VFGS_MAX_PATTERNS); // out of loop ?
				uint8 P  = pattern[0][pi][oy + y][ox + i] * s; // We could consider just XORing the sign bit
				grain_buf[y][x+i] = P;
				scale_buf[y][x+i] = sLUT[0][intensity];
			}
		}
		I8  += stride;
		I16 += stride;
	}

	// Y: vertical overlap (merge lines over_buf with 0 & 1, then copy 16 & 17 to over_buf)
	// problem: need to store 9-bits now ? or just clip ?
	for (y=0; y<2 && overlap; y++)
	{
		uint8 oc1 = y ? 24 : 12; // current
		uint8 oc2 = y ? 12 : 24; // previous
		for (x=0; x<width; x++)
		{
			int16 g = round(oc1*grain_buf[y][x+i] + oc2*over_buf[y][x+i], 5);
			grain_buf[y][x+i] = max(-127, min(+127, g));
			over_buf[y][x+i] = grain_buf[y+16][x+i];
		}
	}

	// Y: horizontal deblock
	// problem: need to store 9-bits now ? or just clip ?
	// TODO: set grain_buf[y][width] to zero if width == K*16 +1 (to avoid filtering garbage)
	for (y=0; y<16; y++)
		for (x=16; x<width; x+=16)
		{
			int16 l1, l0, r0, r1;
			l1 = grain_buf[y][x -2];
			l0 = grain_buf[y][x -1];
			r0 = grain_buf[y][x +0];
			r1 = grain_buf[y][x +1];
			l1 = round(l1 + 3*l0 + r0, 2); // left
			r1 = round(l0 + 3*r0 + r1, 2); // right
			grain_buf[y][x -1] = max(-127, min(+127, l1));
			grain_buf[y][x +0] = max(-127, min(+127, r1));
		}

	// Y: scale & merge
	height = min(16, height);
	I8 = (uint8*)Y;
	I16 = (uint16*)Y;
	for (y=0; y<height; y++)
	{
		for (x=0; x<width; x++)
		{
			int32 g = round(scale_buf[y][x] * (int16)grain_buf[y][x], scale_shift);
			if (bs)
				I16[x] = max(Y_min<<bs, min(Y_max<<bs, I16[x] + g));
			else
				I8[x] = max(Y_min, min(Y_max, I8[x] + g));
		}
		I8  += stride;
		I16 += stride;
	}

	// U
	// V
}

void vfgs_set_luma_pattern(int index, int8* P)
{
	assert(index >= 0 && index < 8);
	memcpy(pattern[0][index], P, 64*64);
}

void vfgs_set_chroma_pattern(int index, int8 *P)
{
	assert(index >= 0 && index < 8);
	for (int i=0; i<64/csuby; i++)
		memcpy(pattern[1][index][i], P + (64/csuby)*i, 64/csubx);
}

void vfgs_set_scale_lut(int c, uint8 lut[])
{
	assert(c>=0 && c<3);
	memcpy(sLUT[c], lut, 256);
}

void vfgs_set_pattern_lut(int c, uint8 lut[])
{
	assert(c>=0 && c<3);
	memcpy(pLUT[c], lut, 256);
}

void vfgs_set_seed(uint32 seed)
{
	rnd = rnd_up = line_rnd = line_rnd_up = seed;
}

void vfgs_set_scale_shift(int shift)
{
	assert(shift >= 2 && shift < 8);
	scale_shift = shift + 6 - bs;
}

void vfgs_set_depth(int depth)
{
	assert(depth==8 || depth==10);

	if (bs==0 && depth>8)
		scale_shift -= 2;
	if (bs==2 && depth==8)
		scale_shift += 2;

	bs = depth - 8;
}

void vfgs_set_legal_range(int legal)
{
	if (legal)
	{
		Y_min = 16;
		Y_max = 235;
		C_min = 16;
		C_max = 240;
	}
	else
	{
		Y_min = 0;
		Y_max = 255;
		C_min = 0;
		C_max = 255;
	}
}

void vfgs_set_chroma_subsampling(int subx, int suby)
{
	assert(subx==1 || subx==2);
	assert(suby==1 || suby==2);
	csubx = subx;
	csuby = suby;
}

