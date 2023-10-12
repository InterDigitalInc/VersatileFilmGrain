/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2022-2023, InterDigital
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

#include "vfgs_fw.h"
#include "vfgs_hw.h"
#include "yuv.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#endif

#define DEFAULT_FREQ 8
#define CHECK(cond, ...) { if (!(cond)) { fprintf(stderr, "Error: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); return 1; } }

// Program parameters
static FILE* fsrc = NULL;
static FILE* fdst = NULL;
static int width = 1920;
static int height = 1080;
static int depth = 10;
static int odepth = 0;
static int frames = 0;
static int seek = 0;
static int format = YUV_420;

static fgs_sei sei = {
	.model_id = 0,
	.log2_scale_factor = 5,
	.comp_model_present_flag = { 1, 1, 1 },
	.num_intensity_intervals = { 8, 8, 8 },
	.num_model_values = { 3, 3, 3 },
	.intensity_interval_lower_bound = {
		{  0, 40,  60,  80, 100, 120, 140, 160 },
		{  0, 64,  96, 112, 128, 144, 160, 192 },
		{  0, 64,  96, 112, 128, 144, 160, 192 }
	},
	.intensity_interval_upper_bound = {
		{ 39, 59,  79,  99, 119, 139, 159, 255 },
		{ 63, 95, 111, 127, 143, 159, 191, 255 },
		{ 63, 95, 111, 127, 143, 159, 191, 255 }
	},
	.comp_model_value = {
		// luma (scale / h / v)
		{
			{ 100,  7,  7 },
			{ 100,  8,  8 },
			{ 100,  9,  9 },
			{ 110, 10, 10 },
			{ 120, 11, 11 },
			{ 135, 12, 12 },
			{ 145, 13, 13 },
			{ 180, 14, 14 },
		},
		// Cb
		{
			{ 128, 8, 8 },
			{  96, 8, 8 },
			{  64, 8, 8 },
			{  64, 8, 8 },
			{  64, 8, 8 },
			{  64, 8, 8 },
			{  96, 8, 8 },
			{ 128, 8, 8 },
		},
		// Cr
		{
			{ 128, 8, 8 },
			{  96, 8, 8 },
			{  64, 8, 8 },
			{  64, 8, 8 },
			{  64, 8, 8 },
			{  64, 8, 8 },
			{  96, 8, 8 },
			{ 128, 8, 8 },
		},
	}
};

static fgs_afgs1 afgs1 = {
	.num_y_points = 0,
	// no other default values (default mode is SEI)
};

static int read_array_i16(int16* x, char* s)
{
	while (isdigit(*s) || *s=='-' || *s=='+')
	{
		*x++ = atoi(s);
		while (isdigit(*s) || *s=='-' || *s=='+')
			s++;
		while (isblank(*s))
			s++;
	}
	return 0;
}

static int read_array_u8(uint8* x, char* s)
{
	while (isdigit(*s))
	{
		*x++ = atoi(s);
		while (isdigit(*s))
			s++;
		while (isblank(*s))
			s++;
	}
	return 0;
}

/** Fill model array with default values when unspecified */
static void fill_model_array(int16 *x, int n, int model_id)
{
	x += n;
	if (n<2) { *x++ = model_id ? 0 : DEFAULT_FREQ; } // H high cutoff / 1st AR coef (left & top)
	if (n<3) { x[0] = model_id ? 0 : x[-1]; x++;   } // V high cutoff / x-comp corr
	if (n<4) { *x++ = 0;                           } // H low cutoff / 2nd AR coef (top-left, top-right)
	if (n<5) { *x++ = model_id << sei.log2_scale_factor; } // V low cutoff / aspect ratio
	if (n<6) { *x++ = 0;                           } // x-comp corr / 3rd AR coef (left-left, top-top)
}

static int read_model_array(int16 *x, char* s, int n, int model_id)
{
	int i;

	// Note: frequency cutoffs are included bounds (<=)

	while (isdigit(*s) || *s=='-' || *s=='+')
	{
		for (i=0; i<n; i++)
		{
			*x++ = atoi(s);
			while (isdigit(*s) || *s=='-' || *s=='+')
				s++;
			while (isblank(*s))
				s++;
		}
		fill_model_array(x-n, n, model_id);
		x += SEI_MAX_MODEL_VALUES-n;
	}
	return 0;
}

static int read_format(const char* s)
{
	if      (!strcasecmp(s, "444")) return YUV_444;
	else if (!strcasecmp(s, "422")) return YUV_422;
	else                            return YUV_420;
}

static const char* format_str(int format)
{
	if      (format == YUV_420) return "420";
	else if (format == YUV_422) return "422";
	else if (format == YUV_444) return "444";
	else                        return "???";
}

static int adjust_chroma_cfg()
{
	if (sei.model_id == 0)
	{
		// Conversion of component model values for 4:2:2 and 4:2:0 chroma formats
		for (int c=1; c<3; c++)
			if (sei.comp_model_present_flag[c])
				for (int k=0; k<sei.num_intensity_intervals[c]; k++)
				{
					if (format < YUV_444)
						sei.comp_model_value[c][k][1] = max(2, min(14, sei.comp_model_value[c][k][1] << 1)); // Horizontal frequency
					if (format < YUV_422)
						sei.comp_model_value[c][k][2] = max(2, min(14, sei.comp_model_value[c][k][2] << 1)); // Vertical frequency

					if (format == YUV_420)
						sei.comp_model_value[c][k][0] >>= 1;
					else if (format == YUV_422)
						sei.comp_model_value[c][k][0] = (sei.comp_model_value[c][k][0] * 181 + 128) >> 8;
				}
	}

	return 0;
}

static int check_cfg_sei()
{
	// Unsupported features
	CHECK(format == YUV_420 || (!sei.comp_model_present_flag[1] && !sei.comp_model_present_flag[2]), "color grain currently not supported on yuv422 and yuv444 formats");
	CHECK(sei.model_id==0 || (!sei.comp_model_present_flag[1] && !sei.comp_model_present_flag[2]), "color grain currently not supported in SEI.AR mode");

	// Sanity checks
	CHECK(sei.model_id <= 1, "SEIFGCModelId shall be 0 or 1");
	for (int c = 0; c < 3; c++)
	{
		if (sei.comp_model_present_flag[c])
		{
			int rng = (1 << depth);
			CHECK(sei.num_model_values[c] >= 1 && sei.num_model_values[c] <= 6, "SEIFGCNumModelValuesMinus1Comp%d out of 0..5 range", c);
			for (int i = 0; i < sei.num_intensity_intervals[c]; i++)
			{
				CHECK(sei.intensity_interval_lower_bound[c][i] <= sei.intensity_interval_upper_bound[c][i], "inconsistent interval %d for component %d: upper bound should be larger or equal than lower bound", i, c);

				CHECK(sei.comp_model_value[c][i][0] < rng, "scaling factor for component %d and interval %d is too large", c, i);
				if (sei.model_id == 0) // Frequency-filtering mode
				{
					CHECK(sei.comp_model_value[c][i][1] >= 2 && sei.comp_model_value[c][i][1] <= 14,  "horizontal cutoff frequency for component %d and interval %d out of 2..14 range", c, i);
					CHECK(sei.comp_model_value[c][i][1] >= 2 && sei.comp_model_value[c][i][2] <= 14,  "vertical cutoff frequency for component %d and interval %d out of 2..14 range", c, i);
				}
				else // Auto-regressive
				{
					CHECK(sei.comp_model_value[c][i][1] >= -rng/2 && sei.comp_model_value[c][i][1] < rng/2,  "first AR coefficient for component %d and interval %d is out of range", c, i);
					CHECK(sei.comp_model_value[c][i][3] >= -rng/2 && sei.comp_model_value[c][i][3] < rng/2,  "second AR coefficient for component %d and interval %d is out of range", c, i);
					CHECK(sei.comp_model_value[c][i][5] >= -rng/2 && sei.comp_model_value[c][i][5] < rng/2,  "third AR coefficient for component %d and interval %d is out of range", c, i);
				}
			}
		}
	}

	return 0;
}

static int check_cfg_afgs1()
{
	int i, val;

	// Unsupported features
	CHECK(format == YUV_420 || (!afgs1.num_cb_points && !afgs1.num_cr_points), "color grain currently not supported on yuv422 and yuv444 formats");

	// Check point_y_values are in increasing order
	for (i=1, val=afgs1.point_y_values[0]; i < afgs1.num_y_points; i++)
	{
		CHECK(afgs1.point_y_values[i] > val, "afgs1.point_y_values shall be in increasing order");
		val = afgs1.point_y_values[i];
	}

	// Check point_cb_values are in increasing order
	for (i=1, val=afgs1.point_cb_values[0]; i < afgs1.num_cb_points; i++)
	{
		CHECK(afgs1.point_cb_values[i] > val, "afgs1.point_cb_values shall be in increasing order");
		val = afgs1.point_cb_values[i];
	}

	// Check point_y_values are in increasing order
	for (i=1, val=afgs1.point_cr_values[0]; i < afgs1.num_cr_points; i++)
	{
		CHECK(afgs1.point_cr_values[i] > val, "afgs1.point_cr_values shall be in increasing order");
		val = afgs1.point_cr_values[i];
	}

	return 0;
}

static int check_cfg()
{
	if (afgs1.num_y_points)
		return check_cfg_afgs1();
	else
		return check_cfg_sei();
}

// Read AFGS1 parameters from "grain table" format as output by the grain analyzer in AOM reference software
static int read_afgs1_tbl(FILE* cfg)
{
	char line[1024];
	char *s;
	const char* sep = " \t"; // separators (white space)
	int ncoef;
#   define AERR "AFGS1 table entry: "

	// Header line
	fgets(line, sizeof(line), cfg);
	for (s = line; isblank(*s); s++); // skip whitespace
	s = strtok(line, sep); CHECK(s && !strcmp(s,"E"), AERR "expecting header (E)");
	s = strtok(NULL, sep); // ignore start time (applied immediately)
	s = strtok(NULL, sep); // ignore end time (we never stop)
	s = strtok(NULL, sep); // ignore apply_grain (we always apply)
	s = strtok(NULL, sep); CHECK(s, AERR "missing grain_seed");
	afgs1.grain_seed = atoi(s);
	// ignore update_parameters (we always update)

	// Parameters line
	fgets(line, sizeof(line), cfg);
	for (s = line; isblank(*s); s++); // skip whitespace
	s = strtok(line, sep);             CHECK(s && !strcmp(s,"p"), AERR "expecting parameters (p)");
	s = strtok(NULL, sep);             CHECK(s, AERR "missing ar_coeff_lag");
	afgs1.ar_coeff_lag = atoi(s);      CHECK(afgs1.ar_coeff_lag <= 3, "ar_coeff_lag higher than 3");
	s = strtok(NULL, sep);             CHECK(s, AERR "missing ar_coeff_shift");
	afgs1.ar_coeff_shift = atoi(s);    CHECK(afgs1.ar_coeff_shift >= 6 && afgs1.ar_coeff_shift <= 9, "ar_coeff_shift out of 6..9 range");
	s = strtok(NULL, sep);             CHECK(s, AERR "missing grain_scale_shift");
	afgs1.grain_scale_shift = atoi(s); CHECK(afgs1.grain_scale_shift <= 3, "grain_scale_shift higher than 3");
	s = strtok(NULL, sep);             CHECK(s, AERR "missing grain_scaling");
	afgs1.grain_scaling = atoi(s);     CHECK(afgs1.grain_scaling >= 8 && afgs1.grain_scaling <= 11, "grain_scaling out of 8..11 range");
	s = strtok(NULL, sep);             CHECK(s, AERR "missing chroma_scaling_from_luma");
	afgs1.chroma_scaling_from_luma = atoi(s);
	s = strtok(NULL, sep);             CHECK(s, AERR "missing overlap_flag");
	afgs1.overlap_flag = atoi(s);
	s = strtok(NULL, sep);             CHECK(s, AERR "missing cb_mult");
	afgs1.cb_mult = atoi(s);
	s = strtok(NULL, sep);             CHECK(s, AERR "missing cb_luma_mult");
	afgs1.cb_luma_mult = atoi(s);
	s = strtok(NULL, sep);             CHECK(s, AERR "missing cb_offset");
	afgs1.cb_offset = atoi(s);
	s = strtok(NULL, sep);             CHECK(s, AERR "missing cr_mult");
	afgs1.cr_mult = atoi(s);
	s = strtok(NULL, sep);             CHECK(s, AERR "missing cr_luma_mult");
	afgs1.cr_luma_mult = atoi(s);
	s = strtok(NULL, sep);             CHECK(s, AERR "missing cr_offset");
	afgs1.cr_offset = atoi(s);

	// Y scaling function line
	fgets(line, sizeof(line), cfg);
	for (s = line; isblank(*s); s++); // skip whitespace
	s = strtok(line, sep);             CHECK(s && !strcmp(s,"sY"), AERR "expecting luma scaling function (sY)");
	s = strtok(NULL, sep);             CHECK(s, AERR "missing num_y_points");
	afgs1.num_y_points = atoi(s);      CHECK(afgs1.num_y_points <= 14, "num_y_points higher than 14");
	for (int k=0; k<afgs1.num_y_points; k++)
	{
		s = strtok(NULL, sep);         CHECK(s, AERR "missing luma scaling point (value)");
		afgs1.point_y_values[k] = atoi(s);
		s = strtok(NULL, sep);         CHECK(s, AERR "missing luma scaling point (scale)");
		afgs1.point_y_scaling[k] = atoi(s);
	}

	// Cb scaling function line
	fgets(line, sizeof(line), cfg);
	for (s = line; isblank(*s); s++); // skip whitespace
	s = strtok(line, sep);             CHECK(s && !strcmp(s,"sCb"), AERR "expecting Cb scaling function (sCb)");
	s = strtok(NULL, sep);             CHECK(s, AERR "missing num_cb_points");
	afgs1.num_cb_points = atoi(s);     CHECK(afgs1.num_cb_points <= 10, "num_cb_points higher than 10");
	for (int k=0; k<afgs1.num_cb_points; k++)
	{
		s = strtok(NULL, sep);         CHECK(s, AERR "missing Cb scaling point (value)");
		afgs1.point_cb_values[k] = atoi(s);
		s = strtok(NULL, sep);         CHECK(s, AERR "missing Cb scaling point (scale)");
		afgs1.point_cb_scaling[k] = atoi(s);
	}

	// Cr scaling function line
	fgets(line, sizeof(line), cfg);
	for (s = line; isblank(*s); s++); // skip whitespace
	s = strtok(line, sep);             CHECK(s && !strcmp(s,"sCr"), AERR "expecting Cr scaling function (sCr)");
	s = strtok(NULL, sep);             CHECK(s, AERR "missing num_cr_points");
	afgs1.num_cr_points = atoi(s);     CHECK(afgs1.num_cr_points <= 10, "num_cr_points higher than 10");
	for (int k=0; k<afgs1.num_cr_points; k++)
	{
		s = strtok(NULL, sep);         CHECK(s, AERR "missing Cr scaling point (value)");
		afgs1.point_cr_values[k] = atoi(s);
		s = strtok(NULL, sep);         CHECK(s, AERR "missing Cr scaling point (scale)");
		afgs1.point_cr_scaling[k] = atoi(s);
	}

	// Y coefficients line
	fgets(line, sizeof(line), cfg);
	for (s = line; isblank(*s); s++); // skip whitespace
	s = strtok(line, sep);             CHECK(s && !strcmp(s,"cY"), AERR "expecting luma coefficients");
	ncoef = 2 * afgs1.ar_coeff_lag * (afgs1.ar_coeff_lag + 1);
	for (int k=0; k<ncoef; k++)
	{
		s = strtok(NULL, sep);         CHECK(s, AERR "missing luma AR coefficient");
		afgs1.ar_coeffs_y[k] = atoi(s);
	}

	// Cb coefficients line
	ncoef++;
	fgets(line, sizeof(line), cfg);
	for (s = line; isblank(*s); s++); // skip whitespace
	s = strtok(line, sep);             CHECK(s && !strcmp(s,"cCb"), AERR "expecting Cb coefficients");
	for (int k=0; k<ncoef; k++)
	{
		s = strtok(NULL, sep);         CHECK(s, AERR "missing Cb AR coefficient");
		afgs1.ar_coeffs_cb[k] = atoi(s);
	}

	// Cr coefficients line
	fgets(line, sizeof(line), cfg);
	for (s = line; isblank(*s); s++); // skip whitespace
	s = strtok(line, sep);             CHECK(s && !strcmp(s,"cCr"), AERR "expecting Cr coefficients");
	for (int k=0; k<ncoef; k++)
	{
		s = strtok(NULL, sep);         CHECK(s, AERR "missing Cr AR coefficient");
		afgs1.ar_coeffs_cr[k] = atoi(s);
	}

	// Note: afgs1.clip_to_restricted_range is missing in .tbl files --> set it to a default value ?

	return 0;
}

static int read_cfg(const char* filename)
{
	FILE* cfg;
	char line[1024];
	char *s, *v, *e;
	int c=0, i=0, j=0;
	int cnt1=0, cnt2=0;

	cfg = fopen(filename, "rt");
	if (!cfg)
	{
		printf("Can not open file %s\n\n", filename);
		return 1;
	}

	while (fgets(line, sizeof(line), cfg))
	{
		if (line[0] == '#') // remove comments (special case for 1st character)
			continue;
		s = strtok(line, "#"); // remove comments
		while (isblank(*s)) // skip leading whitespace
			s++;
		s = strtok(s, ":"); // get "name"
		v = strtok(NULL, ":"); // get "value"

		if (v == NULL)
		{
			if (!strncasecmp(s, "filmgrn1", 8))
				return read_afgs1_tbl(cfg);
			else
				continue;
		}
		while (isblank(*v)) // skip leading whitespace of "value"
			v++;
		for (e=s; !isblank(*e) && *e; e++) // trim trailing whitespace of "name"
			;
		*e = '\0';
		cnt1 ++;

		// SEI
		if      (!strcasecmp(s, "SEIFGCModelId"))                          { sei.model_id                   = atoi(v); }
		else if (!strcasecmp(s, "SEIFGCLog2ScaleFactor"))                  { sei.log2_scale_factor          = atoi(v); }
		else if (!strcasecmp(s, "SEIFGCCompModelPresentComp0"))            { sei.comp_model_present_flag[0] = atoi(v); }
		else if (!strcasecmp(s, "SEIFGCCompModelPresentComp1"))            { sei.comp_model_present_flag[1] = atoi(v); }
		else if (!strcasecmp(s, "SEIFGCCompModelPresentComp2"))            { sei.comp_model_present_flag[2] = atoi(v); }
		else if (!strcasecmp(s, "SEIFGCNumIntensityIntervalMinus1Comp0"))  { sei.num_intensity_intervals[0] = atoi(v) + 1; }
		else if (!strcasecmp(s, "SEIFGCNumIntensityIntervalMinus1Comp1"))  { sei.num_intensity_intervals[1] = atoi(v) + 1; }
		else if (!strcasecmp(s, "SEIFGCNumIntensityIntervalMinus1Comp2"))  { sei.num_intensity_intervals[2] = atoi(v) + 1; }
		else if (!strcasecmp(s, "SEIFGCNumModelValuesMinus1Comp0"))        { sei.num_model_values[0]        = atoi(v) + 1; }
		else if (!strcasecmp(s, "SEIFGCNumModelValuesMinus1Comp1"))        { sei.num_model_values[1]        = atoi(v) + 1; }
		else if (!strcasecmp(s, "SEIFGCNumModelValuesMinus1Comp2"))        { sei.num_model_values[2]        = atoi(v) + 1; }
		else if (!strcasecmp(s, "SEIFGCIntensityIntervalLowerBoundComp0")) { read_array_u8(sei.intensity_interval_lower_bound[0], v); }
		else if (!strcasecmp(s, "SEIFGCIntensityIntervalLowerBoundComp1")) { read_array_u8(sei.intensity_interval_lower_bound[1], v); }
		else if (!strcasecmp(s, "SEIFGCIntensityIntervalLowerBoundComp2")) { read_array_u8(sei.intensity_interval_lower_bound[2], v); }
		else if (!strcasecmp(s, "SEIFGCIntensityIntervalUpperBoundComp0")) { read_array_u8(sei.intensity_interval_upper_bound[0], v); }
		else if (!strcasecmp(s, "SEIFGCIntensityIntervalUpperBoundComp1")) { read_array_u8(sei.intensity_interval_upper_bound[1], v); }
		else if (!strcasecmp(s, "SEIFGCIntensityIntervalUpperBoundComp2")) { read_array_u8(sei.intensity_interval_upper_bound[2], v); }
		else if (!strcasecmp(s, "SEIFGCCompModelValuesComp0")) { read_model_array(sei.comp_model_value[0][0], v, sei.num_model_values[0], sei.model_id); }
		else if (!strcasecmp(s, "SEIFGCCompModelValuesComp1")) { read_model_array(sei.comp_model_value[1][0], v, sei.num_model_values[1], sei.model_id); }
		else if (!strcasecmp(s, "SEIFGCCompModelValuesComp2")) { read_model_array(sei.comp_model_value[2][0], v, sei.num_model_values[2], sei.model_id); }

		// SEI, dump style
		else if (!strcasecmp(s, "fg_model_id"))                             { sei.model_id                   = atoi(v); }
		else if (!strcasecmp(s, "fg_log2_scale_factor"))                    { sei.log2_scale_factor          = atoi(v); }
		else if (!strcasecmp(s, "fg_comp_model_present_flag[c]"))           { sei.comp_model_present_flag[c] = atoi(v);     c = (c<2) ? c+1 : 0; }
		else if (!strcasecmp(s, "fg_num_intensity_intervals_minus1[c]"))    { sei.num_intensity_intervals[c] = atoi(v) + 1; }
		else if (!strcasecmp(s, "fg_num_model_values_minus1[c]"))           { sei.num_model_values[c]        = atoi(v) + 1; }
		else if (!strcasecmp(s, "fg_intensity_interval_lower_bound[c][i]")) { sei.intensity_interval_lower_bound[c][i] = atoi(v); }
		else if (!strcasecmp(s, "fg_intensity_interval_upper_bound[c][i]")) { sei.intensity_interval_upper_bound[c][i] = atoi(v); }
		else if (!strcasecmp(s, "fg_comp_model_value[c][i]"))
		{
			sei.comp_model_value[c][i][j++] = atoi(v);
			if (j == sei.num_model_values[c])
			{
				fill_model_array(sei.comp_model_value[c][i], sei.num_model_values[c], sei.model_id);
				i ++; // next intensity interval
				j = 0;
				if (i == sei.num_intensity_intervals[c])
				{
					c ++; // next color component
					i = 0;
				}
			}
		}
		else if (!strcasecmp(s, "fg_characteristics_persistence_flag"))     { break; /* stop at the end of the first FGS SEI */ }

		// AFGS1
		else if (!strcasecmp(s, "AFGS1GrainSeed"))             { afgs1.grain_seed = atoi(v); }
		else if (!strcasecmp(s, "AFGS1NumYPoints"))            { afgs1.num_y_points = atoi(v); CHECK(afgs1.num_y_points <= 14, "AFGS1NumYPoints higher than 14"); }
		else if (!strcasecmp(s, "AFGS1PointYValues"))          { read_array_u8(afgs1.point_y_values, v); }
		else if (!strcasecmp(s, "AFGS1PointYScaling"))         { read_array_u8(afgs1.point_y_scaling, v); }
		else if (!strcasecmp(s, "AFGS1ChromaScalingFromLuma")) { afgs1.chroma_scaling_from_luma = atoi(v); }
		else if (!strcasecmp(s, "AFGS1NumCbPoints"))           { afgs1.num_cb_points = atoi(v); CHECK(afgs1.num_cb_points <= 10, "AFGS1NumCbPoints higher than 10"); }
		else if (!strcasecmp(s, "AFGS1PointCbValues"))         { read_array_u8(afgs1.point_cb_values, v); }
		else if (!strcasecmp(s, "AFGS1PointCbScaling"))        { read_array_u8(afgs1.point_cb_scaling, v); }
		else if (!strcasecmp(s, "AFGS1NumCrPoints"))           { afgs1.num_cr_points = atoi(v); CHECK(afgs1.num_cr_points <= 10, "AFGS1NumCrPoints higher than 10"); }
		else if (!strcasecmp(s, "AFGS1PointCrValues"))         { read_array_u8(afgs1.point_cr_values, v); }
		else if (!strcasecmp(s, "AFGS1PointCrScaling"))        { read_array_u8(afgs1.point_cr_scaling, v); }
		else if (!strcasecmp(s, "AFGS1GrainScaling"))          { afgs1.grain_scaling = atoi(v); CHECK(afgs1.grain_scaling >= 8 && afgs1.grain_scaling <= 11, "AFGS1GrainScaling out of 8..11 range"); }
		else if (!strcasecmp(s, "AFGS1ARCoeffLag"))            { afgs1.ar_coeff_lag = atoi(v); CHECK(afgs1.ar_coeff_lag <= 3, "AFGS1ARCoeffLag higher than 3"); }
		else if (!strcasecmp(s, "AFGS1ARCoeffsY"))             { read_array_i16(afgs1.ar_coeffs_y, v); }
		else if (!strcasecmp(s, "AFGS1ARCoeffsCb"))            { read_array_i16(afgs1.ar_coeffs_cb, v); }
		else if (!strcasecmp(s, "AFGS1ARCoeffsCr"))            { read_array_i16(afgs1.ar_coeffs_cr, v); }
		else if (!strcasecmp(s, "AFGS1ARCoeffShift"))          { afgs1.ar_coeff_shift = atoi(v); CHECK(afgs1.ar_coeff_shift >= 6 && afgs1.ar_coeff_shift <= 9, "AFGS1ARCoeffShift out of 6..9 range"); }
		else if (!strcasecmp(s, "AFGS1GrainScaleShift"))       { afgs1.grain_scale_shift = atoi(v); CHECK(afgs1.grain_scale_shift <= 3, "AFGS1GrainScaleShift higher than 3"); }
		else if (!strcasecmp(s, "AFGS1CbMult"))                { afgs1.cb_mult = atoi(v); }
		else if (!strcasecmp(s, "AFGS1CbLumaMult"))            { afgs1.cb_luma_mult = atoi(v); }
		else if (!strcasecmp(s, "AFGS1CbOffset"))              { afgs1.cb_offset = atoi(v); }
		else if (!strcasecmp(s, "AFGS1CrMult"))                { afgs1.cr_mult = atoi(v); }
		else if (!strcasecmp(s, "AFGS1CrLumaMult"))            { afgs1.cr_luma_mult = atoi(v); }
		else if (!strcasecmp(s, "AFGS1CrOffset"))              { afgs1.cr_offset = atoi(v); }
		else if (!strcasecmp(s, "AFGS1OverlapFlag"))           { afgs1.overlap_flag = atoi(v); }
		else if (!strcasecmp(s, "AFGS1ClipToRestrictedRange")) { afgs1.clip_to_restricted_range = atoi(v); }

		else cnt2 ++;
	}
	CHECK(cnt1 > cnt2, "could not ready anything from configuration file");

	return 0;
}

static void apply_gain(unsigned gain)
{
	if (gain==100)
		return;

	if (afgs1.num_y_points)
	{
		// AFGS1
		for(;gain>100; gain/=2)
			afgs1.grain_scaling --;
		for(;gain && gain<50; gain*=2)
			afgs1.grain_scaling ++;

		for (int i=0; i<afgs1.num_y_points; i++)
			afgs1.point_y_scaling[i] = (uint8)((int)afgs1.point_y_scaling[i] * gain / 100);
		for (int i=0; i<afgs1.num_cb_points; i++)
			afgs1.point_cb_scaling[i] = (uint8)((int)afgs1.point_cb_scaling[i] * gain / 100);
		for (int i=0; i<afgs1.num_cr_points; i++)
			afgs1.point_cr_scaling[i] = (uint8)((int)afgs1.point_cr_scaling[i] * gain / 100);
	}
	else
	{
		// FGC SEI
		for(;gain>100; gain/=2)
			sei.log2_scale_factor --;
		for(;gain && gain<50; gain*=2)
			sei.log2_scale_factor ++;

		for (int c=0; c<3; c++)
			for (int i=0; sei.comp_model_present_flag[c] && i<sei.num_intensity_intervals[c]; i++)
				sei.comp_model_value[c][i][0] = (int16)((int)sei.comp_model_value[c][i][0] * gain / 100);
	}
}

static int help(const char* name)
{
	printf("Usage: %s [options] <input.yuv> <output.yuv>\n\n", name);
	printf("   -w,--width    <value>     Picture width [%d]\n", width);
	printf("   -h,--height   <value>     Picture height [%d]\n", height);
	printf("   -b,--bitdepth <value>     Input bit depth [%d]\n", depth);
	printf("      --outdepth <value>     Output bit depth (<= input depth) [same as input]\n");
	printf("   -f,--format   <value>     Chroma format (420/422/444) [%s]\n", format_str(format));
	printf("   -n,--frames   <value>     Number of frames to process (0=all) [%d]\n", frames);
	printf("   -s,--seek     <value>     Picture start index within input file [%d]\n", seek);
	printf("   -c,--cfg      <filename>  Read film grain configuration file\n");
	printf("   -g,--gain     <value>     Apply a global scale (in percent) to grain strength\n");
	printf("   --help                    Display this page\n\n");
	return 0;
}

static void vfgs_add_grain(yuv* frame)
{
	uint8 *Y = frame->Y;
	uint8 *U = frame->U;
	uint8 *V = frame->V;

	assert(depth == frame->depth);

	for (int y=0; y<frame->height; y++)
	{
		vfgs_add_grain_line(Y, U, V, y, frame->width);
		Y += frame->stride * (depth > 8 ? 2 : 1);
		if ((y & 1) || (frame->height == frame->cheight))
		{
			U += frame->cstride * (depth > 8 ? 2 : 1);
			V += frame->cstride * (depth > 8 ? 2 : 1);
		}
	}
}

int main(int argc, const char **argv)
{
	int i;
	int err=0;
	yuv frame, oframe;
	unsigned gain = 100;

	// Parse parameters
	for (i=1; i<argc && !err; i++)
	{
		const char* param = argv[i];
		if      (!strcasecmp(param, "-w") || !strcasecmp(param, "--width"))       { if (i+1 < argc) width  = atoi(argv[++i]); else err = 1; }
		else if (!strcasecmp(param, "-h") || !strcasecmp(param, "--height"))      { if (i+1 < argc) height = atoi(argv[++i]); else err = 1; }
		else if (!strcasecmp(param, "-b") || !strcasecmp(param, "--bitdepth"))    { if (i+1 < argc) depth  = atoi(argv[++i]); else err = 1; }
		else if (                            !strcasecmp(param, "--outdepth"))    { if (i+1 < argc) odepth = atoi(argv[++i]); else err = 1; }
		else if (!strcasecmp(param, "-f") || !strcasecmp(param, "--format"))      { if (i+1 < argc) format = read_format(argv[++i]); else err = 1; }
		else if (!strcasecmp(param, "-n") || !strcasecmp(param, "--frames"))      { if (i+1 < argc) frames = atoi(argv[++i]); else err = 1; }
		else if (!strcasecmp(param, "-s") || !strcasecmp(param, "--seek"))        { if (i+1 < argc) seek   = atoi(argv[++i]); else err = 1; }
		else if (!strcasecmp(param, "-c") || !strcasecmp(param, "--cfg"))         { if (i+1 < argc) err = read_cfg(argv[++i]); else err = 1; }
		else if (!strcasecmp(param, "-g") || !strcasecmp(param, "--gain"))        { if (i+1 < argc) gain   = atoi(argv[++i]); else err = 1; }
		else if (!strcasecmp(param, "-h") || !strcasecmp(param, "--help"))        { help(argv[0]); return 1; }
		else if (param[0]!='-')
		{
			if (!fsrc)
			{
				fsrc = fopen(param, "rb");
				if (!fsrc)
				{
					printf("Can not open file %s\n\n", param);
					err = 1;
				}
			}
			else if (!fdst)
			{
				fdst = fopen(param, "wb");
				if (!fdst)
				{
					printf("Can not create file %s\n\n", param);
					err = 1;
				}
			}
		}
		else
		{
			printf("Unknown parameter %s\n", param);
			err = 1;
		}
	}
	if (!fsrc || !fdst || err)
	{
		help(argv[0]);
		return 1;
	}
	if (check_cfg())
	{
		return 1;
	}
	odepth = odepth ? odepth : depth;

	assert(depth==8 || depth==10);
	assert((odepth==8 || odepth==10) && (odepth <= depth));
	assert(width>=128);
	assert(height>=128);

	vfgs_set_depth(depth);
	vfgs_set_chroma_subsampling((format < YUV_444)?2:1, (format < YUV_422)?2:1);
	adjust_chroma_cfg();
	apply_gain(gain);

	if (afgs1.num_y_points)
		vfgs_init_afgs1(&afgs1);
	else
		vfgs_init_sei(&sei);

	yuv_alloc(width, height, depth, format, &frame);
	if (odepth < depth)
		yuv_alloc(width, height, odepth, format, &oframe);
	else
		oframe = frame;

	yuv_skip(&frame, seek, fsrc);

	// Process frames
	for (int n = 0; ((frames == 0) || (n < frames)) && !ferror(fsrc); n++)
	{
		yuv_read(&frame, fsrc);
		if (feof(fsrc))
			break;
		//yuv_pad(&frame);
		vfgs_add_grain(&frame);
		if (odepth < depth)
			yuv_to_8bit(&oframe, &frame);
		yuv_write(&oframe, fdst);
	}

	yuv_free(&frame);
	if (odepth < depth)
		yuv_free(&oframe);

	return 0;
}

