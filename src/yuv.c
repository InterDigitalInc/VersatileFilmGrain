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

#include "yuv.h"
#include <stdint.h>
#include <assert.h>

#ifdef _MSC_VER
#include <malloc.h>
#else
#include <mm_malloc.h>
#endif

#define ALIGN_SIZE 16 // width & height padded to that
#define ALIGN_MEM  64 // start addr + stride should be a multiple of that

#define uint16 unsigned short
#define uint8  unsigned char

int yuv_alloc(int width, int height, int depth, int format, yuv* frame)
{
	int size;
	int sz = (depth == 8) ? 1 : 2;
	int height2, cheight2;
	int subx = (format > YUV_422) ? 1 : 2;
	int suby = (format > YUV_420) ? 1 : 2;

	frame->depth = depth;

	frame->width = width;
	frame->stride = (width & (ALIGN_MEM-1)) ? (width + ALIGN_MEM) & ~(ALIGN_MEM-1) : width;

	frame->height = height;
	height2 = (height & (ALIGN_SIZE-1)) ? (height + ALIGN_SIZE) & ~(ALIGN_SIZE-1) : height;

	size = frame->stride * height2 * sz;

	width = width / subx;
	frame->cwidth = width;
	frame->cstride = (width & (ALIGN_MEM-1)) ? (width + ALIGN_MEM) & ~(ALIGN_MEM-1) : width;

	height = height / suby;
	frame->cheight = height;
	cheight2 = (height & (ALIGN_SIZE/suby-1)) ? (height + ALIGN_SIZE/suby) & ~(ALIGN_SIZE/suby-1) : height;

	size += frame->cstride * cheight2 * 2 * sz;

	frame->Y = _mm_malloc(size, ALIGN_MEM*sz);
	frame->U = (uint8*) frame->Y + frame->stride * height2 * sz;
	frame->V = (uint8*) frame->U + frame->cstride * cheight2 * sz;

	return !frame->Y;
}

void yuv_free(yuv* frame)
{
	_mm_free(frame->Y);
	frame->Y = NULL;
	frame->U = NULL;
	frame->V = NULL;
}

static void yuv_pad_comp(void* buffer, int width, int height, int stride, int walign, int halign, int depth)
{
	uint8*  buf8  = (uint8* )buffer;
	uint16* buf16 = (uint16*)buffer;
	int width2 = (width & (walign-1)) ? (width + walign) & ~(walign-1) : width;
	int height2 = (height & (halign-1)) ? (height + halign) & ~(halign-1) : height;
	int sz = (depth == 8) ? 1 : 2;
	int i, k;

	assert(walign==ALIGN_SIZE || walign==ALIGN_SIZE/2 || halign==ALIGN_SIZE || halign==ALIGN_SIZE/2);
	assert(((intptr_t)buffer & (ALIGN_MEM*sz-1)) == 0);
	assert((stride & (ALIGN_MEM-1)) == 0);

	if (depth==8)
	{
		for (i=0; i<height; i++)
		{
			for (k=width; k<width2; k++)
				buf8[k] = buf8[width-1];
			buf8 += stride;
		}
		for (;i<height2; i++)
		{
			for (k=0; k<stride; k++)
				buf8[k] = buf8[k-stride];
			buf8 += stride;
		}
	}
	else
	{
		for (i=0; i<height; i++)
		{
			for (k=width; k<width2; k++)
				buf16[k] = buf16[width-1];
			buf16 += stride;
		}
		for (;i<height2; i++)
		{
			for (k=0; k<stride; k++)
				buf16[k] = buf16[k-stride];
			buf16 += stride;
		}
	}
}

void yuv_pad(yuv* frame)
{
	int subx = (frame->width == frame->cwidth) ? 1 : 2;
	int suby = (frame->height == frame->cheight) ? 1 : 2;

	yuv_pad_comp(frame->Y, frame->width, frame->height, frame->stride, ALIGN_SIZE, ALIGN_SIZE, frame->depth);
	yuv_pad_comp(frame->U, frame->cwidth, frame->cheight, frame->cstride, ALIGN_SIZE/subx, ALIGN_SIZE/suby, frame->depth);
	yuv_pad_comp(frame->V, frame->cwidth, frame->cheight, frame->cstride, ALIGN_SIZE/subx, ALIGN_SIZE/suby, frame->depth);
}

static int yuv_read_comp(void* buffer, FILE* file, int width, int height, int stride, int depth)
{
	uint8* buf8 = buffer;
	int sz = (depth == 8) ? 1 : 2;
	int nread = 0;
	int err = 0;
	int i;

	for (i=0; i<height && !err; i++)
	{
		nread = (int)fread(buf8, sz, width, file);
		buf8 += stride*sz;
		err = (nread != width);
	}

	return err;
}

int yuv_read(yuv* frame, FILE* file)
{
	int err = 0;
	err |= yuv_read_comp(frame->Y, file, frame->width, frame->height, frame->stride, frame->depth);
	err |= yuv_read_comp(frame->U, file, frame->cwidth, frame->cheight, frame->cstride, frame->depth);
	err |= yuv_read_comp(frame->V, file, frame->cwidth, frame->cheight, frame->cstride, frame->depth);
	return err;
}

static int yuv_write_comp(void* buffer, FILE* file, int width, int height, int stride, int depth)
{
	uint8* buf8 = buffer;
	int sz = (depth == 8) ? 1 : 2;
	int nwrite = 0;
	int err = 0;
	int i;

	for (i=0; i<height && !err; i++)
	{
		nwrite = (int)fwrite(buf8, sz, width, file);
		buf8 += stride*sz;
		err = (nwrite != width);
	}

	return err;
}

int yuv_write(yuv* frame, FILE* file)
{
	int err = 0;
	err |= yuv_write_comp(frame->Y, file, frame->width, frame->height, frame->stride, frame->depth);
	err |= yuv_write_comp(frame->U, file, frame->cwidth, frame->cheight, frame->cstride, frame->depth);
	err |= yuv_write_comp(frame->V, file, frame->cwidth, frame->cheight, frame->cstride, frame->depth);
	return err;
}

