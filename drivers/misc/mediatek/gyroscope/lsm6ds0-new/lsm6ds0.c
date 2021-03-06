/* LSM6DS0 motion sensor driver
 *
 *
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>

#include <cust_gyro.h>
#include <linux/hwmsensor.h>
#include <linux/hwmsen_dev.h>
#include <linux/sensors_io.h>
#include "lsm6ds0.h"
#include <linux/hwmsen_helper.h>
#include <linux/kernel.h>
#include <linux/batch.h>

#include <mach/mt_typedefs.h>
#include <mach/mt_gpio.h>
#include <mach/mt_pm_ldo.h>
#include <mach/mt_boot.h>

#include <gyroscope.h>

#define INV_GYRO_AUTO_CALI  1

#define POWER_NONE_MACRO MT65XX_POWER_NONE

/*----------------------------------------------------------------------------*/
#define I2C_DRIVERID_LSM6DS0	3000
/*----------------------------------------------------------------------------*/
//#define LSM6DS0_DEFAULT_FS		LSM6DS0_FS_1000
//#define LSM6DS0_DEFAULT_LSB		LSM6DS0_FS_250_LSB
/*---------------------------------------------------------------------------*/
#define DEBUG 1
/*----------------------------------------------------------------------------*/
#define CONFIG_LSM6DS0_LOWPASS   /*apply low pass filter on output*/       
/*----------------------------------------------------------------------------*/
#define LSM6DS0_AXIS_X          0
#define LSM6DS0_AXIS_Y          1
#define LSM6DS0_AXIS_Z          2
#define LSM6DS0_AXES_NUM        3
#define LSM6DS0_DATA_LEN        6   
#define LSM6DS0_DEV_NAME        "lsm6ds0"
/*----------------------------------------------------------------------------*/
static const struct i2c_device_id LSM6DS0_i2c_id[] = {{LSM6DS0_DEV_NAME,0},{}};
static struct i2c_board_info __initdata i2c_LSM6DS0={ I2C_BOARD_INFO(LSM6DS0_DEV_NAME, (LSM6DS0_I2C_SLAVE_ADDR>>1))};
/*the adapter id will be available in customization*/
//static unsigned short LSM6DS0_force[] = {0x00, LSM6DS0_I2C_SLAVE_ADDR, I2C_CLIENT_END, I2C_CLIENT_END};
//static const unsigned short *const LSM6DS0_forces[] = { LSM6DS0_force, NULL };
//static struct i2c_client_address_data LSM6DS0_addr_data = { .forces = LSM6DS0_forces,};

int packet_thresh = 75; // 600 ms / 8ms/sample

/*----------------------------------------------------------------------------*/
static int LSM6DS0_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id); 
static int LSM6DS0_i2c_remove(struct i2c_client *client);
static int LSM6DS0_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info);
static int LSM6DS0_init_client(struct i2c_client *client, bool enable);
static int LSM6DS0_read_byte_sr(struct i2c_client *client, u8 addr, u8 *data, u8 len);

static int LSM6DS0_local_init(void);
static int  LSM6DS0_remove(void);
static int LSM6DS0_init_flag =-1; // 0<==>OK -1 <==> fail
static struct gyro_init_info LSM6DS0_init_info = {
        .name = "LSM6DS0GY",
        .init = LSM6DS0_local_init,
        .uninit =LSM6DS0_remove,
};


/*----------------------------------------------------------------------------*/
typedef enum {
    GYRO_TRC_FILTER  = 0x01,
    GYRO_TRC_RAWDATA = 0x02,
    GYRO_TRC_IOCTL   = 0x04,
    GYRO_TRC_CALI	= 0X08,
    GYRO_TRC_INFO	= 0X10,
    GYRO_TRC_DATA	= 0X20,
} GYRO_TRC;
/*----------------------------------------------------------------------------*/
struct scale_factor{
    u8  whole;
    u8  fraction;
};
/*----------------------------------------------------------------------------*/
struct data_resolution {
    struct scale_factor scalefactor;
    int                 sensitivity;
};
/*----------------------------------------------------------------------------*/
#define C_MAX_FIR_LENGTH (32)
/*----------------------------------------------------------------------------*/
struct data_filter {
    s16 raw[C_MAX_FIR_LENGTH][LSM6DS0_AXES_NUM];
    int sum[LSM6DS0_AXES_NUM];
    int num;
    int idx;
};
/*----------------------------------------------------------------------------*/
struct LSM6DS0_i2c_data {
    struct i2c_client *client;
    struct gyro_hw *hw;
    struct hwmsen_convert   cvt;
    
    /*misc*/
    struct data_resolution *reso;
    atomic_t                trace;
    atomic_t                suspend;
    atomic_t                selftest;
    atomic_t				filter;
    s16                     cali_sw[LSM6DS0_AXES_NUM+1];

    /*data*/
    s8                      offset[LSM6DS0_AXES_NUM+1];  /*+1: for 4-byte alignment*/
    s16                     data[LSM6DS0_AXES_NUM+1];

#if defined(CONFIG_LSM6DS0_LOWPASS)
    atomic_t                firlen;
    atomic_t                fir_en;
    struct data_filter      fir;
#endif 
    /*early suspend*/
#if defined(CONFIG_HAS_EARLYSUSPEND)
    struct early_suspend    early_drv;
#endif     
};
/*----------------------------------------------------------------------------*/
static struct i2c_driver LSM6DS0_i2c_driver = {
    .driver = {
//      .owner          = THIS_MODULE,
        .name           = LSM6DS0_DEV_NAME,
    },
	.probe      		= LSM6DS0_i2c_probe,
	.remove    			= LSM6DS0_i2c_remove,
	.detect				= LSM6DS0_i2c_detect,
#if !defined(CONFIG_HAS_EARLYSUSPEND)    
    .suspend            = LSM6DS0_suspend,
    .resume             = LSM6DS0_resume,
#endif
	.id_table = LSM6DS0_i2c_id,
//	.address_data = &LSM6DS0_addr_data,
};

/*----------------------------------------------------------------------------*/
static struct i2c_client *LSM6DS0_i2c_client = NULL;
static struct LSM6DS0_i2c_data *obj_i2c_data = NULL;
static bool sensor_power = false;



/*----------------------------------------------------------------------------*/
#define GYRO_TAG                  "[Gyroscope] "
//#define GYRO_FUN(f)               printk(KERN_INFO GYRO_TAG"%s\n", __FUNCTION__)
//#define GYRO_ERR(fmt, args...)    printk(KERN_ERR GYRO_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)

//#define GYRO_LOG(fmt, args...)    printk(KERN_INFO GYRO_TAG fmt, ##args)

#define GYRO_FUN(f)               printk(GYRO_TAG"%s\n", __FUNCTION__)
#define GYRO_ERR(fmt, args...)    printk(KERN_ERR GYRO_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
#define GYRO_LOG(fmt, args...)    printk(GYRO_TAG fmt, ##args)

/*----------------------------------------------------------------------------*/

static void LSM6DS0_dumpReg(struct i2c_client *client)
{
  int i=0;
  u8 addr = 0x20;
  u8 regdata=0;
  for(i=0; i<25 ; i++)
  {
    //dump all
    LSM6DS0_read_byte_sr(client,addr,&regdata,1);
	HWM_LOG("Reg addr=%x regdata=%x\n",addr,regdata);
	addr++;
	
  }

}


/*--------------------gyroscopy power control function----------------------------------*/
static void LSM6DS0_power(struct gyro_hw *hw, unsigned int on) 
{
	static unsigned int power_on = 0;

	if(hw->power_id != POWER_NONE_MACRO)		// have externel LDO
	{        
		GYRO_LOG("power %s\n", on ? "on" : "off");
		if(power_on == on)	// power status not change
		{
			GYRO_LOG("ignore power control: %d\n", on);
		}
		else if(on)	// power on
		{
			if(!hwPowerOn(hw->power_id, hw->power_vol, "LSM6DS0"))
			{
				GYRO_ERR("power on fails!!\n");
			}
		}
		else	// power off
		{
			if (!hwPowerDown(hw->power_id, "LSM6DS0"))
			{
				GYRO_ERR("power off fail!!\n");
			}			  
		}
	}
	power_on = on;    
}
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
static int LSM6DS0_write_rel_calibration(struct LSM6DS0_i2c_data *obj, int dat[LSM6DS0_AXES_NUM])
{
    obj->cali_sw[LSM6DS0_AXIS_X] = obj->cvt.sign[LSM6DS0_AXIS_X]*dat[obj->cvt.map[LSM6DS0_AXIS_X]];
    obj->cali_sw[LSM6DS0_AXIS_Y] = obj->cvt.sign[LSM6DS0_AXIS_Y]*dat[obj->cvt.map[LSM6DS0_AXIS_Y]];
    obj->cali_sw[LSM6DS0_AXIS_Z] = obj->cvt.sign[LSM6DS0_AXIS_Z]*dat[obj->cvt.map[LSM6DS0_AXIS_Z]];
#if DEBUG		
		if(atomic_read(&obj->trace) & GYRO_TRC_CALI)
		{
			GYRO_LOG("test  (%5d, %5d, %5d) ->(%5d, %5d, %5d)->(%5d, %5d, %5d))\n", 
				obj->cvt.sign[LSM6DS0_AXIS_X],obj->cvt.sign[LSM6DS0_AXIS_Y],obj->cvt.sign[LSM6DS0_AXIS_Z],
				dat[LSM6DS0_AXIS_X], dat[LSM6DS0_AXIS_Y], dat[LSM6DS0_AXIS_Z],
				obj->cvt.map[LSM6DS0_AXIS_X],obj->cvt.map[LSM6DS0_AXIS_Y],obj->cvt.map[LSM6DS0_AXIS_Z]);
			GYRO_LOG("write gyro calibration data  (%5d, %5d, %5d)\n", 
				obj->cali_sw[LSM6DS0_AXIS_X],obj->cali_sw[LSM6DS0_AXIS_Y],obj->cali_sw[LSM6DS0_AXIS_Z]);
		}
#endif
    return 0;
}


/*----------------------------------------------------------------------------*/
static int LSM6DS0_ResetCalibration(struct i2c_client *client)
{
	struct LSM6DS0_i2c_data *obj = i2c_get_clientdata(client);	

	memset(obj->cali_sw, 0x00, sizeof(obj->cali_sw));
	return 0;    
}
/*----------------------------------------------------------------------------*/
static int LSM6DS0_ReadCalibration(struct i2c_client *client, int dat[LSM6DS0_AXES_NUM])
{
    struct LSM6DS0_i2c_data *obj = i2c_get_clientdata(client);

    dat[obj->cvt.map[LSM6DS0_AXIS_X]] = obj->cvt.sign[LSM6DS0_AXIS_X]*obj->cali_sw[LSM6DS0_AXIS_X];
    dat[obj->cvt.map[LSM6DS0_AXIS_Y]] = obj->cvt.sign[LSM6DS0_AXIS_Y]*obj->cali_sw[LSM6DS0_AXIS_Y];
    dat[obj->cvt.map[LSM6DS0_AXIS_Z]] = obj->cvt.sign[LSM6DS0_AXIS_Z]*obj->cali_sw[LSM6DS0_AXIS_Z];

#if DEBUG		
		if(atomic_read(&obj->trace) & GYRO_TRC_CALI)
		{
			GYRO_LOG("Read gyro calibration data  (%5d, %5d, %5d)\n", 
				dat[LSM6DS0_AXIS_X],dat[LSM6DS0_AXIS_Y],dat[LSM6DS0_AXIS_Z]);
		}
#endif
          GYRO_LOG("Read gyro calibration data  (%5d, %5d, %5d)\n", 
				dat[LSM6DS0_AXIS_X],dat[LSM6DS0_AXIS_Y],dat[LSM6DS0_AXIS_Z]);                             
    return 0;
}
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
static int LSM6DS0_WriteCalibration(struct i2c_client *client, int dat[LSM6DS0_AXES_NUM])
{
	struct LSM6DS0_i2c_data *obj = i2c_get_clientdata(client);
	int err = 0;
	int cali[LSM6DS0_AXES_NUM];


	GYRO_FUN();
	if(!obj || ! dat)
	{
		GYRO_ERR("null ptr!!\n");
		return -EINVAL;
	}
	else
	{        		
		cali[obj->cvt.map[LSM6DS0_AXIS_X]] = obj->cvt.sign[LSM6DS0_AXIS_X]*obj->cali_sw[LSM6DS0_AXIS_X];
		cali[obj->cvt.map[LSM6DS0_AXIS_Y]] = obj->cvt.sign[LSM6DS0_AXIS_Y]*obj->cali_sw[LSM6DS0_AXIS_Y];
		cali[obj->cvt.map[LSM6DS0_AXIS_Z]] = obj->cvt.sign[LSM6DS0_AXIS_Z]*obj->cali_sw[LSM6DS0_AXIS_Z]; 
		cali[LSM6DS0_AXIS_X] += dat[LSM6DS0_AXIS_X];
		cali[LSM6DS0_AXIS_Y] += dat[LSM6DS0_AXIS_Y];
		cali[LSM6DS0_AXIS_Z] += dat[LSM6DS0_AXIS_Z];
#if DEBUG		
		if(atomic_read(&obj->trace) & GYRO_TRC_CALI)
		{
			GYRO_LOG("write gyro calibration data  (%5d, %5d, %5d)-->(%5d, %5d, %5d)\n", 
				dat[LSM6DS0_AXIS_X], dat[LSM6DS0_AXIS_Y], dat[LSM6DS0_AXIS_Z],
				cali[LSM6DS0_AXIS_X],cali[LSM6DS0_AXIS_Y],cali[LSM6DS0_AXIS_Z]);
		}
#endif
		return LSM6DS0_write_rel_calibration(obj, cali);
	} 

	return err;
}
/*----------------------------------------------------------------------------*/
static int LSM6DS0_CheckDeviceID(struct i2c_client *client)
{
	u8 databuf[10];    
	int res = 0;

	memset(databuf, 0, sizeof(u8)*10);    
	databuf[0] = LSM6DS0_FIXED_DEVID;    

	res = LSM6DS0_read_byte_sr(client,LSM6DS0_REG_DEVID,databuf,1);
    GYRO_LOG(" LSM6DS0  id %x!\n",databuf[0]);
	if(databuf[0]!=LSM6DS0_FIXED_DEVID)
	{
		return LSM6DS0_ERR_IDENTIFICATION;
	}

	//exit_MMA8453Q_CheckDeviceID:
	if (res < 0)
	{
		return LSM6DS0_ERR_I2C;
	}
	
	return LSM6DS0_SUCCESS;
}


//----------------------------------------------------------------------------//
static int LSM6DS0_SetPowerMode(struct i2c_client *client, bool enable)
{
	u8 databuf[2] = {0};    
	int res = 0;

	if(enable == sensor_power)
	{
		GYRO_LOG("Sensor power status is newest!\n");
		return LSM6DS0_SUCCESS;
	}

	if(LSM6DS0_read_byte_sr(client, LSM6DS0_CTL_REG1, databuf,1))
	{
		GYRO_ERR("read power ctl register err!\n");
		return LSM6DS0_ERR_I2C;
	}

	databuf[0] &= ~LSM6DS0_POWER_ON;//clear power on bit
	if(true == enable )
	{
		databuf[0] |= LSM6DS0_POWER_ON;
	}
	else
	{
		// do nothing
	}
	databuf[1] = databuf[0];
	databuf[0] = LSM6DS0_CTL_REG1;    
	res = i2c_master_send(client, databuf, 0x2);
	if(res <= 0)
	{
		GYRO_LOG("set power mode failed!\n");
		return LSM6DS0_ERR_I2C;
	}	
	else
	{
		GYRO_LOG("set power mode ok %d!\n", enable);
	}

	sensor_power = enable;
	
	return LSM6DS0_SUCCESS;    
}

/*----------------------------------------------------------------------------*/


static int LSM6DS0_SetDataResolution(struct i2c_client *client, u8 dataResolution)
{
	u8 databuf[2] = {0};    
	int res = 0;
	GYRO_FUN();     
	
	if(LSM6DS0_read_byte_sr(client, LSM6DS0_CTL_REG1, databuf,1))
	{
		GYRO_ERR("read LSM6DS0_CTL_REG4 err!\n");
		return LSM6DS0_ERR_I2C;
	}
	else
	{
		GYRO_LOG("read  LSM6DS0_CTL_REG4 register: 0x%x\n", databuf[0]);
	}

	databuf[0] &= 0xE7;//clear 
	databuf[0] |= dataResolution;
	
	databuf[1] = databuf[0];
	databuf[0] = LSM6DS0_CTL_REG1; 
	
	
	res = i2c_master_send(client, databuf, 0x2);
	if(res <= 0)
	{
		GYRO_ERR("write SetDataResolution register err!\n");
		return LSM6DS0_ERR_I2C;
	}
	return LSM6DS0_SUCCESS;    
}

// set the sample rate
static int LSM6DS0_SetSampleRate(struct i2c_client *client, u8 sample_rate)
{
	u8 databuf[2] = {0}; 
	int res = 0;
	GYRO_FUN();    

	if(LSM6DS0_read_byte_sr(client, LSM6DS0_CTL_REG1, databuf,1))
	{
		GYRO_ERR("read gyro data format register err!\n");
		return LSM6DS0_ERR_I2C;
	}
	else
	{
		GYRO_LOG("read  gyro data format register: 0x%x\n", databuf[0]);
	}

	databuf[0] &= 0x3f;//clear 
	databuf[0] |= sample_rate;
	
	databuf[1] = databuf[0];
	databuf[0] = LSM6DS0_CTL_REG1; 
	
	
	res = i2c_master_send(client, databuf, 0x2);
	if(res <= 0)
	{
		GYRO_ERR("write sample rate register err!\n");
		return LSM6DS0_ERR_I2C;
	}
	
	return LSM6DS0_SUCCESS;    
}
/*----------------------------------------------------------------------------*/

static int LSM6DS0_read_byte_sr(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
    int ret = 0;
	unsigned short length = 0;
	
    client->addr = (client->addr & I2C_MASK_FLAG) | I2C_WR_FLAG |I2C_RS_FLAG;
    data[0] = addr;
	length = (len << 8) | 1;
	
	ret = i2c_master_send(client, (const char*)data, length);
    if (ret < 0) {
        GYRO_ERR("send command error!!\n");
        return -EFAULT;
    }
	
	client->addr &= I2C_MASK_FLAG;
	
    return 0;
}

/*----------------------------------------------------------------------------*/
static int LSM6DS0_ReadGyroData(struct i2c_client *client, char *buf, int bufsize)
{
	char databuf[6]= {0};	
	int data[3];
	struct LSM6DS0_i2c_data *obj = i2c_get_clientdata(client);  
	
	if(sensor_power == false)
	{
		LSM6DS0_SetPowerMode(client, true);
	}

	if(LSM6DS0_read_byte_sr(client, LSM6DS0_REG_GYRO_XL, databuf, 0x06))
	{
		GYRO_ERR("LSM6DS0 read gyroscope data  error\n");
		return LSM6DS0_ERR_GETGSENSORDATA;
	}
	else
	{
		obj->data[LSM6DS0_AXIS_X] = (s16)((databuf[LSM6DS0_AXIS_X*2+1] << 8) | (databuf[LSM6DS0_AXIS_X*2]));
		obj->data[LSM6DS0_AXIS_Y] = (s16)((databuf[LSM6DS0_AXIS_Y*2+1] << 8) | (databuf[LSM6DS0_AXIS_Y*2]));
		obj->data[LSM6DS0_AXIS_Z] = (s16)((databuf[LSM6DS0_AXIS_Z*2+1] << 8) | (databuf[LSM6DS0_AXIS_Z*2]));
		
#if DEBUG		
		if(atomic_read(&obj->trace) & GYRO_TRC_RAWDATA)
		{
			GYRO_LOG("read gyro register: %x, %x, %x, %x, %x, %x",
				databuf[0], databuf[1], databuf[2], databuf[3], databuf[4], databuf[5]);
			GYRO_LOG("get gyro raw data (0x%08X, 0x%08X, 0x%08X) -> (%5d, %5d, %5d)\n", 
				obj->data[LSM6DS0_AXIS_X],obj->data[LSM6DS0_AXIS_Y],obj->data[LSM6DS0_AXIS_Z],
				obj->data[LSM6DS0_AXIS_X],obj->data[LSM6DS0_AXIS_Y],obj->data[LSM6DS0_AXIS_Z]);
		}
#endif	

		obj->data[LSM6DS0_AXIS_X] = obj->data[LSM6DS0_AXIS_X] + obj->cali_sw[LSM6DS0_AXIS_X];
		obj->data[LSM6DS0_AXIS_Y] = obj->data[LSM6DS0_AXIS_Y] + obj->cali_sw[LSM6DS0_AXIS_Y];
		obj->data[LSM6DS0_AXIS_Z] = obj->data[LSM6DS0_AXIS_Z] + obj->cali_sw[LSM6DS0_AXIS_Z];
	
		/*remap coordinate*/
		data[obj->cvt.map[LSM6DS0_AXIS_X]] = obj->cvt.sign[LSM6DS0_AXIS_X]*obj->data[LSM6DS0_AXIS_X];
		data[obj->cvt.map[LSM6DS0_AXIS_Y]] = obj->cvt.sign[LSM6DS0_AXIS_Y]*obj->data[LSM6DS0_AXIS_Y];
		data[obj->cvt.map[LSM6DS0_AXIS_Z]] = obj->cvt.sign[LSM6DS0_AXIS_Z]*obj->data[LSM6DS0_AXIS_Z];

	
		//Out put the degree/second(o/s)
		data[LSM6DS0_AXIS_X] = data[LSM6DS0_AXIS_X] * LSM6DS0_OUT_MAGNIFY / LSM6DS0_FS_2000_LSB;
		data[LSM6DS0_AXIS_Y] = data[LSM6DS0_AXIS_Y] * LSM6DS0_OUT_MAGNIFY / LSM6DS0_FS_2000_LSB;
		data[LSM6DS0_AXIS_Z] = data[LSM6DS0_AXIS_Z] * LSM6DS0_OUT_MAGNIFY / LSM6DS0_FS_2000_LSB;
	 
	}

	sprintf(buf, "%04x %04x %04x", data[LSM6DS0_AXIS_X],data[LSM6DS0_AXIS_Y],data[LSM6DS0_AXIS_Z]);

#if DEBUG		
	if(atomic_read(&obj->trace) & GYRO_TRC_DATA)
	{
		GYRO_LOG("get gyro data packet:[%d %d %d]\n", data[0], data[1], data[2]);
	}
#endif
	
	return 0;
	
}


#if 0
static int LSM6DS0_SelfTest(struct i2c_client *client)
{
    int err =0;
	u8 data=0;
	char strbuf[LSM6DS0_BUFSIZE] = {0};
	int avgx_NOST,avgy_NOST,avgz_NOST;
	int sumx,sumy,sumz;
	int avgx_ST,avgy_ST,avgz_ST;
	int nost_x,nost_y,nost_z=0;
	int st_x,st_y,st_z=0;

	int resx,resy,resz=-1;
	int i=0;
	int testRes=0;
	int sampleNum =5;

	sumx=sumy=sumz=0;
	// 1 init 
    err = LSM6DS0_init_client(client, true);
	if(err)
	{
		GYRO_ERR("initialize client fail! err code %d!\n", err);
		return - 2;        
	}
	LSM6DS0_dumpReg(client);
	// 2 check ZYXDA bit
	LSM6DS0_read_byte_sr(client,LSM6DS0_STATUS_REG,&data,1);
	GYRO_LOG("LSM6DS0_STATUS_REG=%d\n",data );
	while(0x04 != (data&0x04))
	{
	  msleep(10);
	}
	msleep(1000); //wait for stable
	// 3 read raw data no self test data
	for(i=0; i<sampleNum; i++)
	{
	  LSM6DS0_ReadGyroData(client, strbuf, LSM6DS0_BUFSIZE);
	  sscanf(strbuf, "%x %x %x", &nost_x, &nost_y, &nost_z);
	  GYRO_LOG("NOst %d %d %d!\n", nost_x,nost_y,nost_z);
	  sumx += nost_x;
	  sumy += nost_y;
	  sumz += nost_z;
	  msleep(10);
	}
	//calculate avg x y z
	avgx_NOST = sumx/sampleNum;
	avgy_NOST = sumy/sampleNum;
	avgz_NOST = sumz/sampleNum;
	GYRO_LOG("avg NOST %d %d %d!\n", avgx_NOST,avgy_NOST,avgz_NOST);

	// 4 enalbe selftest
	LSM6DS0_read_byte_sr(client,LSM6DS0_CTL_REG4,&data,1);
	data = data | 0x02;
	hwmsen_write_byte(client,LSM6DS0_CTL_REG4,data);

	msleep(1000);//wait for stable

	LSM6DS0_dumpReg(client);
	// 5 check  ZYXDA bit
	
	//6 read raw data   self test data
	sumx=0;
	sumy=0;
	sumz=0;
	for(i=0; i<sampleNum; i++)
	{
	  LSM6DS0_ReadGyroData(client, strbuf, LSM6DS0_BUFSIZE);
	  sscanf(strbuf, "%x %x %x", &st_x, &st_y, &st_z);
	  GYRO_LOG("st %d %d %d!\n", st_x,st_y,st_z);
	  
	  sumx += st_x;
	  sumy += st_y;
	  sumz += st_z;
	
	  msleep(10);
	}
	// 7 calc calculate avg x y z ST
	avgx_ST = sumx/sampleNum;
	avgy_ST = sumy/sampleNum;
	avgz_ST = sumz/sampleNum;
	//GYRO_LOG("avg ST %d %d %d!\n", avgx_ST,avgy_ST,avgz_ST);
	//GYRO_LOG("abs(avgx_ST-avgx_NOST): %ld \n", abs(avgx_ST-avgx_NOST));
	//GYRO_LOG("abs(avgy_ST-avgy_NOST): %ld \n", abs(avgy_ST-avgy_NOST));
	//GYRO_LOG("abs(avgz_ST-avgz_NOST): %ld \n", abs(avgz_ST-avgz_NOST));

	if((abs(avgx_ST-avgx_NOST)>=175*131) && (abs(avgx_ST-avgx_NOST)<=875*131))
	{
	  resx =0; //x axis pass
	  GYRO_LOG(" x axis pass\n" );
	}
	if((abs(avgy_ST-avgy_NOST)>=175*131) && (abs(avgy_ST-avgy_NOST)<=875*131))
	{
	  resy =0; //y axis pass
	  GYRO_LOG(" y axis pass\n" );
	}
	if((abs(avgz_ST-avgz_NOST)>=175*131) && (abs(avgz_ST-avgz_NOST)<=875*131))
	{
	  resz =0; //z axis pass
	  GYRO_LOG(" z axis pass\n" );
	}

	if(0==resx && 0==resy && 0==resz)
	{
	  testRes = 0;
	}
	else
	{
	  testRes = -1;
	}

    hwmsen_write_byte(client,LSM6DS0_CTL_REG4,0x00);
	err = LSM6DS0_init_client(client, false);
	if(err)
	{
		GYRO_ERR("initialize client fail! err code %d!\n", err);
		return -2;        
	}
    GYRO_LOG("testRes %d!\n", testRes);
	return testRes;

}

//self test for factory 
static int LSM6DS0_SMTReadSensorData(struct i2c_client *client)
{
	//S16 gyro[LSM6DS0_AXES_NUM*LSM6DS0_FIFOSIZE];
	int res = 0;

	GYRO_FUN();
	res = LSM6DS0_SelfTest(client);

	GYRO_LOG(" LSM6DS0_SMTReadSensorData %d", res ); 
	
	return res;
}

#endif
/*----------------------------------------------------------------------------*/
static int LSM6DS0_ReadChipInfo(struct i2c_client *client, char *buf, int bufsize)
{
	u8 databuf[10];    

	memset(databuf, 0, sizeof(u8)*10);

	if((NULL == buf)||(bufsize<=30))
	{
		return -1;
	}
	
	if(NULL == client)
	{
		*buf = 0;
		return -2;
	}

	sprintf(buf, "LSM6DS0 Chip");
	return 0;
}


/*----------------------------------------------------------------------------*/
static ssize_t show_chipinfo_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = LSM6DS0_i2c_client;
	char strbuf[LSM6DS0_BUFSIZE];
	if(NULL == client)
	{
		GYRO_ERR("i2c client is null!!\n");
		return 0;
	}
	
	LSM6DS0_ReadChipInfo(client, strbuf, LSM6DS0_BUFSIZE);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);        
}
/*----------------------------------------------------------------------------*/
static ssize_t show_sensordata_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = LSM6DS0_i2c_client;
	char strbuf[LSM6DS0_BUFSIZE]= {0};
	
	if(NULL == client)
	{
		GYRO_ERR("i2c client is null!!\n");
		return 0;
	}
	
	LSM6DS0_ReadGyroData(client, strbuf, LSM6DS0_BUFSIZE);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);;            
}

/*----------------------------------------------------------------------------*/
static ssize_t show_trace_value(struct device_driver *ddri, char *buf)
{
	ssize_t res;
	struct LSM6DS0_i2c_data *obj = obj_i2c_data;
	if (obj == NULL)
	{
		GYRO_ERR("i2c_data obj is null!!\n");
		return 0;
	}
	
	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&obj->trace));     
	return res;    
}
/*----------------------------------------------------------------------------*/
static ssize_t store_trace_value(struct device_driver *ddri, const char *buf, size_t count)
{
	struct LSM6DS0_i2c_data *obj = obj_i2c_data;
	int trace;
	if (obj == NULL)
	{
		GYRO_ERR("i2c_data obj is null!!\n");
		return 0;
	}
	
	if(1 == sscanf(buf, "0x%x", &trace))
	{
		atomic_set(&obj->trace, trace);
	}	
	else
	{
		GYRO_ERR("invalid content: '%s', length = %d\n", buf, count);
	}
	
	return count;    
}
/*----------------------------------------------------------------------------*/
static ssize_t show_status_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;    
	struct LSM6DS0_i2c_data *obj = obj_i2c_data;
	if (obj == NULL)
	{
		GYRO_ERR("i2c_data obj is null!!\n");
		return 0;
	}	
	
	if(obj->hw)
	{
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: %d %d (%d %d)\n", 
	            obj->hw->i2c_num, obj->hw->direction, obj->hw->power_id, obj->hw->power_vol);   
	}
	else
	{
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: NULL\n");
	}
	return len;    
}
/*----------------------------------------------------------------------------*/

static DRIVER_ATTR(chipinfo,             S_IRUGO, show_chipinfo_value,      NULL);
static DRIVER_ATTR(sensordata,           S_IRUGO, show_sensordata_value,    NULL);
static DRIVER_ATTR(trace,      S_IWUGO | S_IRUGO, show_trace_value,         store_trace_value);
static DRIVER_ATTR(status,               S_IRUGO, show_status_value,        NULL);
/*----------------------------------------------------------------------------*/
static struct driver_attribute *LSM6DS0_attr_list[] = {
	&driver_attr_chipinfo,     /*chip information*/
	&driver_attr_sensordata,   /*dump sensor data*/	
	&driver_attr_trace,        /*trace log*/
	&driver_attr_status,        
};
/*----------------------------------------------------------------------------*/
static int LSM6DS0_create_attr(struct device_driver *driver) 
{
	int idx, err = 0;
	int num = (int)(sizeof(LSM6DS0_attr_list)/sizeof(LSM6DS0_attr_list[0]));
	if (driver == NULL)
	{
		return -EINVAL;
	}

	for(idx = 0; idx < num; idx++)
	{
		if(0 != (err = driver_create_file(driver, LSM6DS0_attr_list[idx])))
		{            
			GYRO_ERR("driver_create_file (%s) = %d\n", LSM6DS0_attr_list[idx]->attr.name, err);
			break;
		}
	}    
	return err;
}
/*----------------------------------------------------------------------------*/
static int LSM6DS0_delete_attr(struct device_driver *driver)
{
	int idx ,err = 0;
	int num = (int)(sizeof(LSM6DS0_attr_list)/sizeof(LSM6DS0_attr_list[0]));

	if(driver == NULL)
	{
		return -EINVAL;
	}
	

	for(idx = 0; idx < num; idx++)
	{
		driver_remove_file(driver, LSM6DS0_attr_list[idx]);
	}
	

	return err;
}

/*open drain mode set*/
static int LSM6DS0_PP_OD_init(client)
{
	u8 databuf[2] = {0};	
	int res = 0;
	GYRO_FUN();

	databuf[1] = 0x14;
	databuf[0] = LSM6DS0_CTL_REG8;	  
	res = i2c_master_send(client, databuf, 0x2);
	if(res <= 0)
	{
		GYRO_LOG("set PP_OD mode failed!\n");
		return LSM6DS0_ERR_I2C;
	}	

	return LSM6DS0_SUCCESS;    
}

/*----------------------------------------------------------------------------*/
static int LSM6DS0_init_client(struct i2c_client *client, bool enable)
{
	struct LSM6DS0_i2c_data *obj = i2c_get_clientdata(client);
	int res = 0;
	int i = 0;
	GYRO_FUN();	
    GYRO_LOG(" fwq LSM6DS0 addr %x!\n",client->addr);
#if 0
	res = LSM6DS0_PP_OD_init(client);
	if(res != LSM6DS0_SUCCESS)
	{
		GYRO_LOG("set PP_OD mode failed!\n");
		return res;
	}
#endif
	for(i = 0; i < 5; i++)
	{
		res = LSM6DS0_CheckDeviceID(client); 
		if(res = LSM6DS0_SUCCESS)
		{
			GYRO_LOG("check success time %d !\n", i);
			break;
		}
	}

	res = LSM6DS0_SetPowerMode(client, enable);
	GYRO_LOG("LSM6DS0_SetPowerMode res = %x\n", res);
	
	if(res != LSM6DS0_SUCCESS)
	{
		return res;
	}
	
	// The range should at least be 17.45 rad/s (ie: ~1000 deg/s).
	res = LSM6DS0_SetDataResolution(client,LSM6DS0_RANGE_2000);
	GYRO_LOG("LSM6DS0_SetDataResolution res = %x\n", res);
	
	if(res != LSM6DS0_SUCCESS) 
	{
		return res;
	}

	#if 0
	res = LSM6DS0_SetSampleRate(client, LSM6DS0_100HZ);
	GYRO_LOG("LSM6DS0_SetSampleRate res = %x\n", res);
	
	if(res != LSM6DS0_SUCCESS ) 
	{
		return res;
	}
	#endif
	
	GYRO_LOG("LSM6DS0_init_client OK!\n");

#ifdef CONFIG_LSM6DS0_LOWPASS
	memset(&obj->fir, 0x00, sizeof(obj->fir));  
#endif

	return LSM6DS0_SUCCESS;
}

/*----------------------------------------------------------------------------*/
int LSM6DS0_operate(void* self, uint32_t command, void* buff_in, int size_in,
		void* buff_out, int size_out, int* actualout)
{
	int err = 0;
	int value;	
	struct LSM6DS0_i2c_data *priv = (struct LSM6DS0_i2c_data*)self;
	hwm_sensor_data* gyro_data;
	char buff[LSM6DS0_BUFSIZE];	

	switch (command)
	{
		case SENSOR_DELAY:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				GYRO_ERR("Set delay parameter error!\n");
				err = -EINVAL;
			}
			else
			{
			
			}
			break;

		case SENSOR_ENABLE:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				GYRO_ERR("Enable gyroscope parameter error!\n");
				err = -EINVAL;
			}
			else
			{
				value = *(int *)buff_in;
				if(((value == 0) && (sensor_power == false)) ||((value == 1) && (sensor_power == true)))
				{
					GYRO_LOG("gyroscope device have updated!\n");
				}
				else
				{
					err = LSM6DS0_SetPowerMode(priv->client, !sensor_power);
				}
			}
			break;

		case SENSOR_GET_DATA:
			if((buff_out == NULL) || (size_out< sizeof(hwm_sensor_data)))
			{
				GYRO_ERR("get gyroscope data parameter error!\n");
				err = -EINVAL;
			}
			else
			{
				gyro_data = (hwm_sensor_data *)buff_out;
				LSM6DS0_ReadGyroData(priv->client, buff, LSM6DS0_BUFSIZE);
				sscanf(buff, "%x %x %x", &gyro_data->values[0], 
									&gyro_data->values[1], &gyro_data->values[2]);				
				gyro_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;				
				gyro_data->value_divide = DEGREE_TO_RAD;
			}
			break;
		default:
			GYRO_ERR("gyroscope operate function no this parameter %d!\n", command);
			err = -1;
			break;
	}

	return err;
}

/****************************************************************************** 
 * Function Configuration
******************************************************************************/
static int LSM6DS0_open(struct inode *inode, struct file *file)
{
	file->private_data = LSM6DS0_i2c_client;

	if(file->private_data == NULL)
	{
		GYRO_ERR("null pointer!!\n");
		return -EINVAL;
	}
	return nonseekable_open(inode, file);
}
/*----------------------------------------------------------------------------*/
static int LSM6DS0_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}
/*----------------------------------------------------------------------------*/
//static int LSM6DS0_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
//       unsigned long arg)
static long LSM6DS0_ioctl(struct file *file, unsigned int cmd,
       unsigned long arg)
{
	struct i2c_client *client = (struct i2c_client*)file->private_data;
	//struct LSM6DS0_i2c_data *obj = (struct LSM6DS0_i2c_data*)i2c_get_clientdata(client);	
	char strbuf[LSM6DS0_BUFSIZE] = {0};
	void __user *data;
	long err = 0;
	int copy_cnt = 0;
	SENSOR_DATA sensor_data;
	int cali[3];
	int smtRes=0;
	//GYRO_FUN();
	
	if(_IOC_DIR(cmd) & _IOC_READ)
	{
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	}
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
	{
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}

	if(err)
	{
		GYRO_ERR("access error: %08X, (%2d, %2d)\n", cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch(cmd)
	{
		case GYROSCOPE_IOCTL_INIT:
			LSM6DS0_init_client(client, false);			
			break;

		case GYROSCOPE_IOCTL_SMT_DATA:
			#if 0
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}

			smtRes = LSM6DS0_SMTReadSensorData(client);
			GYRO_LOG("IOCTL smtRes: %d!\n", smtRes);
			copy_cnt = copy_to_user(data, &smtRes,  sizeof(smtRes));
			
			if(copy_cnt)
			{
				err = -EFAULT;
				GYRO_ERR("copy gyro data to user failed!\n");
			}
			#endif
			
			GYRO_LOG("GYROSCOPE SMT Test not support !\n");
			break;
			

		case GYROSCOPE_IOCTL_READ_SENSORDATA:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			
			LSM6DS0_ReadGyroData(client, strbuf, LSM6DS0_BUFSIZE);
			if(copy_to_user(data, strbuf, sizeof(strbuf)))
			{
				err = -EFAULT;
				break;	  
			}				 
			break;

		case GYROSCOPE_IOCTL_SET_CALI:
			data = (void __user*)arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			if(copy_from_user(&sensor_data, data, sizeof(sensor_data)))
			{
				err = -EFAULT;
				break;	  
			}
			
			else
			{
				cali[LSM6DS0_AXIS_X] = sensor_data.x * LSM6DS0_FS_2000_LSB / LSM6DS0_OUT_MAGNIFY;
				cali[LSM6DS0_AXIS_Y] = sensor_data.y * LSM6DS0_FS_2000_LSB / LSM6DS0_OUT_MAGNIFY;
				cali[LSM6DS0_AXIS_Z] = sensor_data.z * LSM6DS0_FS_2000_LSB / LSM6DS0_OUT_MAGNIFY;			  
				err = LSM6DS0_WriteCalibration(client, cali);
			}
			break;

		case GYROSCOPE_IOCTL_CLR_CALI:
			err = LSM6DS0_ResetCalibration(client);
			break;

		case GYROSCOPE_IOCTL_GET_CALI:
			data = (void __user*)arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			err = LSM6DS0_ReadCalibration(client, cali);
			if(err)
			{
				break;
			}
			
			sensor_data.x = cali[LSM6DS0_AXIS_X] * LSM6DS0_OUT_MAGNIFY / LSM6DS0_FS_2000_LSB;
			sensor_data.y = cali[LSM6DS0_AXIS_Y] * LSM6DS0_OUT_MAGNIFY / LSM6DS0_FS_2000_LSB;
			sensor_data.z = cali[LSM6DS0_AXIS_Z] * LSM6DS0_OUT_MAGNIFY / LSM6DS0_FS_2000_LSB;
			if(copy_to_user(data, &sensor_data, sizeof(sensor_data)))
			{
				err = -EFAULT;
				break;
			}		
			break;

		default:
			GYRO_ERR("unknown IOCTL: 0x%08x\n", cmd);
			err = -ENOIOCTLCMD;
			break;			
	}
	return err;
}


/*----------------------------------------------------------------------------*/
static struct file_operations LSM6DS0_fops = {
//	.owner = THIS_MODULE,
	.open = LSM6DS0_open,
	.release = LSM6DS0_release,
	.unlocked_ioctl = LSM6DS0_ioctl,
};
/*----------------------------------------------------------------------------*/
static struct miscdevice LSM6DS0_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gyroscope",
	.fops = &LSM6DS0_fops,
};
/*----------------------------------------------------------------------------*/
#ifndef CONFIG_HAS_EARLYSUSPEND
/*----------------------------------------------------------------------------*/
static int LSM6DS0_suspend(struct i2c_client *client, pm_message_t msg) 
{
	struct LSM6DS0_i2c_data *obj = i2c_get_clientdata(client);    
	GYRO_FUN();    

	if(msg.event == PM_EVENT_SUSPEND)
	{   
		if(obj == NULL)
		{
			GYRO_ERR("null pointer!!\n");
			return -EINVAL;
		}
		atomic_set(&obj->suspend, 1);		
		
		err = LSM6DS0_SetPowerMode(client, false);
		if(err <= 0)
		{
			return err;
		}
	}
	return err;
}
/*----------------------------------------------------------------------------*/
static int LSM6DS0_resume(struct i2c_client *client)
{
	struct LSM6DS0_i2c_data *obj = i2c_get_clientdata(client);        
	int err;
	GYRO_FUN();

	if(obj == NULL)
	{
		GYRO_ERR("null pointer!!\n");
		return -EINVAL;
	}

	LSM6DS0_power(obj->hw, 1);
	err = LSM6DS0_init_client(client, false);
	if(err)
	{
		GYRO_ERR("initialize client fail!!\n");
		return err;        
	}
	atomic_set(&obj->suspend, 0);

	return 0;
}
/*----------------------------------------------------------------------------*/
#else /*CONFIG_HAS_EARLY_SUSPEND is defined*/
/*----------------------------------------------------------------------------*/
static void LSM6DS0_early_suspend(struct early_suspend *h) 
{
	struct LSM6DS0_i2c_data *obj = container_of(h, struct LSM6DS0_i2c_data, early_drv);   
	int err;
	GYRO_FUN();    

	if(obj == NULL)
	{
		GYRO_ERR("null pointer!!\n");
		return;
	}
	
	atomic_set(&obj->suspend, 1);
	err = LSM6DS0_SetPowerMode(obj->client, false);
	if(err)
	{
		GYRO_ERR(" LSM6DS0_early_suspend write power control fail!!\n");
		return;
	}

	LSM6DS0_power(obj->hw, 0);

	return;
}
/*----------------------------------------------------------------------------*/
static void LSM6DS0_late_resume(struct early_suspend *h)
{
	struct LSM6DS0_i2c_data *obj = container_of(h, struct LSM6DS0_i2c_data, early_drv);         
	int err;
	GYRO_FUN();

	if(obj == NULL)
	{
		GYRO_ERR("null pointer!!\n");
		return;
	}

	atomic_set(&obj->suspend, 0);
	err = LSM6DS0_SetPowerMode(obj->client, true);
	if(err)
	{
		GYRO_ERR("LSM6DS0_late_resume write power control fail!!\n");
		return;
	}
	
	LSM6DS0_power(obj->hw, 1);

	return;
}
/*----------------------------------------------------------------------------*/
#endif /*CONFIG_HAS_EARLYSUSPEND*/
/*----------------------------------------------------------------------------*/
static int LSM6DS0_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info) 
{    
	strcpy(info->type, LSM6DS0_DEV_NAME);
	return 0;
}

// if use  this typ of enable , Gsensor should report inputEvent(x, y, z ,stats, div) to HAL
static int LSM6DS0_open_report_data(int open)
{
    //should queuq work to report event if  is_report_input_direct=true
    return 0;
}

// if use  this typ of enable , Gsensor only enabled but not report inputEvent to HAL

static int LSM6DS0_enable_nodata(int en)
{
    int res =0;
    int retry = 0;
    bool power=false;

    if(1==en)
    {
        power=true;
    }
    if(0==en)
    {
        power =false;
    }

    for(retry = 0; retry < 3; retry++){
        res = LSM6DS0_SetPowerMode(obj_i2c_data->client, power);
        if(res == 0)
        {
            GYRO_LOG("LSM6DS0_SetPowerMode done\n");
            break;
        }
        GYRO_LOG("LSM6DS0_SetPowerMode fail\n");
    }


    if(res != LSM6DS0_SUCCESS)
    {
        GYRO_LOG("LSM6DS0_SetPowerMode fail!\n");
        return -1;
    }
    GYRO_LOG("LSM6DS0_enable_nodata OK!\n");
    return 0;

}

static int LSM6DS0_set_delay(u64 ns)
{
    return 0;
}

static int LSM6DS0_get_data(int* x ,int* y,int* z, int* status)
{
    char buff[LSM6DS0_BUFSIZE];
    LSM6DS0_ReadGyroData(obj_i2c_data->client, buff, LSM6DS0_BUFSIZE);

    sscanf(buff, "%x %x %x", x, y, z);
    *status = SENSOR_STATUS_ACCURACY_MEDIUM;

    return 0;
}


/*----------------------------------------------------------------------------*/
static int LSM6DS0_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_client *new_client;
	struct LSM6DS0_i2c_data *obj;
	int err = 0;
    struct gyro_control_path ctl={0};
    struct gyro_data_path data={0};
	GYRO_FUN();

	if(!(obj = kzalloc(sizeof(struct LSM6DS0_i2c_data), GFP_KERNEL)))
	{
		err = -ENOMEM;
		goto exit;
	}
	
	memset(obj, 0, sizeof(struct LSM6DS0_i2c_data));

	obj->hw = get_cust_gyro_hw();
	err = hwmsen_get_convert(obj->hw->direction, &obj->cvt);
	if(err)
	{
		GYRO_ERR("invalid direction: %d\n", obj->hw->direction);
		goto exit;
	}

	obj_i2c_data = obj;
	obj->client = client;
	new_client = obj->client;
	i2c_set_clientdata(new_client,obj);
	
	atomic_set(&obj->trace, 0);
	atomic_set(&obj->suspend, 0);
	
	LSM6DS0_i2c_client = new_client;	
	err = LSM6DS0_init_client(new_client, false);
	if(err)
	{
		goto exit_init_failed;
	}
	
	err = misc_register(&LSM6DS0_device);
	if(err)
	{
		GYRO_ERR("LSM6DS0_device misc register failed!\n");
		goto exit_misc_device_register_failed;
	}

    err = LSM6DS0_create_attr(&(LSM6DS0_init_info.platform_diver_addr->driver));

	if(err)
	{
		GYRO_ERR("LSM6DS0 create attribute err = %d\n", err);
		goto exit_create_attr_failed;
	}

    ctl.open_report_data= LSM6DS0_open_report_data;
    ctl.enable_nodata = LSM6DS0_enable_nodata;
    ctl.set_delay  = LSM6DS0_set_delay;
    ctl.is_report_input_direct = false;
    ctl.is_support_batch = obj->hw->is_batch_supported;

    err = gyro_register_control_path(&ctl);
    if(err)
    {
        GYRO_ERR("register gyro control path err\n");
        goto exit_kfree;
    }

    data.get_data = LSM6DS0_get_data;
    data.vender_div = DEGREE_TO_RAD;
    err = gyro_register_data_path(&data);
    if(err)
        {
        GYRO_ERR("gyro_register_data_path fail = %d\n", err);
        goto exit_kfree;
        }
    err = batch_register_support_info(ID_GYROSCOPE,obj->hw->is_batch_supported,0, 0);
    if(err)
    {
        GYRO_ERR("register gyro batch support err = %d\n", err);
        goto exit_kfree;
    }

#ifdef CONFIG_HAS_EARLYSUSPEND
	obj->early_drv.level    = EARLY_SUSPEND_LEVEL_STOP_DRAWING - 2,
	obj->early_drv.suspend  = LSM6DS0_early_suspend,
	obj->early_drv.resume   = LSM6DS0_late_resume,    
	register_early_suspend(&obj->early_drv);
#endif 
    LSM6DS0_init_flag =0;

	GYRO_LOG("%s: OK\n", __func__);    
	return 0;

	exit_create_attr_failed:
	misc_deregister(&LSM6DS0_device);
	exit_misc_device_register_failed:
	exit_init_failed:
	//i2c_detach_client(new_client);
	exit_kfree:
	kfree(obj);
	exit:
    LSM6DS0_init_flag =-1;
	GYRO_ERR("%s: err = %d\n", __func__, err);        
	return err;
}

/*----------------------------------------------------------------------------*/
static int LSM6DS0_i2c_remove(struct i2c_client *client)
{
	int err = 0;	
	
    err = LSM6DS0_delete_attr(&(LSM6DS0_init_info.platform_diver_addr->driver));
	if(err)
	{
		GYRO_ERR("LSM6DS0_delete_attr fail: %d\n", err);
	}
	
	err = misc_deregister(&LSM6DS0_device);
	if(err)
	{
		GYRO_ERR("misc_deregister fail: %d\n", err);
	}

	err = hwmsen_detach(ID_ACCELEROMETER);
	if(err)
	{
		GYRO_ERR("hwmsen_detach fail: %d\n", err);
	}

	LSM6DS0_i2c_client = NULL;
	i2c_unregister_device(client);
	kfree(i2c_get_clientdata(client));
	return 0;
}
/*----------------------------------------------------------------------------*/
static int LSM6DS0_remove(void)
{
    struct gyro_hw *hw = get_cust_gyro_hw();
    GYRO_FUN();
    LSM6DS0_power(hw, 0);
    i2c_del_driver(&LSM6DS0_i2c_driver);
    return 0;
}
/*----------------------------------------------------------------------------*/
static int LSM6DS0_local_init(void)
{
    struct gyro_hw *hw = get_cust_gyro_hw();
    //printk("fwq loccal init+++\n");

    LSM6DS0_power(hw, 1);
    if(i2c_add_driver(&LSM6DS0_i2c_driver))
    {
        GYRO_ERR("add driver error\n");
        return -1;
    }
    if(-1 == LSM6DS0_init_flag)
    {
       return -1;
    }
    //printk("fwq loccal init---\n");
    return 0;
}


/*----------------------------------------------------------------------------*/
static int __init LSM6DS0_init(void)
{
	struct gyro_hw *hw = get_cust_gyro_hw();
	GYRO_LOG("%s: i2c_number=%d\n", __func__,hw->i2c_num); 
	i2c_register_board_info(hw->i2c_num, &i2c_LSM6DS0, 1);
    gyro_driver_add(&LSM6DS0_init_info);

	return 0;    
}
/*----------------------------------------------------------------------------*/
static void __exit LSM6DS0_exit(void)
{
	GYRO_FUN();
}
/*----------------------------------------------------------------------------*/
module_init(LSM6DS0_init);
module_exit(LSM6DS0_exit);
/*----------------------------------------------------------------------------*/
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LSM6DS0 gyroscope driver");
MODULE_AUTHOR("Chunlei.Wang@mediatek.com");
