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

#ifndef _TPD_CUSTOM_SYNAPTICS_H_
#define _TPD_CUSTOM_SYNAPTICS_H_
#include <pmic_drv.h>
#include <cust_i2c.h>

/* Pre-defined definition */
#define TPD_TYPE_CAPACITIVE
#define TPD_POWER_SOURCE_CUSTOM PMIC_APP_CAP_TOUCH_VDD	
//#define TPD_POWER_SOURCE_1800 MT6322_POWER_LDO_VGP3
#define TPD_I2C_BUS		0
#define TPD_I2C_ADDR		0x20
#define TPD_WAKEUP_TRIAL	60
#define TPD_WAKEUP_DELAY	100
#define CUST_EINT_POLARITY_LOW 0
#define TPD_DCLICK_WAKEUP

//#define TPD_HAVE_TREMBLE_ELIMINATION

/* Define the virtual button mapping */
#define TPD_HAVE_BUTTON
#define TPD_BUTTON_HEIGH        (100)
#define TPD_KEY_COUNT           3
#define TPD_KEYS                { KEY_MENU, KEY_HOMEPAGE,KEY_BACK}
#define TPD_KEYS_DIM            {{139,1376,120,100},{360,1376,120,100},{600,1376,120,100}}

/* Define the touch dimension */
#ifdef TPD_HAVE_BUTTON
#define TPD_TOUCH_HEIGH_RATIO	80
#define TPD_DISPLAY_HEIGH_RATIO	80
#endif

/* Define the 0D button mapping */
#ifdef TPD_HAVE_BUTTON
//#define TPD_0D_BUTTON		{}
#define TPD_0D_BUTTON		{KEY_MENU, KEY_HOMEPAGE, KEY_BACK}
#else
#define TPD_0D_BUTTON		{KEY_MENU, KEY_HOMEPAGE, KEY_BACK}
#endif

#endif /* TPD_CUSTOM_SYNAPTICS_H */

