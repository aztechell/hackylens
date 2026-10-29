/* OV7740 initialization register values adapted from Sipeed MaixPy-v1,
 * components/micropython/port/src/omv/ov7740.c (OpenMV project).
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "ov7740_sensor.h"
#include "../config/camera_config.h"
#include "../internal/hk_board_port.h"
#include "hal_dvp.h"
#include "hal_time.h"
#include <stddef.h>

static const uint8_t default_regs[][2] = {
	{0x47, 0x02}  ,
	{0x17, 0x27}  ,
	{0x04, 0x40}  ,
	{0x1B, 0x81}  ,
	{0x29, 0x17}  ,
	{0x5F, 0x03}  ,
	{0x3A, 0x09}  ,
	{0x33, 0x44}  ,
	{0x68, 0x1A}  ,
	{0x14, 0x38}  ,
	{0x5F, 0x04}  ,
	{0x64, 0x00}  ,
	{0x67, 0x90}  ,
	{0x27, 0x80}  ,
	{0x45, 0x41}  ,
	{0x4B, 0x40}  ,
	{0x36, 0x2f}  ,
	{0x11, 0x00}  ,  // 60fps
	{0x36, 0x3f}  ,
	// {0x0c, 0x12}  , // default YUYV
	{0x12, 0x00}  ,
	{0x17, 0x25}  ,
	{0x18, 0xa0}  ,
	{0x1a, 0xf0}  ,
	{0x31, 0x50}  ,
	{0x32, 0x78}  ,
	{0x82, 0x3f}  ,
	{0x85, 0x08}  ,
	{0x86, 0x02}  ,
	{0x87, 0x01}  ,
	{0xd5, 0x10}  ,
	{0x0d, 0x34}  ,
	{0x19, 0x03}  ,
	{0x2b, 0xf8}  ,
	{0x2c, 0x01}  ,
	{0x53, 0x00}  ,
	{0x89, 0x30}  ,
	{0x8d, 0x30}  ,
	{0x8f, 0x85}  ,
	{0x93, 0x30}  ,
	{0x95, 0x85}  ,
	{0x99, 0x30}  ,
	{0x9b, 0x85}  ,
	{0xac, 0x6E}  ,
	{0xbe, 0xff}  ,
	{0xbf, 0x00}  ,
	{0x38, 0x14}  ,
	{0xe9, 0x00}  ,
	{0x3D, 0x08}  ,
	{0x3E, 0x80}  ,
	{0x3F, 0x40}  ,
	{0x40, 0x7F}  ,
	{0x41, 0x6A}  ,
	{0x42, 0x29}  ,
	{0x49, 0x64}  ,
	{0x4A, 0xA1}  ,
	{0x4E, 0x13}  ,
	{0x4D, 0x50}  ,
	{0x44, 0x58}  ,
	{0x4C, 0x1A}  ,
	{0x4E, 0x14}  ,
	{0x38, 0x11}  ,
	{0x84, 0x70}  ,
	{0,0}
};

static void write_reg(uint8_t reg, uint8_t value)
{
    hal_dvp_sccb_write(CAMERA_SCCB_ADDR, reg, value);
}
uint8_t ov7740_read_reg(uint8_t reg)
{
    return hal_dvp_sccb_read(CAMERA_SCCB_ADDR, reg);
}
void ov7740_init_bus(uint32_t *actual_xclk, uint32_t *actual_sccb)
{
    hk_board_ops.camera_prepare();
    hal_dvp_init_bus(8U);
    *actual_xclk = hal_dvp_set_xclk_rate(CAMERA_XCLK_HZ);
    *actual_sccb = hal_dvp_set_sccb_rate(CAMERA_SCCB_HZ);
    /* Let the board's reset/power-down sequence settle before SCCB probing. */
    hal_sleep_ms(20U);
}
void ov7740_probe_id(uint16_t *mid, uint16_t *pid)
{
    if(mid)
        *mid = ((uint16_t)ov7740_read_reg(0x1CU) << 8) |
               ov7740_read_reg(0x1DU);
    if(pid)
        *pid = ((uint16_t)ov7740_read_reg(0x0AU) << 8) |
               ov7740_read_reg(0x0BU);
}
void ov7740_apply_init_table(void)
{
    write_reg(0x12U, 0x80U);
    hal_sleep_ms(10U);
    for(size_t i = 0U; default_regs[i][0] != 0U; ++i)
        write_reg(default_regs[i][0], default_regs[i][1]);
    hal_sleep_ms(10U);
}
void ov7740_apply_common_tuning(void)
{
    /* Cube's camera is mounted inverted. Match the official Cube example's
     * hmirror/vflip and OV7740 horizontal start-point correction. */
    uint8_t value = ov7740_read_reg(0x0CU);
    write_reg(0x0CU, value | 0xC0U);
    value = ov7740_read_reg(0x16U);
    write_reg(0x16U, (value & 0xEEU) | 0x11U);
}
void ov7740_apply_output_size(uint16_t width, uint16_t height)
{
    if(width > 320U || height > 240U)
    {
        write_reg(0x31U, 0xA0U);
        write_reg(0x32U, 0xF0U);
        write_reg(0x82U, 0x32U);
    }
    else if(width > 160U || height > 120U)
    {
        write_reg(0x31U, 0x50U);
        write_reg(0x32U, 0x78U);
        write_reg(0x82U, 0x3FU);
    }
    else
    {
        write_reg(0x31U, 0x28U);
        write_reg(0x32U, 0x3CU);
        write_reg(0x82U, 0x3FU);
    }
}
void ov7740_apply_colorbar(uint8_t enabled)
{
    write_reg(0x38U, 0x07U);
    write_reg(0x84U, enabled ? 0x02U : 0x00U);
}
