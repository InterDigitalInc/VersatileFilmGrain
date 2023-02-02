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

#ifndef _VFGS_FW_H_
#define _VFGS_FW_H_

#ifndef int32
#define int32  signed int
#define uint32 unsigned int
#define int16  signed short
#define uint16 unsigned short
#define int8   signed char
#define uint8  unsigned char
#endif

#define SEI_MAX_MODEL_VALUES 6

typedef struct fgs_sei_s {
	uint8 model_id;
	uint8 log2_scale_factor;
	uint8 comp_model_present_flag[3];
	uint16 num_intensity_intervals[3];
	uint8 num_model_values[3];
	uint8 intensity_interval_lower_bound[3][256];
	uint8 intensity_interval_upper_bound[3][256];
	int16 comp_model_value[3][256][SEI_MAX_MODEL_VALUES];
} fgs_sei;

typedef struct fgs_metadata_s {
	uint16 grain_seed;
	uint8 num_y_points; /* 0..14 */
	uint8 point_y_values[14]; /* shall be in increasing order */
	uint8 point_y_scaling[14];
	uint8 chroma_scaling_from_luma;
	uint8 num_cb_points; /* 0..10 */
	uint8 point_cb_values[10];
	uint8 point_cb_scaling[10];
	uint8 num_cr_points; /* 0..10 */
	uint8 point_cr_values[10];
	uint8 point_cr_scaling[10];
	uint8 grain_scaling; /* 8..11 () */
	uint8 ar_coeff_lag; /* 0..3 */
	int16 ar_coeffs_y[24];  /* 16-bit to match comp_model_value, but only 8-bit signed is used here */
	int16 ar_coeffs_cb[25]; /* Last value is a luma injection coefficient */
	int16 ar_coeffs_cr[25];
	uint8 ar_coeff_shift; /* 6..9 (AR coefficients scale down) */
	uint8 grain_scale_shift; /* 0..3 (Gaussian random numbers scale down) */
	uint8  cb_mult;
	uint8  cb_luma_mult;
	uint16 cb_offset; /* 9-bit */
	uint8  cr_mult;
	uint8  cr_luma_mult;
	uint16 cr_offset; /* 9-bit */
	uint8 overlap_flag;
	uint8 clip_to_restricted_range;
} fgs_metadata;

void vfgs_init_sei(fgs_sei* cfg);
void vfgs_init_mtdt(fgs_metadata* cfg);

#endif  // _VFGS_FW_H_

