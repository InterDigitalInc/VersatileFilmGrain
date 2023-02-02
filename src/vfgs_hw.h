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

#ifndef _VFGS_HW_H_
#define _VFGS_HW_H_

#ifndef int32
#define int32  signed int
#define uint32 unsigned int
#define int16  signed short
#define uint16 unsigned short
#define int8   signed char
#define uint8  unsigned char
#endif

#define VFGS_MAX_PATTERNS 8

void vfgs_set_luma_pattern(int index, int8* P);
void vfgs_set_chroma_pattern(int index, int8 *P);
void vfgs_set_scale_lut(int c, uint8 lut[]);
void vfgs_set_pattern_lut(int c, uint8 lut[]);

void vfgs_set_seed(uint32 seed);
void vfgs_set_scale_shift(int shift);
void vfgs_set_depth(int depth);
void vfgs_set_legal_range(int legal);
void vfgs_set_chroma_subsampling(int subx, int suby);

void vfgs_add_grain_line(void* Y, void* U, void* V, int y, int width);
void vfgs_add_grain_stripe(void* Y, void* U, void* V, unsigned y, unsigned width, unsigned height, unsigned stride);

#endif  // _VFGS_HW_H_

