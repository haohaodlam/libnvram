#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "nvram.h"
#include <mysql/mysql.h>

#define NVRAM_MYSQL_LAYOUT_VERSION		"0001"

#define MYSQL_ODBC_CONFIG_FILE			"/etc/odbc.ini"
#define DEFAULT_MYSQL_CONNECT_NAME		"root"
#define DEFAULT_MYSQL_CONNECT_PASSWD	"soc1000_db"
#define DEFAULT_MYSQL_CONNECT_DB_NAME	"freeswitch"

static void get_value_no_blankspace_tab(char *dst_value,char *src_value)
{//没有对内存越界做判断,恶意的配置文件,比如超过256字节长度的参数,将会导致内存越界
	//从src_value中提取非制表符和空格的字符串到dst_value,以回车,换行或字符串结束符结尾
	if(dst_value == NULL || src_value == NULL)
	{
		return;
	}
	int i=0,j=0;
	while((src_value[i] != '\0') && (src_value[i] != '\r') && (src_value[i] != '\n'))
	{
		if((src_value[i] != '\t') && (src_value[i] != ' '))
		{
			dst_value[j] = src_value[i];
			j++;
		}
		i++;
	}
}

static void read_mysql_config(char *Database,char *Server,char *UserName,char *Password,char *Port)
{
	char read_buf[256];
	if((Database == NULL) || (Server == NULL) || (UserName == NULL) || (Password == NULL) || (Port == NULL))
	{
		return;
	}

	FILE *fp = fopen(MYSQL_ODBC_CONFIG_FILE,"rb");
	if(fp)
	{
		while(fgets(read_buf,sizeof(read_buf),fp))
		{
			char *value = NULL;
			if(strstr(read_buf,"Database") && (value = strchr(read_buf,'=')))
			{
				get_value_no_blankspace_tab(Database,value + sizeof("="));
			}
			else if (strstr(read_buf,"Server") && (value = strchr(read_buf,'=')))
			{
				get_value_no_blankspace_tab(Server,value + sizeof("="));
			}
			else if (strstr(read_buf,"UserName") && (value = strchr(read_buf,'=')))
			{
				get_value_no_blankspace_tab(UserName,value + sizeof("="));
			}
			else if (strstr(read_buf,"Password") && (value = strchr(read_buf,'=')))
			{
				get_value_no_blankspace_tab(Password,value + sizeof("="));
			}
			else if (strstr(read_buf,"Port") && (value = strchr(read_buf,'=')))
			{
				get_value_no_blankspace_tab(Port,value + sizeof("="));
			}
		}
		fclose(fp);
		return;
	}
}

unsigned char flash_type(void)
{
	return NVRAM_FLASH_TYPE_MYSQL;
}

int flash_read(char *zone,char *read_buf,unsigned int read_buf_size,unsigned int *read_size,char bak_flag)
{
	char 		tmp_buf[256];
	int			ret = 0;

	if(zone == NULL || read_buf == NULL || read_size == NULL)
	{
		return EINVAL;
	}
	*read_size = 0;

    mysql_library_init(0, NULL, NULL);
    MYSQL *db = mysql_init(NULL);
    if (!db)
	{
		ret = errno;
	}
	else
	{//MSQL初始化数据成功
		char Database[256]={0};
		char Server[256]={0};
		char UserName[256]={0};
		char Password[256]={0};
		char Port[256]={0};

		read_mysql_config(Database,Server,UserName,Password,Port);
		if(Database[0] == '\0')
		{
			strcpy(Database,DEFAULT_MYSQL_CONNECT_DB_NAME);
		}
		if((Server[0] == '\0') || (strncmp(Server,"localhost",9) == 0))
		{
			strcpy(Server,"127.0.0.1");
		}
		if(UserName[0] == '\0')
		{
			strcpy(UserName,DEFAULT_MYSQL_CONNECT_NAME);
		}
		if(Password[0] == '\0')
		{
			strcpy(Password,DEFAULT_MYSQL_CONNECT_PASSWD);
		}
		//端口无需判断容错,为0时mysql_real_connect()会使用默认端口3306
		if(!mysql_real_connect(db, Server, UserName, Password, Database, atoi(Port), NULL, 0))
		{
			ret = mysql_errno(db);
		}
		else
		{//连接数据库ok
			snprintf(tmp_buf,sizeof(tmp_buf),"select value from nvram_data_%s where name = '"DEFAULT_NVRAM_DATA_NAME_HEAD"%s%s'",NVRAM_MYSQL_LAYOUT_VERSION,zone,(bak_flag == 1)?"_bak":"");
			ret = mysql_real_query(db,tmp_buf,strlen(tmp_buf));
			if(ret == 0)
			{//执行数据库查询语句ok
				MYSQL_RES *result = NULL;
				if (!(result = mysql_store_result(db)))
				{//找不到资源
					ret = mysql_errno(db);
				}
				else
				{//读取value
					MYSQL_ROW row = mysql_fetch_row(result);
					if(row)
					{
						unsigned long *row_len = mysql_fetch_lengths(result);
						if(row_len)
						{
							memcpy(read_buf, row[0], row_len[0]);
							*read_size = row_len[0];
							ret = 0;
						}
						else
						{
							ret = ENOENT;
						}
					}
					else
					{
						ret = ENOENT;
					}
					mysql_free_result(result);
				}
			}
			else
			{
				//返回ret
			}
		}
        mysql_close(db);
    }
    mysql_library_end();

	return ret;
}

int flash_write(char *zone,char *write_buf,unsigned int write_size,char bak_flag)
{
	int		ret = 0;
	char 	*tmp_buf = NULL;
	int 	sizeof_tmp_buf = write_size*2 + 2 + 256;//安全编码后两倍写入大小+安全编码补丁2字节+sql命令256字节

	if(zone == NULL || write_buf == NULL)
	{
		return EINVAL;
	}
	tmp_buf = (char *)malloc(sizeof_tmp_buf);
	if(tmp_buf == NULL)
	{
		return ENOMEM;
	}

    mysql_library_init(0, NULL, NULL);
    MYSQL *db = mysql_init(NULL);
    if (!db)
	{
		ret = errno;
	}
	else
	{//MSQL初始化数据成功
		char Database[256]={0};
		char Server[256]={0};
		char UserName[256]={0};
		char Password[256]={0};
		char Port[256]={0};

		read_mysql_config(Database,Server,UserName,Password,Port);
		if(Database[0] == '\0')
		{
			strcpy(Database,DEFAULT_MYSQL_CONNECT_DB_NAME);
		}
		if((Server[0] == '\0') || (strncmp(Server,"localhost",9) == 0))
		{
			strcpy(Server,"127.0.0.1");
		}
		if(UserName[0] == '\0')
		{
			strcpy(UserName,DEFAULT_MYSQL_CONNECT_NAME);
		}
		if(Password[0] == '\0')
		{
			strcpy(Password,DEFAULT_MYSQL_CONNECT_PASSWD);
		}
		//端口无需判断容错,为0时mysql_real_connect()会使用默认端口3306
		if(!mysql_real_connect(db, Server, UserName, Password, Database, atoi(Port), NULL, 0))

		{
			ret = mysql_errno(db);
		}
		else
		{//连接数据库ok
			//查看是否存在表nvram_data
			snprintf(tmp_buf,sizeof_tmp_buf,"create database if not exists %s",Database);
			ret = mysql_real_query(db,tmp_buf,strlen(tmp_buf));
			if(ret == 0)
			{//执行数据库表存在语句ok
				//查看是否存在表nvram_data
				snprintf(tmp_buf,sizeof_tmp_buf,"create table if not exists nvram_data_%s(name VARCHAR(255),value MEDIUMBLOB)",NVRAM_MYSQL_LAYOUT_VERSION);
				ret = mysql_real_query(db,tmp_buf,strlen(tmp_buf));
				if(ret == 0)
				{
					char has_nvram_data_zone = 0;
					//查看是否存在项DEFAULT_NVRAM_DATA_NAME_HEAD+zone
					snprintf(tmp_buf,sizeof_tmp_buf,"select value from nvram_data_%s where name = '"DEFAULT_NVRAM_DATA_NAME_HEAD"%s%s' limit 1",NVRAM_MYSQL_LAYOUT_VERSION,zone,(bak_flag == 1)?"_bak":"");
					ret = mysql_real_query(db,tmp_buf,strlen(tmp_buf));
					if(ret == 0)
					{
						MYSQL_RES *res;
						if((res = mysql_store_result(db)))
						{
							if(mysql_num_rows(res) > 0)
							{
								has_nvram_data_zone = 1;
							}
						}
					}
					//写入数据库
					unsigned int escape_size = write_size*2 + 2;
					char *escape_object = (char *)malloc(escape_size);
					if(escape_object == NULL)
					{
					   ret = ENOMEM;
					}
					else
					{
						if(mysql_real_escape_string(db, escape_object, (char *)write_buf, write_size))
						{
							if(has_nvram_data_zone == 1)
							{//更新数据
								snprintf(tmp_buf,sizeof_tmp_buf,"update nvram_data_%s set value = '%s' where name = '"DEFAULT_NVRAM_DATA_NAME_HEAD"%s%s'",NVRAM_MYSQL_LAYOUT_VERSION, escape_object,zone,(bak_flag == 1)?"_bak":"");
							}
							else
							{//插入数据
								snprintf(tmp_buf,sizeof_tmp_buf,"insert into nvram_data_%s values('"DEFAULT_NVRAM_DATA_NAME_HEAD"%s%s','%s')",NVRAM_MYSQL_LAYOUT_VERSION,zone,(bak_flag == 1)?"_bak":"",escape_object);
							}
							ret = mysql_real_query(db,tmp_buf,strlen(tmp_buf));
						}
						else
						{
							ret = ENOEXEC;
						}
						if(escape_object)free(escape_object);
					}
				}
				else
				{
					//返回ret
				}
			}
			else
			{
				//返回ret
			}
		}
        mysql_close(db);
    }
    mysql_library_end();

	if(tmp_buf)free(tmp_buf);
	return ret;
}

