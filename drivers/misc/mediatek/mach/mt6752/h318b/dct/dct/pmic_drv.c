/*
 * Generated by MTK SP DrvGen Version 03.13.6 for MT6752. Copyright MediaTek Inc. (C) 2013.
 * Fri Nov 20 16:15:00 2015
 * Do Not Modify the File.
 */



#include <linux/types.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_pm_ldo.h>
void pmu_drv_tool_customization_init(void)
{
    pmic_ldo_enable(MT6325_POWER_LDO_VGP2,KAL_TRUE);
}

