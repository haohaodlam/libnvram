#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <syslog.h>		//用于调试打印

#include "nvram.h"
#include "aesende.h"

union semun
{//用于信号量初始化时用的联合体
   int              val;    /* Value for SETVAL */
   struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
   unsigned short  *array;  /* Array for GETALL, SETALL */
   struct seminfo  *__buf;  /* Buffer for IPC_INFO
                               (Linux specific) */
};
//下面三个宏会影响共享内存的大小,不能随意更改大小
#define DEFAULT_NVRAM_NAME_MAX 			32
#define DEFAULT_NVRAM_VALUE_MAX 		256
#if !defined(DEFAULT_NVRAM_COUNT_MAX)
#define DEFAULT_NVRAM_COUNT_MAX			1024
#endif
typedef struct cache_t
{
	char name[DEFAULT_NVRAM_NAME_MAX];
	char value[DEFAULT_NVRAM_VALUE_MAX];
} cache_t;

typedef struct zone_s {
	unsigned char vaild;	//共享内存是否初始化标记
	unsigned char dirty;	//共享内存是否修改标记
	cache_t	cache[DEFAULT_NVRAM_COUNT_MAX];
}zone_s;
//存储在掉电非易失介质中的数据块头域
#define CONF_FILE_HEAD_MAGIC			0x12345678
#define CONF_FILE_HEAD_TYPE_NULL		0
#define CONF_FILE_HEAD_TYPE_ROUTE		1
#define CONF_FILE_HEAD_TYPE_VOIP 		2
#define CONF_FILE_HEAD_TYPE_TAR 		3
#define CONF_FILE_HEAD_TYPE_MAX 		4

typedef struct ConfFileHead {//配置文件头
	unsigned int	magic;			//头域
	unsigned int	data_len;		//数据长度
	unsigned char	type;			//数据类型
	unsigned char	file_version;	//配置文件版本
	unsigned int	from_version;	//导出设备的版本
	unsigned char	iv[16];			//aes加密vi域
	unsigned short	reserve;		//保留
//	char			data[0];		//后接加密数据
}__attribute__ ((packed))ConfFileHead;

//目前还不能完全确定同一个进程使用两个以上的zone时,zone_tmp是否可能会和其他zone冲突
//如果你的进程中仅使用唯一的zone,则不会有问题
//该全局变量存在的意义是给nvram_get的返回值一个存在的内存空间
zone_s * zone_tmp = NULL;

//以下函数与硬件相关,不同的掉电不易失存储介质操作方法不一样
//目前仅支持nvram_flash_utils_fopen.c和nvram_flash_utils_mysql.c
unsigned char flash_type(void);
int flash_read(char *zone,char *read_buf,unsigned int read_buf_size,unsigned int *read_size,char bak_flag);
int flash_write(char *zone,char *write_buf,unsigned int write_size,char bak_flag);

static int shmem_open(char *zone,int file_chmod,void **buf,unsigned int buf_size)
{
	char	tmp_buf[256];
	key_t	file_key;
	int		shmid = -1;
	int		semid = -1;

	if(zone == NULL || buf == NULL )
	{
		fprintf(stderr,"%s(ponit(NULL))\n",__FUNCTION__);
		return EINVAL;
	}

	snprintf(tmp_buf,sizeof(tmp_buf),DEFAULT_NVRAM_CONF_PATH""DEFAULT_NVRAM_SHMEM_NAME_HEAD"%s",zone);
	file_key = ftok(tmp_buf,'a');//生成计算标识符
	if(file_key == -1)
	{
		fprintf(stderr,"%s(ftok(file = %s)) = %s\n",__FUNCTION__,tmp_buf,strerror(errno));
		return errno;
	}
	//获取信号量id
	semid = semget(file_key,1,IPC_CREAT|0666);
	if(semid == -1)
	{
		fprintf(stderr,"%s(semget(%s))\n",__FUNCTION__,strerror(errno));
		return errno;
	}
	//查看信号量是否有初始化
	struct stat file_stat;
	char filename_sem[256];
	snprintf(filename_sem,sizeof(filename_sem),DEFAULT_NVRAM_SEM_INIT_PATH""DEFAULT_NVRAM_SEM_INIT_NAME_HEAD"%s",zone);
	if(stat(filename_sem,&file_stat) != 0)
	{//信号量初始化标记文件不存在,初始化
		//信号量初始值设置为1
		union semun semun_union;
		semun_union.val = 1;
		if(semctl(semid,0,SETVAL,semun_union) == -1)
		{
			fprintf(stderr,"%s(semctl(%s))\n",__FUNCTION__,strerror(errno));
		}
		else
		{//信号量初始化成功设置标记文件
			FILE *fp = fopen(filename_sem,"wb");if(fp)fclose(fp);//touch标记文件
		}
	}
	{//信号量P操作
		struct sembuf sb={0,-1,SEM_UNDO};
		semop(semid,&sb,1);//P操作
	}

	shmid = shmget(file_key ,buf_size, IPC_CREAT | 0666) ;//获得共享内存
	if(shmid == -1)
	{
		fprintf(stderr,"%s(shmget(%s))\n",__FUNCTION__,strerror(errno));
		{//信号量V操作
			struct sembuf sb={0,+1,SEM_UNDO};
			semop(semid,&sb,1);
		}
		return errno;
	}
	else
	{
		int temp_chmod = 0;
		if(file_chmod&SHM_RDONLY)temp_chmod = SHM_RDONLY;
		*buf = shmat(shmid,0,temp_chmod);//连接共享内存，类型为void
		if(*buf == (void *)-1)
		{
			fprintf(stderr,"%s(shmat(%s))\n",__FUNCTION__,strerror(errno));
			{//信号量V操作
				struct sembuf sb={0,+1,SEM_UNDO};
				semop(semid,&sb,1);
			}
			return errno;
		}
		return 0;
	}
}

static int shmem_close(char *zone,const void *buf)
{
	char	tmp_buf[256];
	key_t   file_key;
	int		semid = -1;
	int 	ret;

	if(buf == NULL)
	{
		fprintf(stderr,"%s(ponit(NULL))\n",__FUNCTION__);
		return EINVAL;
	}

	ret = shmdt(buf);
	if(ret == -1)
	{
		fprintf(stderr,"%s(shmdt(%s))\n",__FUNCTION__,strerror(errno));
		ret = errno;//因为无论是否处理正常,都要释放信号量,所以这里既是发生错误也先不返回
	}

	snprintf(tmp_buf,sizeof(tmp_buf),DEFAULT_NVRAM_CONF_PATH""DEFAULT_NVRAM_SHMEM_NAME_HEAD"%s",zone);
	file_key = ftok(tmp_buf,'a');//生成计算标识符
	if(file_key == -1)
	{
		fprintf(stderr,"%s(ftok(file = %s)) = %s\n",__FUNCTION__,tmp_buf,strerror(errno));
		//释放信号量时出错,信号量就锁死了,代码运行到此处,就悲剧了
		return errno;
	}
	//获取信号量id
	semid = semget(file_key,1,IPC_CREAT|0666);
	if(semid == -1)
	{
		fprintf(stderr,"%s(semget(%s))\n",__FUNCTION__,strerror(errno));
		//释放信号量时出错,信号量就锁死了,代码运行到此处,就悲剧了
		return errno;
	}
	{//信号量V操作
		struct sembuf sb={0,+1,SEM_UNDO};
		semop(semid,&sb,1);
	}
	if(ret)return ret;
	return 0;
}

static int shmem_close_remove(char *zone,const void *buf)
{
	char	tmp_buf[256];
	key_t   file_key;
	int		semid = -1;
	int 	ret;

	if(buf == NULL)
	{
		fprintf(stderr,"%s(ponit(NULL))\n",__FUNCTION__);
		return EINVAL;
	}

	ret = shmdt(buf);
	if(ret == -1)
	{
		fprintf(stderr,"%s(shmdt(%s))\n",__FUNCTION__,strerror(errno));
		ret = errno;//因为无论是否处理正常,都要释放信号量,所以这里既是发生错误也先不返回
	}

	snprintf(tmp_buf,sizeof(tmp_buf),DEFAULT_NVRAM_CONF_PATH""DEFAULT_NVRAM_SHMEM_NAME_HEAD"%s",zone);
	file_key = ftok(tmp_buf,'a');//生成计算标识符
	if(file_key == -1)
	{
		fprintf(stderr,"%s(ftok(file = %s)) = %s\n",__FUNCTION__,tmp_buf,strerror(errno));
		//释放信号量时出错,信号量就锁死了,代码运行到此处,就悲剧了
		return errno;
	}

	ret = shmctl(file_key,IPC_RMID,NULL);
	if(ret == -1)
	{
		fprintf(stderr,"%s(shmctl(%s))\n",__FUNCTION__,strerror(errno));
		ret = errno;//因为无论是否处理正常,都要释放信号量,所以这里既是发生错误也先不返回
	}

	//获取信号量id
	semid = semget(file_key,1,IPC_CREAT|0666);
	if(semid == -1)
	{
		fprintf(stderr,"%s(semget(%s))\n",__FUNCTION__,strerror(errno));
		//释放信号量时出错,信号量就锁死了,代码运行到此处,就悲剧了
		return errno;
	}
	{//信号量V操作
		struct sembuf sb={0,+1,SEM_UNDO};
		semop(semid,&sb,1);
	}
	if(ret)return ret;
	return 0;
}

static int nvram_read_data(char *zone,zone_s *zone_buf)
{
	char *tmp = NULL;
	tmp = malloc(sizeof(zone_s)*2);
	if(tmp)
	{
		int ret = AES_ERR_UNKOWN;
		unsigned int read_size;
		char bak_flag = 0;
		while(1)
		{
			ret = flash_read(zone,tmp,sizeof(zone_s)*2,&read_size,bak_flag);
			if(ret == 0)
			{
				//分析头文件
				ConfFileHead *conf_file_head = (ConfFileHead *)tmp;
				if(conf_file_head->magic != CONF_FILE_HEAD_MAGIC)//确定是配置文件
				{
					fprintf(stderr,"%s() = conf_file_head magic error 0x%08x\n",__FUNCTION__,conf_file_head->magic);
					ret = ERR_NVRAM_CONF_HEAD_MAGIC;
				}
				else if(((conf_file_head->data_len != (read_size - sizeof(ConfFileHead))) && conf_file_head->data_len > 0) || (conf_file_head->data_len == 0))
				{
					fprintf(stderr,"%s() = conf_file_head data len error flash_read = %d,data_len = %d\n",__FUNCTION__,read_size,conf_file_head->data_len);
					ret = ERR_NVRAM_CONF_DATA_LEN;
				}
				else if((conf_file_head->file_version & 0xf0) != 0x10)
				{//目前只支持1.x版本的配置文件
					fprintf(stderr,"%s() = conf_file_head crypt version error\n",__FUNCTION__);
					ret = ERR_NVRAM_ZONE_CRYPT_VERSION;
				}
				else if(conf_file_head->type == CONF_FILE_HEAD_TYPE_ROUTE)
				{
					char *outbuf = NULL;
					outbuf = malloc(sizeof(zone_s));
					if(outbuf)
					{
						int  outlen = 0;
						char aes_key[16 + 1];//计算字符结束位'\0'
						sprintf(aes_key,"NVRAM_AES_KEY");
						strcat(aes_key,"_");
						strcat(aes_key,"0");
						strcat(aes_key,"0");
						aes_key[16] = '\0';

						memset(outbuf,0,sizeof(zone_s));
						ret = AESDecode(AES_OPTION_PKCS_PADDING|AES_OPTION_CBC,aes_key,strlen(aes_key),conf_file_head->iv,tmp + sizeof(ConfFileHead),read_size - sizeof(ConfFileHead),outbuf,sizeof(zone_s),&outlen);
						if(ret)
						{
							fprintf(stderr,"%s() = data decrypt error ret=0x%08x\n",__FUNCTION__,ret);
							ret = ERR_NVRAM_ZONE_DECRYPT;
						}
						else
						{
							//从outbuf中分析数据到zone_buf
							char *tmp_name  = outbuf;
							char *tmp_value = NULL;
							unsigned int count = DEFAULT_NVRAM_COUNT_MAX;
							unsigned int index = 0;
							while(count--)
							{
								char *next = strchr(tmp_name, '\n');
								if(next)
								{
									*next = '\0';
									next++;
									//提取当前行的内容
									tmp_value = strchr(tmp_name, '=');
									if(tmp_value)
									{//当前行有=
										*tmp_value = '\0';
										tmp_value++;
										strncpy(zone_buf->cache[index].name,tmp_name,sizeof(zone_buf->cache[index].name));
										zone_buf->cache[index].name[sizeof(zone_buf->cache[index].name) - 1] = '\0';
										strncpy(zone_buf->cache[index].value,tmp_value,sizeof(zone_buf->cache[index].value));
										zone_buf->cache[index].value[sizeof(zone_buf->cache[index].value) - 1] = '\0';
										index++;
									}
									tmp_name = next;
								}
								else
								{
									break;
								}
							}
						}
						free(outbuf);
					}
					else
					{
						ret = ENOMEM;
					}
				}
			}
			if(ret == 0)
			{
				break;
			}
			else
			{
				if(bak_flag < 1)
				{
					bak_flag++;
				}
				else
				{
					break;
				}
			}
		}
		free(tmp);
		return ret;
	}
	else
	{
		return ENOMEM;
	}
	return 0;
}

static int nvram_write_data(char *zone,zone_s *zone_buf)
{
	char *tmp = NULL;
	srand(time(NULL));
	tmp = malloc(sizeof(zone_s)*2);
	if(tmp)
	{
		int ret = AES_ERR_UNKOWN;
		char *inbuf = NULL;
		inbuf = malloc(sizeof(zone_s));
		if(inbuf)
		{
			int  outlen = 0;
			char aes_key[16 + 1];//计算字符结束位'\0'
			//组建文域
			memset(tmp,0,sizeof(sizeof(zone_s)*2));
			ConfFileHead *conf_file_head = (ConfFileHead *)tmp;
			conf_file_head->magic		 = CONF_FILE_HEAD_MAGIC;
			conf_file_head->file_version = 0x10;
			conf_file_head->type = CONF_FILE_HEAD_TYPE_ROUTE;
			//组建加密前的数据
			memset(inbuf,0,sizeof(zone_s));
			int i;
			for(i=0;i<DEFAULT_NVRAM_COUNT_MAX;i++)
			{
				if(strlen(zone_buf->cache[i].name))
				{
					char temp[DEFAULT_NVRAM_NAME_MAX + DEFAULT_NVRAM_VALUE_MAX + 1 + 1 + 1];//名,值,=,\n,\0
					snprintf(temp,sizeof(temp),"%s=%s\n",zone_buf->cache[i].name,zone_buf->cache[i].value);
					strcat(inbuf,temp);
				}
			}
			//生成秘钥和iv
			sprintf(aes_key,"NVRAM_AES_KEY");
			strcat(aes_key,"_");
			strcat(aes_key,"0");
			strcat(aes_key,"0");
			aes_key[16] = '\0';
			//生成iv
			for(i=0;i<16;i++)
			{
				conf_file_head->iv[i] = rand()%256;
			}
			//加密
			ret = AESEncode(AES_OPTION_PKCS_PADDING|AES_OPTION_CBC,aes_key,strlen(aes_key),conf_file_head->iv,inbuf,strlen(inbuf),tmp + sizeof(ConfFileHead),sizeof(zone_s)*2 - sizeof(ConfFileHead),&outlen);
			if(ret)
			{
				fprintf(stderr,"%s() = data encrypt error ret=0x%08x\n",__FUNCTION__,ret);
				ret = ERR_NVRAM_ZONE_ENCRYPT;
			}
			else
			{
				conf_file_head->data_len = outlen;
			}
			free(inbuf);
			//写入数据
			ret = flash_write(zone,tmp,outlen + sizeof(ConfFileHead),0);
			if(ret != 0)
			{
				fprintf(stderr,"%s ->flash_write(%s) err = %s)\n",__FUNCTION__,zone,strerror(errno));
			}
			else
			{
				ret = flash_write(zone,tmp,outlen + sizeof(ConfFileHead),1);
			}
		}
		else
		{
			ret = ENOMEM;
		}
		free(tmp);
		return ret;
	}
	else
	{
		return ENOMEM;
	}
	return 0;

}

const char *nvram_error(int ret)
{
	if(ret > 0)
	{
		return strerror(ret);
	}
	else if(ret < 0)
	{
		switch(ret)
		{
			case ERR_NVRAM_ZONE_OVERFLOW:
				return "ERR_NVRAM_ZONE_OVERFLOW";
			case ERR_NVRAM_CONF_HEAD_MAGIC:
				return "ERR_NVRAM_CONF_HEAD_MAGIC";
			case ERR_NVRAM_CONF_DATA_LEN:
				return "ERR_NVRAM_CONF_DATA_LEN";
			case ERR_NVRAM_ZONE_CRYPT_VERSION:
				return "ERR_NVRAM_ZONE_CRYPT_VERSION";
			case ERR_NVRAM_ZONE_DECRYPT:
				return "ERR_NVRAM_ZONE_DECRYPT";
			case ERR_NVRAM_ZONE_ENCRYPT:
				return "ERR_NVRAM_ZONE_ENCRYPT";
			default:
				return "ERR_NVRAM_UNKOWN";
		}
	}
	else
	{
		return "";
	}
}

const char *nvram_version(void)
{
	if(flash_type() == NVRAM_FLASH_TYPE_FOPEN)
	{
		return NVRAM_LIB_VERSION"(fopen)";
	}
	else if(flash_type() == NVRAM_FLASH_TYPE_MYSQL)
	{
		return NVRAM_LIB_VERSION"(mysql)";
	}
	else
	{
		return NVRAM_LIB_VERSION"(unknown)";
	}
}

int nvram_init(char *zone)
{
	struct stat file_stat;
	char 		tmp_buf[256];
	int 		ret = 0;

	if(stat(DEFAULT_NVRAM_CONF_PATH,&file_stat) != 0)
	{//仅需要时才操作
		if(mkdir(DEFAULT_NVRAM_CONF_PATH,0777) == -1)
		{
			fprintf(stderr,"%s(mkdir(%s) err = %s)\n",__FUNCTION__,DEFAULT_NVRAM_CONF_PATH,strerror(errno));
		}
	}
	if(stat(DEFAULT_NVRAM_SEM_INIT_PATH,&file_stat) != 0)
	{//仅需要时才操作
		if(mkdir(DEFAULT_NVRAM_SEM_INIT_PATH,0777) == -1)
		{
			fprintf(stderr,"%s(mkdir(%s) err = %s)\n",__FUNCTION__,DEFAULT_NVRAM_SEM_INIT_PATH,strerror(errno));
		}
	}

	if(zone_tmp == NULL)
	{
		zone_tmp = malloc(sizeof(zone_s));
		if(zone_tmp == NULL)
		{
			return ENOMEM;
		}
		memset(zone_tmp,0,sizeof(zone_s));
	}

	snprintf(tmp_buf,sizeof(tmp_buf),DEFAULT_NVRAM_CONF_PATH""DEFAULT_NVRAM_SHMEM_NAME_HEAD"%s",zone);
	ret = stat(tmp_buf,&file_stat);
	if(ret == 0)
	{
		zone_s *zone_buf = NULL;
		ret = shmem_open(zone,0,(void *)&zone_buf,sizeof(zone_s));
		if(ret == 0)
		{
			if(zone_buf)
			{
				if(zone_buf->vaild != 1)
				{
					memset(zone_buf,0,sizeof(zone_s));
					ret = nvram_read_data(zone,zone_buf);//读入参数
					if(ret)
					{
						fprintf(stderr,"%s(nvram_read_data(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
					}
					zone_buf->vaild = 1;
					zone_buf->dirty = 0;
				}
			}
			shmem_close(zone,zone_buf);
		}
		else
		{
			fprintf(stderr,"%s(shmem_open(key = %s) = %s)\n",__FUNCTION__,zone,strerror(ret));
			//释放资源
			if(zone_tmp)
			{
				free(zone_tmp);
				zone_tmp = NULL;
			}
			return ret;
		}
	}
	else
	{
		fprintf(stderr,"%s(stat(file = %s) = %s)\n",__FUNCTION__,tmp_buf,strerror(errno));
		//释放资源
		if(zone_tmp)
		{
			free(zone_tmp);
			zone_tmp = NULL;
		}
		return errno;
	}
	return 0;
}

int nvram_close(char *zone)
{
	//释放资源
	if(zone_tmp)
	{
		free(zone_tmp);
		zone_tmp = NULL;
	}
	return 0;
}

int nvram_reload(char *zone)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		zone_s 	*zone_buf = NULL;
		ret = shmem_open(zone,0,(void *)&zone_buf,sizeof(zone_s));
		if(ret == 0)
		{
			if(zone_buf)
			{
				//这样会丢弃现有内存中的数据,从flash重新导入nvram数据
				zone_buf->vaild = 0;
				zone_buf->dirty = 0;
			}
			shmem_close_remove(zone,zone_buf);
		}
		else
		{
			fprintf(stderr,"%s(shmem_open(key = %s) = %s)\n",__FUNCTION__,zone,strerror(ret));
			return ret;
		}
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return 0;
}

const char *nvram_get(char *zone, char *name)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		zone_s 	*zone_buf = NULL;
		ret = shmem_open(zone,0,(void *)&zone_buf,sizeof(zone_s));
		if(ret == 0)
		{
			char	*ret_value = "";
			if(zone_buf)
			{
				int 	i;
				//读入数据
				for(i=0;i<DEFAULT_NVRAM_COUNT_MAX;i++)
				{
					if(strcmp(name,zone_buf->cache[i].name) == 0)
					{
						strncpy(zone_tmp->cache[i].value,zone_buf->cache[i].value,sizeof(zone_tmp->cache[i].value));
						zone_tmp->cache[i].value[sizeof(zone_tmp->cache[i].value) - 1] = '\0';
						ret_value = zone_tmp->cache[i].value;
						break;
					}
					//0.0.4 20160120 nvram_unset功能就无法实现
					else
					{
						if(zone_buf->cache[i].name[0] == '\0')
						{//这里假设0~(i-1)的name都是有内容的,而i这项刚好是有用数据的最末尾
							break;
						}
					}
					//0.0.4 20160120 nvram_unset功能就无法实现
				}
			}
			shmem_close(zone,zone_buf);
			//返回数据
			return ret_value;
		}
		else
		{
			fprintf(stderr,"%s(shmem_open(key = %s) = %s)\n",__FUNCTION__,zone,strerror(ret));
			return "";
		}
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return "";
	}
	return "";
}

int nvram_show(char *zone)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		zone_s 	*zone_buf = NULL;
		ret = shmem_open(zone,0,(void *)&zone_buf,sizeof(zone_s));
		if(ret == 0)
		{
			if(zone_buf)
			{
				int i;
				int cache_count=0;
				//打印数据
				for(i=0;i<DEFAULT_NVRAM_COUNT_MAX;i++)
				{
					if(strlen(zone_buf->cache[i].name))
					{
						fprintf(stdout,"%s=%s\n",zone_buf->cache[i].name,zone_buf->cache[i].value);
						cache_count++;
					}
				}
				fprintf(stdout,"cache count %d/%d\n",cache_count,DEFAULT_NVRAM_COUNT_MAX);
			}
			shmem_close(zone,zone_buf);
			return 0;
		}
		else
		{
			fprintf(stderr,"%s(shmem_open(key = %s) = %s)\n",__FUNCTION__,zone,strerror(ret));
			return ret;
		}
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return 0;
}

int nvram_clear(char *zone)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		zone_s	*zone_buf = NULL;
		ret = shmem_open(zone,0,(void *)&zone_buf,sizeof(zone_s));
		if(ret == 0)
		{
			if(zone_buf)
			{
				//清空数据
				int i;
				for(i=0;i<DEFAULT_NVRAM_COUNT_MAX;i++)
				{
					strcpy(zone_tmp->cache[i].name,"");
					strcpy(zone_tmp->cache[i].value,"");
					strcpy(zone_buf->cache[i].name,"");
					strcpy(zone_buf->cache[i].value,"");
				}
				zone_buf->dirty = 1;
			}
			shmem_close(zone,zone_buf);
			return 0;
		}
		else
		{
			fprintf(stderr,"%s(shmem_open(key = %s) = %s)\n",__FUNCTION__,zone,strerror(ret));
			return ret;
		}
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return 0;
}

int nvram_set(char *zone, char *name, char *value)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		zone_s	*zone_buf = NULL;
		ret = shmem_open(zone,0,(void *)&zone_buf,sizeof(zone_s));
		if(ret == 0)
		{
			int deal_done = 0;
			if(zone_buf)
			{
				//操作数据
				int i;
				for(i=0;i<DEFAULT_NVRAM_COUNT_MAX;i++)
				{
					if(strcmp(zone_buf->cache[i].name,name) == 0)
					{
						strncpy(zone_tmp->cache[i].value,value,sizeof(zone_tmp->cache[i].value));
						zone_tmp->cache[i].value[sizeof(zone_tmp->cache[i].value) - 1] = '\0';
						strncpy(zone_buf->cache[i].value,value,sizeof(zone_buf->cache[i].value));
						zone_buf->cache[i].value[sizeof(zone_buf->cache[i].value) - 1] = '\0';
						deal_done = 1;
						zone_buf->dirty = 1;
						break;
					}
				}
				if(deal_done == 0)
				{//如果没有这个name,则寻找一个空的cache
					for(i=0;i<DEFAULT_NVRAM_COUNT_MAX;i++)
					{
						if(strcmp(zone_buf->cache[i].name,"") == 0)
						{
							strncpy(zone_tmp->cache[i].name,name,sizeof(zone_tmp->cache[i].name));
							zone_tmp->cache[i].name[sizeof(zone_tmp->cache[i].name) - 1] = '\0';
							strncpy(zone_buf->cache[i].name,name,sizeof(zone_buf->cache[i].name));
							zone_buf->cache[i].name[sizeof(zone_buf->cache[i].name) - 1] = '\0';

							strncpy(zone_tmp->cache[i].value,value,sizeof(zone_tmp->cache[i].value));
							zone_tmp->cache[i].value[sizeof(zone_tmp->cache[i].value) - 1] = '\0';
							strncpy(zone_buf->cache[i].value,value,sizeof(zone_buf->cache[i].value));
							zone_buf->cache[i].value[sizeof(zone_buf->cache[i].value) - 1] = '\0';

							deal_done = 1;
							zone_buf->dirty = 1;
							break;
						}
					}
				}
			}
			//操作数据
			shmem_close(zone,zone_buf);
			if(deal_done == 0)
			{
				fprintf(stderr,"%s(zone(%s)room overflow)\n",__FUNCTION__,zone);
				return ERR_NVRAM_ZONE_OVERFLOW;
			}
			return 0;
		}
		else
		{
			fprintf(stderr,"%s(shmem_open(key = %s) = %s)\n",__FUNCTION__,zone,strerror(ret));
			return ret;
		}
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return 0;
}

int nvram_commit(char *zone)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		zone_s 	*zone_buf = NULL;
		ret = shmem_open(zone,0,(void *)&zone_buf,sizeof(zone_s));
		if(ret == 0)
		{
			if(zone_buf)
			{
				//保存参数
				//因为随时可能被reload操作将zone失效,失效时不能将参数写入flash
				if((zone_buf->dirty == 1) && (zone_buf->vaild == 1))
				{
					ret = nvram_write_data(zone,zone_buf);//写入参数
					if(ret)
					{
						fprintf(stderr,"%s(nvram_write_data(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
					}
					zone_buf->dirty = 0;
				}
			}
			shmem_close(zone,zone_buf);
		}
		else
		{
			fprintf(stderr,"%s(shmem_open(key = %s) = %s)\n",__FUNCTION__,zone,strerror(ret));
			return ret;
		}
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return 0;
}

