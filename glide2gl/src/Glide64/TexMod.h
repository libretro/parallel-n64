/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

//****************************************************************
//
// Glide64 - Glide Plugin for Nintendo 64 emulators
// Project started on December 29th, 2001
//
// Authors:
// Dave2001, original author, founded the project in 2001, left it in 2002
// Gugaman, joined the project in 2002, left it in 2002
// Sergey 'Gonetz' Lipski, joined the project in 2002, main author since fall of 2002
// Hiroshi 'KoolSmoky' Morii, joined the project in 2007
//
//****************************************************************
//
// To modify Glide64:
// * Write your name and (optional)email, commented by your work, so I know who did it, and so that you can find which parts you modified when it comes time to send it to me.
// * Do NOT send me the whole project or file that you modified.  Take out your modified code sections, and tell me where to put them.  If people sent the whole thing, I would have many different versions, but no idea how to combine them all.
//
//****************************************************************

static void mod_tex_inter_col_using_col1 (uint16_t *dst, int size, uint32_t color0, uint32_t color1)
{
	float percent_r = ((color1 >> 12) & 0xF) / 15.0f;
	float percent_g = ((color1 >> 8) & 0xF) / 15.0f;
	float percent_b = ((color1 >> 4) & 0xF) / 15.0f;
	float percent_r_i = 1.0f - percent_r;
	float percent_g_i = 1.0f - percent_g;
	float percent_b_i = 1.0f - percent_b;

	uint32_t cr = (color0 >> 12) & 0xF;
	uint32_t cg = (color0 >> 8) & 0xF;
	uint32_t cb = (color0 >> 4) & 0xF;

   do
   {
      uint8_t r = (percent_r_i * (((*dst) >> 8) & 0xF) + percent_r * cr);
      uint8_t g = (percent_g_i * (((*dst) >> 4) & 0xF) + percent_g * cg);
      uint8_t b = (percent_b_i * ((*dst) & 0xF) + percent_b * cb);
      *(dst++) = (*dst & 0xF000) | (r << 8) | (g << 4) | b;
   }while(--size);
}

static void mod_col_inter_col1_using_tex (uint16_t *dst, int size, uint32_t color0, uint32_t color1)
{
   uint32_t cr0 = (color0 >> 12) & 0xF;
   uint32_t cg0 = (color0 >> 8) & 0xF;
   uint32_t cb0 = (color0 >> 4) & 0xF;
   uint32_t cr1 = (color1 >> 12) & 0xF;
   uint32_t cg1 = (color1 >> 8) & 0xF;
   uint32_t cb1 = (color1 >> 4) & 0xF;

   do
   {
      float percent_r = (((*dst) >> 8) & 0xF) / 15.0f;
      float percent_g = (((*dst) >> 4) & 0xF) / 15.0f;
      float percent_b = ((*dst) & 0xF) / 15.0f;
      uint8_t r = min(15, (uint8_t)((1.0f-percent_r) * cr0 + percent_r * cr1 + 0.0001f));
      uint8_t g = min(15, (uint8_t)((1.0f-percent_g) * cg0 + percent_g * cg1 + 0.0001f));
      uint8_t b = min(15, (uint8_t)((1.0f-percent_b) * cb0 + percent_b * cb1 + 0.0001f));
      *(dst++) = (*dst & 0xF000) | (r << 8) | (g << 4) | b;
   }while(--size);
}

static void mod_col2_inter__col_inter_col1_using_tex__using_texa (uint16_t *dst, int size,
																  uint32_t color0, uint32_t color1,
																  uint32_t color2)
{
	uint32_t cr0 = (color0 >> 12) & 0xF;
	uint32_t cg0 = (color0 >> 8) & 0xF;
	uint32_t cb0 = (color0 >> 4) & 0xF;
	uint32_t cr1 = (color1 >> 12) & 0xF;
	uint32_t cg1 = (color1 >> 8) & 0xF;
	uint32_t cb1 = (color1 >> 4) & 0xF;
	uint32_t cr2 = (color2 >> 12) & 0xF;
	uint32_t cg2 = (color2 >> 8) & 0xF;
	uint32_t cb2 = (color2 >> 4) & 0xF;

   do
   {
      float percent_a = ((*dst & 0xF000) >> 12) / 15.0f;
      float percent_r = (((*dst) >> 8) & 0xF) / 15.0f;
      float percent_g = (((*dst) >> 4) & 0xF) / 15.0f;
      float percent_b = (*dst & 0xF) / 15.0f;
      uint8_t r = (((1.0f-percent_r) * cr0 + percent_r * cr1) * percent_a + cr2 * (1.0f-percent_a));
      uint8_t g = (((1.0f-percent_g) * cg0 + percent_g * cg1) * percent_a + cg2 * (1.0f-percent_a));
      uint8_t b = (((1.0f-percent_b) * cb0 + percent_b * cb1) * percent_a + cb2 * (1.0f-percent_a));
      *(dst++) = (*dst & 0xF000) | (r << 8) | (g << 4) | b;
   }while(--size);
}

static void mod_tex_sub_col_mul_fac_add_tex (uint16_t *dst, int size, uint32_t color, uint32_t factor)
{
   int i;
	float percent = factor / 255.0f;
	uint32_t cr, cg, cb;
	uint16_t col, a;
	float r, g, b;

	cr = (color >> 12) & 0xF;
	cg = (color >> 8) & 0xF;
	cb = (color >> 4) & 0xF;

	for (i = 0; i < size; i++)
	{
		col = *dst;
		a = col & 0xF000;
		r = (float)((col >> 8) & 0xF);
		r = (r - cr) * percent + r;
		if (r > 15.0f) r = 15.0f;
		if (r < 0.0f) r = 0.0f;
		g = (float)((col >> 4) & 0xF);
		g = (g - cg) * percent + g;
		if (g > 15.0f) g = 15.0f;
		if (g < 0.0f) g = 0.0f;
		b = (float)(col & 0xF);
		b = (b - cb) * percent + b;
		if (b > 15.0f) b = 15.0f;
		if (b < 0.0f) b = 0.0f;

		*(dst++) = a | ((uint16_t)r << 8) | ((uint16_t)g << 4) | (uint16_t)b;
	}
}

static void mod_tex_scale_col_add_col (uint16_t *dst, int size, uint32_t color0, uint32_t color1)
{
   int i;
	uint32_t cr0, cg0, cb0, cr1, cg1, cb1;
	uint16_t col;
	uint8_t r, g, b;
	uint16_t a;
	float percent_r, percent_g, percent_b;

	cr0 = (color0 >> 12) & 0xF;
	cg0 = (color0 >> 8) & 0xF;
	cb0 = (color0 >> 4) & 0xF;
	cr1 = (color1 >> 12) & 0xF;
	cg1 = (color1 >> 8) & 0xF;
	cb1 = (color1 >> 4) & 0xF;

	for (i = 0; i < size; i++)
	{
		col = *dst;
		a = col & 0xF000;
		percent_r = ((col >> 8) & 0xF) / 15.0f;
		percent_g = ((col >> 4) & 0xF) / 15.0f;
		percent_b = (col & 0xF) / 15.0f;
		r = min(15, (uint8_t)(percent_r * cr0 + cr1 + 0.0001f));
		g = min(15, (uint8_t)(percent_g * cg0 + cg1 + 0.0001f));
		b = min(15, (uint8_t)(percent_b * cb0 + cb1 + 0.0001f));
		*(dst++) = a | (r << 8) | (g << 4) | b;
	}
}

static void mod_tex_add_col (uint16_t *dst, int size, uint32_t color)
{
   uint32_t cr = (color >> 12) & 0xF;
   uint32_t cg = (color >> 8) & 0xF;
   uint32_t cb = (color >> 4) & 0xF;

   do
   {
      uint8_t r = (uint8_t)(cr + (((*dst) >> 8) & 0xF))&0xF;
      uint8_t g = (uint8_t)(cg + (((*dst) >> 4) & 0xF))&0xF;
      uint8_t b = (uint8_t)(cb + ((*dst) & 0xF))&0xF;
      *(dst++) = ((((*dst) >> 12) & 0xF) << 12) | (r << 8) | (g << 4) | b;
   }while(--size);
}

static void mod_col_mul_texa_add_tex (uint16_t *dst, int size, uint32_t color)
{
   uint32_t	cr = (color >> 12) & 0xF;
   uint32_t cg = (color >> 8) & 0xF;
   uint32_t cb = (color >> 4) & 0xF;

   do
	{
		float factor = ((*dst & 0xF000) >> 12) / 15.0f;
		uint8_t r = (uint8_t)(cr*factor + (((*dst) >> 8) & 0xF))&0xF;
		uint8_t g = (uint8_t)(cg*factor + (((*dst) >> 4) & 0xF))&0xF;
		uint8_t b = (uint8_t)(cb*factor + ((*dst) & 0xF))&0xF;
		*(dst++) = (*dst & 0xF000) | (r << 8) | (g << 4) | b;
	}while(--size);
}

static void mod_tex_sub_col_mul_fac (uint16_t *dst, int size, uint32_t color, uint32_t factor)
{
   int i;
	float percent = factor / 255.0f;
	uint32_t cr, cg, cb;
	uint16_t col, a;
	float r, g, b;

	cr = (color >> 12) & 0xF;
	cg = (color >> 8) & 0xF;
	cb = (color >> 4) & 0xF;

	for (i = 0; i < size; i++)
	{
		col = *dst;
		a = (uint8_t)((col >> 12) & 0xF);
		r = (float)((col >> 8) & 0xF);
		r = (r - cr) * percent;
		if (r > 15.0f) r = 15.0f;
		if (r < 0.0f) r = 0.0f;
		g = (float)((col >> 4) & 0xF);
		g = (g - cg) * percent;
		if (g > 15.0f) g = 15.0f;
		if (g < 0.0f) g = 0.0f;
		b = (float)(col & 0xF);
		b = (b - cb) * percent;
		if (b > 15.0f) b = 15.0f;
		if (b < 0.0f) b = 0.0f;

		*(dst++) = (a << 12) | ((uint16_t)r << 8) | ((uint16_t)g << 4) | (uint16_t)b;
	}
}

static void mod_col_inter_tex_using_col1 (uint16_t *dst, int size, uint32_t color0, uint32_t color1)
{
	float percent_r = ((color1 >> 12) & 0xF) / 15.0f;
	float percent_g = ((color1 >> 8) & 0xF) / 15.0f;
	float percent_b = ((color1 >> 4) & 0xF) / 15.0f;
	float percent_r_i = 1.0f - percent_r;
	float percent_g_i = 1.0f - percent_g;
	float percent_b_i = 1.0f - percent_b;

	uint32_t cr = (color0 >> 12) & 0xF;
	uint32_t cg = (color0 >> 8) & 0xF;
	uint32_t cb = (color0 >> 4) & 0xF;

   do
	{
		uint8_t r = (percent_r * (((*dst) >> 8) & 0xF) + percent_r_i * cr);
		uint8_t g = (percent_g * (((*dst) >> 4) & 0xF) + percent_g_i * cg);
		uint8_t b = (percent_b * ((*dst) & 0xF) + percent_b_i * cb);
		*(dst++) = ((((*dst) >> 12) & 0xF) << 12) | (r << 8) | (g << 4) | b;
	}while(--size);
}

static void mod_tex_inter_noise_using_col (uint16_t *dst, int size, uint32_t color)
{
	float percent_r = ((color >> 12) & 0xF) / 15.0f;
	float percent_g = ((color >> 8) & 0xF) / 15.0f;
	float percent_b = ((color >> 4) & 0xF) / 15.0f;
	float percent_r_i = 1.0f - percent_r;
	float percent_g_i = 1.0f - percent_g;
	float percent_b_i = 1.0f - percent_b;

   do
   {
      uint8_t noise = rand()%16;
      uint8_t r = (percent_r_i * (((*dst) >> 8) & 0xF) + percent_r * noise);
      uint8_t g = (percent_g_i * (((*dst) >> 4) & 0xF) + percent_g * noise);
      uint8_t b = (percent_b_i * (*dst & 0xF) + percent_b * noise);
      *(dst++) = (*dst & 0xF000) | (r << 8) | (g << 4) | b;
   }while(--size);
}

static void mod_tex_inter_col_using_texa (uint16_t *dst, int size, uint32_t color)
{
   uint32_t cr = (color >> 12) & 0xF;
   uint32_t cg = (color >> 8) & 0xF;
   uint32_t cb = (color >> 4) & 0xF;

   do
   {
      float percent = ((*dst & 0xF000) >> 12) / 15.0f;
      float percent_i = 1.0f - percent;
      uint8_t r = (percent * cr + percent_i * ((*dst & 0x0F00) >> 8));
      uint8_t g = (percent * cg + percent_i * ((*dst & 0x00F0) >> 4));
      uint8_t b = (percent * cb + percent_i * (*dst & 0x000F));
      *(dst++) = (*dst & 0xF000) | (r << 8) | (g << 4) | b;
   }while(--size);
}

static void mod_tex_mul_col (uint16_t *dst, int size, uint32_t color)
{
   float cr = (float)((color >> 12) & 0xF)/16.0f;
   float cg = (float)((color >> 8) & 0xF)/16.0f;
   float cb = (float)((color >> 4) & 0xF)/16.0f;

   do
   {
      uint8_t r = (cr * ((*dst & 0x0F00) >> 8));
      uint8_t g = (cg * ((*dst & 0x00F0) >> 4));
      uint8_t b = (cb * (*dst & 0x000F));
      *(dst++) = (*dst & 0xF000) | (r << 8) | (g << 4) | b;
   }while(--size);
}

static void mod_tex_scale_fac_add_col (uint16_t *dst, int size, uint32_t color, uint32_t factor)
{
   float percent = factor / 255.0f;
   uint32_t cr = (color >> 12) & 0xF;
   uint32_t cg = (color >> 8) & 0xF;
   uint32_t cb = (color >> 4) & 0xF;

   do
   {
      uint8_t r = cr + percent * (float)(((*dst) >> 8) & 0xF);
      uint8_t g = cg + percent * (float)(((*dst) >> 4) & 0xF);
      uint8_t b = cb + percent * (float)(*dst & 0xF);
      *(dst++) = (*dst & 0xF000) | (r << 8) | (g << 4) | b;
   }while(--size);
}
