#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "nvram.h"

unsigned char flash_type(void)
{
	return NVRAM_FLASH_TYPE_FOPEN;
}

int flash_read(char *zone,char *read_buf,unsigned int read_buf_size,unsigned int *read_size,char bak_flag)
{
	char 		tmp_buf[256];

	if(zone == NULL || read_buf == NULL || read_size == NULL)
	{
		return EINVAL;
	}
	//读取文件
	snprintf(tmp_buf,sizeof(tmp_buf),DEFAULT_NVRAM_CONF_PATH""DEFAULT_NVRAM_DATA_NAME_HEAD"%s%s",zone,(bak_flag == 1)?"_bak":"");
	FILE *fp = fopen(tmp_buf,"rb");
	if(fp)
	{
		char *psrc = read_buf;
		while(fread(psrc,1,1,fp) && (psrc <= (read_buf + read_buf_size)))
		{
			psrc++;
		}
		fclose(fp);
		read_buf[read_buf_size - 1] = '\0';
		*read_size = psrc - read_buf;
		return 0;
	}
	else
	{
		*read_size = 0;
		return errno;
	}
}

int flash_write(char *zone,char *write_buf,unsigned int write_size,char bak_flag)
{
	char 		tmp_buf[256];

	if(zone == NULL || write_buf == NULL)
	{
		return EINVAL;
	}
	//写入文件
	snprintf(tmp_buf,sizeof(tmp_buf),DEFAULT_NVRAM_CONF_PATH""DEFAULT_NVRAM_DATA_NAME_HEAD"%s%s",zone,(bak_flag == 1)?"_bak":"");
	FILE *fp = fopen(tmp_buf,"wb");
	if(fp)
	{
		int write_count = 0;
		int ret;
		char *psrc = write_buf;
		while((ret = fwrite(psrc,1,write_size - write_count,fp)) && (write_count < write_size))
		{
			write_count = write_count + ret;
		}
		//虽然fclose会调用一次fflush,放在这里警示,这里的数据一定要保存
		ret = fflush(fp);
		if(ret != 0)
		{
			return errno;
		}
		ret = fclose(fp);
		if(ret != 0)
		{
			return errno;
		}
		return 0;
	}
	else
	{
		return errno;
	}
}

