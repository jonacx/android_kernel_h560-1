/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 */
/* MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

/*****************************************************************************
 *  Copyright Statement:
 *  --------------------
 *  This software is protected by Copyright and the information contained
 *  herein is confidential. The software may not be copied and the information
 *  contained herein may not be used or disclosed except with the written
 *  permission of MediaTek Inc. (C) 2008
 *
 *  BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 *  THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 *  RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO BUYER ON
 *  AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 *  NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 *  SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 *  SUPPLIED WITH THE MEDIATEK SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH
 *  THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO
 *  NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S
 *  SPECIFICATION OR TO CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
 *
 *  BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
 *  LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 *  AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 *  OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY BUYER TO
 *  MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 *  THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
 *  WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT OF
 *  LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING THEREOF AND
 *  RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN FRANCISCO, CA, UNDER
 *  THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE (ICC).
 *
 *****************************************************************************/

#ifndef BUILD_LK
#include <linux/string.h>
#endif
#include "lcm_drv.h"

#ifdef BUILD_LK
#include <platform/mt_gpio.h>
#include <platform/mt_pmic.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
#include <mach/mt_gpio.h>
#include <linux/xlog.h>
#include <mach/mt_pm_ldo.h>
#include <mach/upmu_common.h>
#endif

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH  (720)
#define FRAME_HEIGHT (1280)

#define LCM_ID_NT35592 (0x92)

#define REGFLAG_DELAY          		(0XFEE)
#define REGFLAG_END_OF_TABLE      	(0xFFFF)	// END OF REGISTERS MARKER

#ifndef FALSE
#define FALSE 0
#endif
#define GPIO_LCD_RST_EN      (GPIO28 | 0x80000000)
// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    (lcm_util.set_reset_pin((v)))

#define UDELAY(n) (lcm_util.udelay(n))
#define MDELAY(n) (lcm_util.mdelay(n))


// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------

#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)        (lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update))
#define dsi_set_cmdq(pdata, queue_size, force_update)		(lcm_util.dsi_set_cmdq(pdata, queue_size, force_update))
#define wrtie_cmd(cmd)						(lcm_util.dsi_write_cmd(cmd))
#define write_regs(addr, pdata, byte_nums)			(lcm_util.dsi_write_regs(addr, pdata, byte_nums))
#define read_reg						(lcm_util.dsi_read_reg())
#define read_reg_v2(cmd, buffer, buffer_size)   		(lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size))

static bool lcm_is_init = false;

#define DCT_H561 1

static void lcm_init_power(void)
{
#if (defined(DCT_H561))
#ifdef BUILD_LK
  SET_RESET_PIN(0);
  mt6325_upmu_set_rg_vgp1_en(1);
  MDELAY(50);
#else
  if(lcm_is_init == false)
  {
    printk("%s, begin\n", __func__);
    hwPowerOn(MT6325_POWER_LDO_VGP1, VOL_DEFAULT, "LCM_DRV");
    printk("%s, end\n", __func__);
    lcm_is_init = true;
  }
#endif
#endif
}

static void lcm_suspend_power(void)
{
#if (defined(DCT_H561))
#ifdef BUILD_LK
  mt6325_upmu_set_rg_vgp1_en(0);
#else
  lcm_init_power();
  printk("%s, begin\n", __func__);
  hwPowerDown(MT6325_POWER_LDO_VGP1, "LCM_DRV");
  printk("%s, end\n", __func__);
#endif
#endif

}

static void lcm_resume_power(void)
{
#if (defined(DCT_H561))
#ifdef BUILD_LK
  mt6325_upmu_set_rg_vgp1_en(1);
#else
  printk("%s, begin\n", __func__);
  hwPowerOn(MT6325_POWER_LDO_VGP1, VOL_DEFAULT, "LCM_DRV");
  printk("%s, end\n", __func__);
#endif
#endif

}



static struct LCM_setting_table {
    unsigned cmd;
    unsigned char count;
    unsigned char para_list[64];
};

static void lcm_suspend(void);

static struct LCM_setting_table lcm_initialization_setting[] = {
//NT35592+JDI4.99
//3power vsout control_modify_20140505
{0xFF,1,{0x05}},
{0xFB,1,{0x01}},
{0x7B,1,{0x10}},
{0xFF,1,{0x00}},
{REGFLAG_DELAY, 10, {}}, 
//CMD2 Page 0
{0xFF,1,{0x01}},
//VGSW=1(JDW)/NB=1/Res=720x1280
{0x00,1,{0x1A}},
//---2013.11.27 JD new-----
{0x02,1,{0x43}},
{0x04,1,{0x43}},
//VGL=2VCL-AVDD,VGH=2AVDD
{0x08,1,{0x56}},
//Turn off VGL clamp
{0x09,1,{0x00}},
{0x0B,1,{0xB3}},
{0x0C,1,{0xB3}},
{0x0D,1,{0x2F}},
//MUXW_JD modify_2013/11/27
{0x0E,1,{0x1B}},
//MUXG1_JD modify_2013/11/27
{0x0F,1,{0xA2}},
{0x11,1,{0x70}},	 //73 VCOM
{0x12,1,{0x03}},
//VGLO sleep in pull GND
{0x37,1,{0x02}},
//ID
{0x23,1,{0x00}},
{0x24,1,{0x00}},
{0x25,1,{0x00}},
{0x26,1,{0x00}},
{0x27,1,{0x00}},
{0x28,1,{0x00}},
//{0x36,1,{0x00}},
{0x36,1,{0x11}},
{0x6D,1,{0x55}},
{0x6F,1,{0x00}},
//CMD2 Page 4
{0xFF,1,{0x05}},
//RTN 11.27 JD modifty 0x8D=>0x82
{0x02,1,{0x82}},
{0x03,1,{0x82}},
{0x04,1,{0x82}},
//NEW
{0x01,1,{0x00}},
{0x05,1,{0x30}},
{0x06,1,{0x33}},
{0x07,1,{0x70}},
{0x08,1,{0x00}},
{0x11,1,{0x00}},
//FS_JD modify_2013/11/18
{0x09,1,{0x00}},
//FW_JD modify_2013/11/27
{0x0A,1,{0x01}},
//SWI_JD modify_2013/11/27
{0x0B,1,{0x70}},
//MUXS_JD modify_2013/11/27
{0x0D,1,{0x04}},
//MUXW_JD modify_2013/11/27
{0x0E,1,{0x1B}},
//MUXG1_JD modify_2013/11/27
{0x0F,1,{0x02}},
//MUXG2_JDI modify_2013/11/27
{0x10,1,{0x32}},
//PRECHW_JDI modify_2013/11/27
{0x12,1,{0x00}},
//STI_JDI modify_2013/11/27
{0x14,1,{0x00}},
//SDT_JDI modify_2013/12/06
{0x16,1,{0x04}},
//SOUTE
{0x19,1,{0xFF}},
{0x1A,1,{0xFF}},
{0x1B,1,{0xFF}},
//SOUTP
{0x1C,1,{0x80}},
{0x1D,1,{0x00}},
{0x1E,1,{0x00}},
{0x1F,1,{0x77}},
//SRGB setting=1~modify2013/12/06
{0x21,1,{0x20}},
//sub-pixel column inversion
{0x22,1,{0x55}},//55/11
{0x23,1,{0x0D}},//0D/09
//0x24~0x4A_power on/off sequence for JDW
{0x24,1,{0x03}},
{0x25,1,{0x53}},
{0x26,1,{0x55}},
{0x27,1,{0x35}},
//CKH power on/off sequence_JDI request_20131120
{0x28,1,{0x00}},
{0x29,1,{0x01}},
{0x2A,1,{0x11}},
// power on VCOM
{0x2D,1,{0x02}},
//CKH power on/off sequence_JDI request_20131120
{0x2F,1,{0x00}},
{0x30,1,{0x11}},
{0x31,1,{0x20}},
{0x32,1,{0x09}},
{0x35,1,{0x0D}},
//STV Rise/Fall
{0x36,1,{0x00}},
{0x37,1,{0x09}},
//NVT_command
{0x48,1,{0x40}},
{0x49,1,{0x23}},
{0x4A,1,{0xC1}},
//JDI NEW 11.27
//SW_OUT=1
//{0x6F,1,{0x21}},
{0x6F,1,{0x25}},
//Modify_2014/05/05
//{0x7B,1,{0x00}},
{0x7C,1,{0x00}},
//LTPS JDC 4 CKV mode
{0x7D,1,{0x03}},
//Dummy Line select
{0x83,1,{0x00}},
//NVT_command
{0x8D,1,{0x00}},
{0xA4,1,{0x8D}},
{0xA6,1,{0x00}},
//BP&FP_JDI modify_2013/11/27
{0xBB,1,{0x0A}},
{0xBC,1,{0x06}},
//CIC setngs_JDI modify_2013/11/27
{0xD7,1,{0x61}},
{0xD8,1,{0x13}},
//power on CIC_high
{0xF5,1,{0x00}},
{0xF6,1,{0x01}},
//2TO6 MUX EQ&SOG OFF
{0x89,1,{0xC1}},
//JDI NEW 11.27
{0x91,1,{0x91}},
{0xA2,1,{0x10}},
//RELOAD CMD2_P4
{0xFB,1,{0x01}},
//=====GAMMA_R=====
{0xFF,1,{0x01}},
{0x75,1,{0x00}},
{0x76,1,{0x81}},
{0x77,1,{0x00}},
{0x78,1,{0x8F}},
{0x79,1,{0x00}},
{0x7A,1,{0xA6}},
{0x7B,1,{0x00}},
{0x7C,1,{0xB9}},
{0x7D,1,{0x00}},
{0x7E,1,{0xC9}},
{0x7F,1,{0x00}},
{0x80,1,{0xD7}},
{0x81,1,{0x00}},
{0x82,1,{0xE5}},
{0x83,1,{0x00}},
{0x84,1,{0xF1}},
{0x85,1,{0x00}},
{0x86,1,{0xFC}},
{0x87,1,{0x01}},
{0x88,1,{0x22}},
{0x89,1,{0x01}},
{0x8A,1,{0x42}},
{0x8B,1,{0x01}},
{0x8C,1,{0x73}},
{0x8D,1,{0x01}},
{0x8E,1,{0x9C}},
{0x8F,1,{0x01}},
{0x90,1,{0xDB}},
{0x91,1,{0x02}},
{0x92,1,{0x11}},
{0x93,1,{0x02}},
{0x94,1,{0x13}},
{0x95,1,{0x02}},
{0x96,1,{0x43}},
{0x97,1,{0x02}},
{0x98,1,{0x78}},
{0x99,1,{0x02}},
{0x9A,1,{0x9F}},
{0x9B,1,{0x02}},
{0x9C,1,{0xD4}},
{0x9D,1,{0x02}},
{0x9E,1,{0xF6}},
{0x9F,1,{0x03}},
{0xA0,1,{0x25}},
{0xA2,1,{0x03}},
{0xA3,1,{0x32}},
{0xA4,1,{0x03}},
{0xA5,1,{0x44}},
{0xA6,1,{0x03}},
{0xA7,1,{0x56}},
{0xA9,1,{0x03}},
{0xAA,1,{0x68}},
{0xAB,1,{0x03}},
{0xAC,1,{0x86}},
{0xAD,1,{0x03}},
{0xAE,1,{0xA1}},
{0xAF,1,{0x03}},
{0xB0,1,{0xBB}},
{0xB1,1,{0x03}},
{0xB2,1,{0xD1}},
{0xB3,1,{0x00}},
{0xB4,1,{0x81}},
{0xB5,1,{0x00}},
{0xB6,1,{0x8F}},
{0xB7,1,{0x00}},
{0xB8,1,{0xA6}},
{0xB9,1,{0x00}},
{0xBA,1,{0xB9}},
{0xBB,1,{0x00}},
{0xBC,1,{0xC9}},
{0xBD,1,{0x00}},
{0xBE,1,{0xD7}},
{0xBF,1,{0x00}},
{0xC0,1,{0xE5}},
{0xC1,1,{0x00}},
{0xC2,1,{0xF1}},
{0xC3,1,{0x00}},
{0xC4,1,{0xFC}},
{0xC5,1,{0x01}},
{0xC6,1,{0x22}},
{0xC7,1,{0x01}},
{0xC8,1,{0x42}},
{0xC9,1,{0x01}},
{0xCA,1,{0x73}},
{0xCB,1,{0x01}},
{0xCC,1,{0x9C}},
{0xCD,1,{0x01}},
{0xCE,1,{0xDB}},
{0xCF,1,{0x02}},
{0xD0,1,{0x11}},
{0xD1,1,{0x02}},
{0xD2,1,{0x13}},
{0xD3,1,{0x02}},
{0xD4,1,{0x43}},
{0xD5,1,{0x02}},
{0xD6,1,{0x78}},
{0xD7,1,{0x02}},
{0xD8,1,{0x9F}},
{0xD9,1,{0x02}},
{0xDA,1,{0xD4}},
{0xDB,1,{0x02}},
{0xDC,1,{0xF6}},
{0xDD,1,{0x03}},
{0xDE,1,{0x25}},
{0xDF,1,{0x03}},
{0xE0,1,{0x32}},
{0xE1,1,{0x03}},
{0xE2,1,{0x44}},
{0xE3,1,{0x03}},
{0xE4,1,{0x56}},
{0xE5,1,{0x03}},
{0xE6,1,{0x68}},
{0xE7,1,{0x03}},
{0xE8,1,{0x86}},
{0xE9,1,{0x03}},
{0xEA,1,{0xA1}},
{0xEB,1,{0x03}},
{0xEC,1,{0xBB}},
{0xED,1,{0x03}},
{0xEE,1,{0xD1}},
//=====GAMMA_G=====  
{0xEF,1,{0x00}},
{0xF0,1,{0x81}},
{0xF1,1,{0x00}},
{0xF2,1,{0x8F}},
{0xF3,1,{0x00}},
{0xF4,1,{0xA6}},
{0xF5,1,{0x00}},
{0xF6,1,{0xB9}},
{0xF7,1,{0x00}},
{0xF8,1,{0xC9}},
{0xF9,1,{0x00}},
{0xFA,1,{0xD7}},
{0xFF,1,{0x02}},
{0x00,1,{0x00}},
{0x01,1,{0xE5}},
{0x02,1,{0x00}},
{0x03,1,{0xF1}},
{0x04,1,{0x00}},
{0x05,1,{0xFC}},
{0x06,1,{0x01}},
{0x07,1,{0x22}},
{0x08,1,{0x01}},
{0x09,1,{0x42}},
{0x0A,1,{0x01}},
{0x0B,1,{0x73}},
{0x0C,1,{0x01}},
{0x0D,1,{0x9C}},
{0x0E,1,{0x01}},
{0x0F,1,{0xDB}},
{0x10,1,{0x02}},
{0x11,1,{0x11}},
{0x12,1,{0x02}},
{0x13,1,{0x13}},
{0x14,1,{0x02}},
{0x15,1,{0x43}},
{0x16,1,{0x02}},
{0x17,1,{0x78}},
{0x18,1,{0x02}},
{0x19,1,{0x9F}},
{0x1A,1,{0x02}},
{0x1B,1,{0xD4}},
{0x1C,1,{0x02}},
{0x1D,1,{0xF6}},
{0x1E,1,{0x03}},
{0x1F,1,{0x25}},
{0x20,1,{0x03}},
{0x21,1,{0x32}},
{0x22,1,{0x03}},
{0x23,1,{0x44}},
{0x24,1,{0x03}},
{0x25,1,{0x56}},
{0x26,1,{0x03}},
{0x27,1,{0x68}},
{0x28,1,{0x03}},
{0x29,1,{0x86}},
{0x2A,1,{0x03}},
{0x2B,1,{0xA1}},
{0x2D,1,{0x03}},
{0x2F,1,{0xBB}},
{0x30,1,{0x03}},
{0x31,1,{0xD1}},
{0x32,1,{0x00}},
{0x33,1,{0x81}},
{0x34,1,{0x00}},
{0x35,1,{0x8F}},
{0x36,1,{0x00}},
{0x37,1,{0xA6}},
{0x38,1,{0x00}},
{0x39,1,{0xB9}},
{0x3A,1,{0x00}},
{0x3B,1,{0xC9}},
{0x3D,1,{0x00}},
{0x3F,1,{0xD7}},
{0x40,1,{0x00}},
{0x41,1,{0xE5}},
{0x42,1,{0x00}},
{0x43,1,{0xF1}},
{0x44,1,{0x00}},
{0x45,1,{0xFC}},
{0x46,1,{0x01}},
{0x47,1,{0x22}},
{0x48,1,{0x01}},
{0x49,1,{0x42}},
{0x4A,1,{0x01}},
{0x4B,1,{0x73}},
{0x4C,1,{0x01}},
{0x4D,1,{0x9C}},
{0x4E,1,{0x01}},
{0x4F,1,{0xDB}},
{0x50,1,{0x02}},
{0x51,1,{0x11}},
{0x52,1,{0x02}},
{0x53,1,{0x13}},
{0x54,1,{0x02}},
{0x55,1,{0x43}},
{0x56,1,{0x02}},
{0x58,1,{0x78}},
{0x59,1,{0x02}},
{0x5A,1,{0x9F}},
{0x5B,1,{0x02}},
{0x5C,1,{0xD4}},
{0x5D,1,{0x02}},
{0x5E,1,{0xF6}},
{0x5F,1,{0x03}},
{0x60,1,{0x25}},
{0x61,1,{0x03}},
{0x62,1,{0x32}},
{0x63,1,{0x03}},
{0x64,1,{0x44}},
{0x65,1,{0x03}},
{0x66,1,{0x56}},
{0x67,1,{0x03}},
{0x68,1,{0x68}},
{0x69,1,{0x03}},
{0x6A,1,{0x86}},
{0x6B,1,{0x03}},
{0x6C,1,{0xA1}},
{0x6D,1,{0x03}},
{0x6E,1,{0xBB}},
{0x6F,1,{0x03}},
{0x70,1,{0xD1}},
//=====GAMMA_B=====  
{0x71,1,{0x00}},
{0x72,1,{0x81}},
{0x73,1,{0x00}},
{0x74,1,{0x8F}},
{0x75,1,{0x00}},
{0x76,1,{0xA6}},
{0x77,1,{0x00}},
{0x78,1,{0xB9}},
{0x79,1,{0x00}},
{0x7A,1,{0xC9}},
{0x7B,1,{0x00}},
{0x7C,1,{0xD7}},
{0x7D,1,{0x00}},
{0x7E,1,{0xE5}},
{0x7F,1,{0x00}},
{0x80,1,{0xF1}},
{0x81,1,{0x00}},
{0x82,1,{0xFC}},
{0x83,1,{0x01}},
{0x84,1,{0x22}},
{0x85,1,{0x01}},
{0x86,1,{0x42}},
{0x87,1,{0x01}},
{0x88,1,{0x73}},
{0x89,1,{0x01}},
{0x8A,1,{0x9C}},
{0x8B,1,{0x01}},
{0x8C,1,{0xDB}},
{0x8D,1,{0x02}},
{0x8E,1,{0x11}},
{0x8F,1,{0x02}},
{0x90,1,{0x13}},
{0x91,1,{0x02}},
{0x92,1,{0x43}},
{0x93,1,{0x02}},
{0x94,1,{0x78}},
{0x95,1,{0x02}},
{0x96,1,{0x9F}},
{0x97,1,{0x02}},
{0x98,1,{0xD4}},
{0x99,1,{0x02}},
{0x9A,1,{0xF6}},
{0x9B,1,{0x03}},
{0x9C,1,{0x25}},
{0x9D,1,{0x03}},
{0x9E,1,{0x32}},
{0x9F,1,{0x03}},
{0xA0,1,{0x44}},
{0xA2,1,{0x03}},
{0xA3,1,{0x56}},
{0xA4,1,{0x03}},
{0xA5,1,{0x68}},
{0xA6,1,{0x03}},
{0xA7,1,{0x86}},
{0xA9,1,{0x03}},
{0xAA,1,{0xA1}},
{0xAB,1,{0x03}},
{0xAC,1,{0xBB}},
{0xAD,1,{0x03}},
{0xAE,1,{0xD1}},
{0xAF,1,{0x00}},
{0xB0,1,{0x81}},
{0xB1,1,{0x00}},
{0xB2,1,{0x8F}},
{0xB3,1,{0x00}},
{0xB4,1,{0xA6}},
{0xB5,1,{0x00}},
{0xB6,1,{0xB9}},
{0xB7,1,{0x00}},
{0xB8,1,{0xC9}},
{0xB9,1,{0x00}},
{0xBA,1,{0xD7}},
{0xBB,1,{0x00}},
{0xBC,1,{0xE5}},
{0xBD,1,{0x00}},
{0xBE,1,{0xF1}},
{0xBF,1,{0x00}},
{0xC0,1,{0xFC}},
{0xC1,1,{0x01}},
{0xC2,1,{0x22}},
{0xC3,1,{0x01}},
{0xC4,1,{0x42}},
{0xC5,1,{0x01}},
{0xC6,1,{0x73}},
{0xC7,1,{0x01}},
{0xC8,1,{0x9C}},
{0xCA,1,{0xDB}},
{0xCB,1,{0x02}},
{0xCC,1,{0x11}},
{0xCD,1,{0x02}},
{0xCE,1,{0x13}},
{0xCF,1,{0x02}},
{0xD0,1,{0x43}},
{0xD1,1,{0x02}},
{0xD2,1,{0x78}},
{0xD3,1,{0x02}},
{0xD4,1,{0x9F}},
{0xD5,1,{0x02}},
{0xD6,1,{0xD4}},
{0xD7,1,{0x02}},
{0xD8,1,{0xF6}},
{0xD9,1,{0x03}},
{0xDA,1,{0x25}},
{0xDB,1,{0x03}},
{0xDC,1,{0x32}},
{0xDD,1,{0x03}},
{0xDE,1,{0x44}},
{0xDF,1,{0x03}},
{0xE0,1,{0x56}},
{0xE1,1,{0x03}},
{0xE2,1,{0x68}},
{0xE3,1,{0x03}},
{0xE4,1,{0x86}},
{0xE5,1,{0x03}},
{0xE6,1,{0xA1}},
{0xE7,1,{0x03}},
{0xE8,1,{0xBB}},
{0xE9,1,{0x03}},
{0xEA,1,{0xD1}},
//=============================
{0xFB,1,{0x01}},
{0xFF,1,{0x00}},
{0x35,1,{0x00}},                                     
{0x11,1,{0x00}}, //Tearing Effect On        Sleep Out
{REGFLAG_DELAY, 120, {}},  
{0x51,1,{0xFF}},
{0x53,1,{0x2C}},
{0x55,1,{0x00}},
{0x29,1,{0x00}}, //Display On   Display On.          
{REGFLAG_DELAY, 40, {}},                             
{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_sleep_out_setting[] = {
    // Sleep Out
    {0x11, 0, {0x00}},
    {REGFLAG_DELAY, 120, {}},

    // Display ON
    {0x29, 0, {0x00}},
    {REGFLAG_DELAY, 50, {}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_sleep_in_setting[] = {
    // Display off sequence
    {0x28, 0, {0x00}},
    {REGFLAG_DELAY, 50, {}},
    // Sleep Mode On
    {0x10, 0, {0x00}},
    {REGFLAG_DELAY, 100, {}},

    {REGFLAG_END_OF_TABLE, 0x00, {}}
};
static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
    unsigned int i;

    for(i = 0; i < count; i++)
    {

        unsigned cmd;
        cmd = table[i].cmd;

        switch (cmd)
        {

            case REGFLAG_DELAY :
                MDELAY(table[i].count);
                break;

            case REGFLAG_END_OF_TABLE :
                break;

            default:
				
				dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
        }
    }
}
// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{

    memset(params, 0, sizeof(LCM_PARAMS));

    params->type   = LCM_TYPE_DSI;

    params->width  = FRAME_WIDTH;
    params->height = FRAME_HEIGHT;

    // enable tearing-free
    params->dbi.te_mode                 = LCM_DBI_TE_MODE_DISABLED; //LCM_DBI_TE_MODE_VSYNC_ONLY;
    params->dbi.te_edge_polarity        = LCM_POLARITY_RISING;

#if (LCM_DSI_CMD_MODE)
    params->dsi.mode   = CMD_MODE;
#else
    params->dsi.mode   = BURST_VDO_MODE;
#endif

    // DSI
    /* Command mode setting */
    //1 Three lane or Four lane
    params->dsi.LANE_NUM                = LCM_FOUR_LANE;
    //The following defined the fomat for data coming from LCD engine.
    params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
    params->dsi.data_format.trans_seq   = LCM_DSI_TRANS_SEQ_MSB_FIRST;
    params->dsi.data_format.padding     = LCM_DSI_PADDING_ON_LSB;
    params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;

    // Highly depends on LCD driver capability.
    // Not support in MT6573
    params->dsi.packet_size=256;

    // Video mode setting       
    params->dsi.intermediat_buffer_num = 0;//because DSI/DPI HW design change, this parameters should be 0 when video mode in MT658X; or memory leakage

    params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;
    params->dsi.word_count=720*3;

	params->dsi.vertical_sync_active = 2; //1
	params->dsi.vertical_backporch = 8;   //1
	params->dsi.vertical_frontporch = 8;  //6
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 10;
	params->dsi.horizontal_backporch = 118;
	params->dsi.horizontal_frontporch = 118;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;

	//params->dsi.LPX=8;

	// Bit rate calculation
	//1 Every lane speed
    params->dsi.PLL_CLOCK = 240;
}

static void lcm_init(void)
{
	mt_set_gpio_mode(GPIO_LCD_RST_EN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_LCD_RST_EN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_LCD_RST_EN, GPIO_OUT_ONE);
	MDELAY(10);
	mt_set_gpio_out(GPIO_LCD_RST_EN, GPIO_OUT_ZERO);
	MDELAY(50);
	mt_set_gpio_out(GPIO_LCD_RST_EN, GPIO_OUT_ONE);
    MDELAY(50);//Must over 6 ms
	push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_suspend(void)
{
    unsigned int data_array[16];

    data_array[0] = 0x00280500;	// Display Off
    dsi_set_cmdq(&data_array, 1, 1);
    MDELAY(120);

    data_array[0] = 0x00100500;	// Sleep In
    dsi_set_cmdq(&data_array, 1, 1);
    MDELAY(200);
//	mt_set_gpio_out(GPIO_LCD_RST_EN, GPIO_OUT_ZERO);
}

static void lcm_resume(void)
{
#ifndef BUILD_LK
    lcm_init();
#endif 
}

static unsigned int lcm_compare_id(void)
{
    unsigned int id;
    unsigned char buffer[5];
    unsigned int array[5];

    MDELAY(100);
	mt_set_gpio_mode(GPIO_LCD_RST_EN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_LCD_RST_EN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_LCD_RST_EN, GPIO_OUT_ONE);
	MDELAY(10);
	mt_set_gpio_out(GPIO_LCD_RST_EN, GPIO_OUT_ZERO);
	MDELAY(50);
	mt_set_gpio_out(GPIO_LCD_RST_EN, GPIO_OUT_ONE);
    MDELAY(50);//Must over 6 ms

    // Set Maximum return byte = 1
    array[0] = 0x00053700;
    dsi_set_cmdq(array, 1, 1);

    read_reg_v2(0xF4, buffer, 2);
    id = buffer[0]; //we only need ID
#ifdef BUILD_LK
    printf("%s, LK nt35592 lk: nt35592 id = 0x%08x\n", __func__, id);
#else
    printk("%s, kernel nt35592 kernel: nt35592 id = 0x%08x\n", __func__, id);
#endif

    if (id == LCM_ID_NT35592)
        return 1;
    else
        return 0;
}
//
// ---------------------------------------------------------------------------
//  Get LCM Driver Hooks
// ---------------------------------------------------------------------------
LCM_DRIVER nt35592_jdi50_xyl_hd_lcm_drv =
{
    .name			= "nt35592_jdi50_xyl_hd",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params     = lcm_get_params,
    .init           = lcm_init,
    .suspend        = lcm_suspend,
    .resume         = lcm_resume,
    .compare_id     = lcm_compare_id,
#if (LCM_DSI_CMD_MODE)
    .update         = lcm_update,
#endif
    .init_power        = lcm_init_power,
    .resume_power = lcm_resume_power,
    .suspend_power = lcm_suspend_power,

};
