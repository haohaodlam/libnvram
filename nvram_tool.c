/*****************************************************************************************
��������	20140524
������		haohaodlam
��ǰ����	
��ǰ�汾	0.0.3 20151215
				1.����nvram_tool test������,����Ҫ�ָ�����ʱ,��clear,Ȼ��renew
				2.����nvram_tool commit����,���Ͻ������ڴ��е�����д��flash
				3.����nvram_tool reload����,�����ȱ�ʱ,ǿ���л������ļ�
				4.���Ӱ�����Ϣ�еİ汾�Ŵ�ӡ
			0.0.2 20150824
				1.����nvram_ramset����
				2.����nvram_tool_set(...char need_commit),need_commit����ָʾ�Ƿ����need_commit()
				3.����nvram_tool test����,���ڲ��������ļ��Ƿ��ʼ��
				4.����nvram_tool_clear()ִ����Ϻ����need_commit()�Ĳ���
				5.����nvram_tool_show()��nvram_close()����,��nvram_init�Գ�
			0.0.1 20140524
				1.��ʼ�汾
*****************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "nvram.h"

int nvram_tool_get_usage(char *aout)
{
	fprintf(stdout,"version:%s\n",nvram_version());
	fprintf(stdout,"Usage: \n");
	fprintf(stdout,"       nvram_get lan_ipaddr\n");
	fprintf(stdout,"       nvram_get %s lan_ipaddr\n",DEFAULT_NVRAM_ZONE_NAME);
	fprintf(stdout,"       nvram_get xxxxx lan_ipaddr\n");
	fprintf(stdout,"\n");
	return -1;
}

int nvram_tool_get(int argc, char *argv[])
{
	char *zone;
	char *key;
	int ret;
	//�����ж�
	if(argc == 2)
	{
		zone = DEFAULT_NVRAM_ZONE_NAME;
		key = argv[1];
	}
	else if(argc == 3)
	{
		zone = argv[1];
		key = argv[2];
	}
	else
	{
		return nvram_tool_get_usage(argv[0]);
	}

	ret = nvram_init(zone);
	if(ret == 0)
	{
		fprintf(stdout,"%s\n", nvram_get(zone, key));
		nvram_close(zone);
		return 0;
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s,key = %s) = %s)\n",__FUNCTION__,zone,key,nvram_error(ret));
		return ret;
	}
	return -1;
}

int nvram_tool_set_usage(char *aout,char need_commit)
{
	fprintf(stdout,"version:%s\n",nvram_version());
	fprintf(stdout,"Usage: \n");
	fprintf(stdout,"       nvram_%sset lan_ipaddr 192.168.1.1\n",(need_commit == 1)?"":"ram");
	fprintf(stdout,"       nvram_%sset %s lan_ipaddr 192.168.1.1\n",(need_commit == 1)?"":"ram",DEFAULT_NVRAM_ZONE_NAME);
	fprintf(stdout,"       nvram_%sset %s lan_name 'youer name'\n",(need_commit == 1)?"":"ram",DEFAULT_NVRAM_ZONE_NAME);
	fprintf(stdout,"       nvram_%sset xxxxx lan_ipaddr 192.168.1.1\n",(need_commit == 1)?"":"ram");
	fprintf(stdout,"\n");
	return -1;
}

int nvram_tool_set(int argc,char **argv,char need_commit)
{
	int ret = -1;
	char *zone;
	char *key;
	char *value;
	//�����ж�
	if (argc == 3)
	{
		zone  = DEFAULT_NVRAM_ZONE_NAME;
		key   = argv[1];
		value = argv[2];
	}
	else if (argc == 4)
	{
		zone  = argv[1];
		key   = argv[2];
		value = argv[3];
	}
	else
	{
		return nvram_tool_set_usage(argv[0],need_commit);
	}

	ret = nvram_init(zone);
	if(ret == 0)
	{
		ret = nvram_set(zone, key, value);
		if(need_commit == 1)nvram_commit(zone);
		nvram_close(zone);
		return 0;
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s,key = %s,value = %s) = %s)\n",__FUNCTION__,zone,key,value,nvram_error(ret));
		return ret;
	}
	return -1;
}

void nvram_tool_usage(char *cmd)
{
	fprintf(stdout,"version:%s\n",nvram_version());
	fprintf(stdout,"Usage:\n");
	fprintf(stdout,"  %s <command> [<zone>] [<file>]\n\n", cmd);
	fprintf(stdout,"command:\n");
	fprintf(stdout,"  show    - display values in nvram for <zone>\n");
	fprintf(stdout,"  renew   - replace nvram values for <zone> with <file>\n");
	fprintf(stdout,"  test    - if WebInit != 1 then replace nvram values for <zone> with <file>\n");
	fprintf(stdout,"  clear	  - clear all entries in nvram for <zone>\n");
	fprintf(stdout,"  commit  - commit all entries in nvram for <zone>\n");
	fprintf(stdout,"  reload  - reload all entries in nvram for <zone> form flash\n");
	fprintf(stdout,"zone:\n");
	fprintf(stdout,"  %s   - default\n",DEFAULT_NVRAM_ZONE_NAME);
	fprintf(stdout,"  any     - user define name\n");
	fprintf(stdout,"file:\n");
	fprintf(stdout,"          - file name for renew or test command\n");
	exit(-1);
}

int nvram_tool_renew(char *zone, char *fname)
{
	FILE 	*fp;
	char 	buf[1024];
	int 	found_default = 0;
	int     ret;

	if (NULL == (fp = fopen(fname, "ro")))
	{
		fprintf(stderr,"%s(fopen(%s) = %s)\n",__FUNCTION__,fname,strerror(errno));
		return errno;
	}

	//find "Default" first
	while (NULL != fgets(buf, sizeof(buf), fp))
	{
		if (!strncmp(buf, "Default\n", 8))
		{
			found_default = 1;
			break;
		}
	}

	if(!found_default)
	{
		fprintf(stderr,"%s(file(format error)) = can't find default head\n",__FUNCTION__);
		fclose(fp);
		return -1;
	}
	else
	{
		char *p = NULL;
		ret = nvram_init(zone);
		if(ret == 0)
		{
			while(NULL != fgets(buf, sizeof(buf), fp))
			{
				if(NULL != (p = strchr(buf, '=')))
				{
					buf[strlen(buf) - 1] = '\0';//remove carriage return
					*p = '\0';//seperate the string
					p++;
					nvram_set(zone, buf, p);
				}
			}
			nvram_commit(zone);
			nvram_close(zone);
			return 0;
		}
		else
		{
			fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
			return ret;
		}
		fclose(fp);
		return 0;
	}
	return -1;
}

int nvram_tool_clear(char *zone)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		nvram_clear(zone);
		nvram_commit(zone);
		nvram_close(zone);
		return 0;
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return -1;
}

int nvram_tool_commit(char *zone)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		nvram_commit(zone);
		nvram_close(zone);
		return 0;
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return -1;
}

int nvram_tool_reload(char *zone)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		nvram_reload(zone);
		nvram_close(zone);
		return 0;
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return -1;
}

int nvram_tool_show(char *zone)
{
	int ret;

	ret = nvram_init(zone);
	if(ret == 0)
	{
		nvram_show(zone);
		nvram_close(zone);
		return 0;
	}
	else
	{
		fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
		return ret;
	}
	return -1;
}

int main(int argc, char *argv[])
{
	char *cmd;

	if ((cmd = strrchr(argv[0], '/')) != NULL)
	{
		cmd++;
	}
	else
	{
		cmd = argv[0];
	}

	if (!strncmp(cmd, "nvram_get", 10))
	{//����nvram_get
		exit(nvram_tool_get(argc, argv));
	}
	else if (!strncmp(cmd, "nvram_set", 10))
	{//����nvram_set
		exit(nvram_tool_set(argc, argv,1));
	}
	else if (!strncmp(cmd, "nvram_ramset", 13))
	{//����nvram_ramset
		exit(nvram_tool_set(argc, argv,0));
	}
	else
	{//����nvram_tool
		if (argc < 2)
		{
			nvram_tool_usage(argv[0]);
			exit(0);
		}
		else if(argc == 3)
		{
			if (!strncasecmp(argv[1], "show", 5))
			{//����nvram_tool show
				exit(nvram_tool_show(argv[2]));
			}
			else if(!strncasecmp(argv[1], "clear", 6))
			{//����nvram_tool clear
				exit(nvram_tool_clear(argv[2]));
			}
			else if(!strncasecmp(argv[1], "commit", 7))
			{//����nvram_tool commit
				exit(nvram_tool_commit(argv[2]));
			}
			else if(!strncasecmp(argv[1], "reload", 7))
			{//����nvram_tool reload
				exit(nvram_tool_reload(argv[2]));
			}
			else
			{
				nvram_tool_usage(argv[0]);
				exit(-1);
			}
		}
		else if (argc == 4)
		{
			if (!strncasecmp(argv[1], "renew", 6))
			{//����nvram_tool renew
				exit(nvram_tool_renew(argv[2], argv[3]));
			}
			else if (!strncasecmp(argv[1], "test", 5))
			{
				int ret;
				char *zone = argv[2];
				char find_webInit_en = 1;
				ret = nvram_init(zone);
				if(ret == 0)
				{
					if(strcmp(nvram_get(zone, "WebInit"),"1") != 0)
					{//���WebInit��Ϊ1��ָ�����
						find_webInit_en = 0;
					}
					nvram_close(zone);
				}
				else
				{//�����ʼ��ʧ��,���ٶ�nvram����,��Ϊ��������������,��ʹ���лָ���������Ҳû������
					fprintf(stderr,"%s(nvram_init(zone = %s) = %s)\n",__FUNCTION__,zone,nvram_error(ret));
					nvram_tool_usage(argv[0]);
					exit(-1);
				}
				if(find_webInit_en == 0)
				{
					nvram_tool_clear(argv[2]);
					exit(nvram_tool_renew(argv[2], argv[3]));//����nvram_tool renew
				}
			}
			else
			{
				nvram_tool_usage(argv[0]);
				exit(-1);
			}
		}
		else
		{
			nvram_tool_usage(argv[0]);
			exit(-1);
		}
	}
	exit(-1);
}

