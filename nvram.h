/*****************************************************************************************
创建日期	20140524
创建人		haohaodlam
当前问题	需要推敲,当调用nvram_reload()后,在nvram_get()中调用nvram_init()后读取参数前的时间点是否影响程序正常运作
当前版本	0.0.8 这一版本要改nvram_read_data和nvram_write_data到flash层去
			0.0.7 20190325
				1.修复文件模式下,当主配置文件写入失败时,不再写入备用配置文件,降低系统配置丢失的概率
				2.原先文件模式下fcloes后才操作fflush,但是这个时候fp指针已经被释放,fflush会报错
			0.0.6 20181210
				1.增加shmem_close_remove()以区分函数shmem_close()关闭共享内存的同时额外删除共享内存,为了共享内存扩容时用
				2.增加DEFAULT_NVRAM_COUNT_MAX从Makefile中导入,这样可以编译时配置最大nvram条目数
			0.0.5 20160414
				1.修复nvram_read_data()和nvram_write_data()中初始化秘钥时一个字节的内存溢出
				2.增加aesende.h中AESEncode()和AESDecode()返回值的定义
			0.0.4 20160120
				1.修复nvram_get()查找效率问题,判断到有效数据的末尾时直接返回""
				  这个修复导致nvram_unset功能就无法实现了
			0.0.3 20151215
				1.增加nvram_reload()函数,用于标记共享内存区域的数据无效
				  使得库强制从配置文件中导入数据,这样内存中的原有数据将被清空
				2.增加nvram_commit()函数中对共享内存数据有效性的检测
				  防止执行nvram_reload()后,重新导入参数前,将内存中的数据保存到flash中
				3.删除nvram_commit()中的srand()调用,移动到nvram_write_data中
				  这样更加合理,因为只有nvram_write_data()才调用rand()
				4.增加mysql接口,可以将数据存储到mysql中,目前仅支持freeswitch的数据库环境
				5.修复nvram_error()中errno的输出错误
				6.增加nvram_version()函数用于返回库版本号和flash类型,增加宏NVRAM_LIB_VERSION和NVRAM_FLASH_TYPE_XXX
			0.0.2 20150824
				1.修正0.0.1版打字错误
				2.删除nvram_close()函数中保存参数到flash的功能,如果需保存要在调用nvram_close()之前调用nvram_commit
				  这样修改的原因是为了nvram_tool中新增的命令nvram_ramset执行后仅更改内容不操作flash,用于命令行下快速操作大量nvram参数,
				  原先每次执行nvram_tool中的命令nvram_set都会保存flash,执行效率很低
				3.system(mkdir -p "")使用mkdir()替代
				4.删除nvram_clear()函数中保存参数到flash的功能,如果需保存要在调用nvram_clear()之后调用nvram_commit
				5.增加nvram_read_data和nvram_write_data,备份参数的操作,使用xxxx_bak文件备份参数
				  若原始文件被破坏,读入备用文件中的参数
			0.0.1 20140524
				1.初始版本
工程说明	nvram实现,用于路由器等,配置数据比较小规模的项目来实现项目数据的断电不丢失支持
			提供数据在flash中存储的加密支持
			提供linux管理工具nvram_tool,windows管理工具IADx00xCRx_conf_tool.exe(下一个新版本会支持)
			数据结构抽象
			enum nvram_errcode{
				0,
				ERR_NVRAM_ZONE_OVERFLOW=0x80000001,
				ERR_NVRAM_CONF_HEAD_MAGIC=0x80000002,
				ERR_NVRAM_CONF_DATA_LEN=0x80000003,
				ERR_NVRAM_ZONE_CRYPT_VERSION=0x80000004,
				ERR_NVRAM_ZONE_DECRYPT=0x80000005,
				ERR_NVRAM_ZONE_ENCRYPT=0x80000006
			}nvram_errcode;

			class zone
			{
			public:
				nvram_errcode nvram_init(char *zone)	// 构造函数
				nvram_errcode nvram_close(char *zone)	// 析构函数
			//静态方法
				const char *nvram_error(nvram_errcode ret);
				const char *nvram_version(void);
			//方法
				nvram_errcode nvram_reload(char *zone);
				const char *nvram_get(char *zone, char *name);
				nvram_errcode nvram_show(char *zone);
				nvram_errcode nvram_set(char *zone, char *name, char *value);
				nvram_errcode nvram_clear(char *zone);
				nvram_errcode nvram_commit(char *zone);
			};
工程来源	基于rt2880中的nvram架构开发,工作于linux系统用户态,使用ipc实现进程间的数据共享,信号量保护临界数据
工程注意	nvram属于共享资源,请注意相关文件的访问权限
			库会读写操作DEFAULT_NVRAM_CONF_PATH目录和该目录下的nvram_data_xxxx文件,需要事先设置文件全局访问权限
*****************************************************************************************/
#ifndef _NVRAM_H
#define _NVRAM_H 	1

#define NVRAM_LIB_VERSION					"0.0.7"

//#define DEFAULT_NVRAM_CONF_PATH 			$(ROOT_PATH)"/etc/nvram/"	//系统需要该目录断电可以保存,用于共享内存key和nvram数据存储
															//它由外部Makefile导入-DDEFAULT_NVRAM_CONF_PATH=\"$(ROOT_PATH)etc/nvram/\"
#define DEFAULT_NVRAM_SHMEM_NAME_HEAD 		"nvram_shmem_"
#define DEFAULT_NVRAM_DATA_NAME_HEAD		"nvram_data_"	//仅用于nvram_flash_utils_fopen.c

//#define DEFAULT_NVRAM_SEM_INIT_PATH 		"/run/nvram/"	//需要该目录重启后清空该文件夹,用于信号量是否初始化的标记文件
															//它由外部Makefile导入-DDEFAULT_NVRAM_SEM_INIT_PATH=\"/run/nvram/\"
#define DEFAULT_NVRAM_SEM_INIT_NAME_HEAD	"nvram_sem_init_"

#define DEFAULT_NVRAM_ZONE_NAME 			"route"

#define ERR_NVRAM_ZONE_OVERFLOW				0x80000001
#define ERR_NVRAM_CONF_HEAD_MAGIC			0x80000002
#define ERR_NVRAM_CONF_DATA_LEN				0x80000003
#define ERR_NVRAM_ZONE_CRYPT_VERSION		0x80000004
#define ERR_NVRAM_ZONE_DECRYPT				0x80000005
#define ERR_NVRAM_ZONE_ENCRYPT				0x80000006

#define NVRAM_FLASH_TYPE_UNKOWN					0x00
#define NVRAM_FLASH_TYPE_FOPEN					0x01
#define NVRAM_FLASH_TYPE_MYSQL					0x02

/*****************************************************************************************
函数参数:	int ret			nvram库函数所返回的错误值
函数功能:	nvram库辅助函数,返回nvram库函数的所有错误代码的意义
函数返回:	返回字符串指针
函数备注:	大于0的值,定义为errno
			小于0的值,定义为ERR_NVRAM_XXXX_XXXX宏所指向的意义
			等于0的值,定义为""
*****************************************************************************************/
const char *nvram_error(int ret);

/*****************************************************************************************
函数参数:	无
函数功能:	nvram库版本号读取函数,返回nvram库函数的版本
函数返回:	返回字符串指针
函数备注:
*****************************************************************************************/
const char *nvram_version(void);

/*****************************************************************************************
函数参数:	char *zone		nvram区域名
函数功能:	nvram库管理,初始化zone所指向的nvram区域的内存和相关资源
函数返回:	返回值由nvram_error()定义
函数备注:	在调用所有nvram库函数前,先调用该函数
			虽然所有的库函数内部都会调用nvram_init来检测nvram是否初始化
			但是这里推荐,在使用nvram库前,先调用一次该函数来判断文件系统的环境是否满足运行要求
*****************************************************************************************/
int nvram_init(char *zone);

/*****************************************************************************************
函数参数:	char *zone		nvram区域名
函数功能:	nvram库管理和flash写入函数,释放所有占用的资源
函数返回:	返回值由nvram_error()定义
函数备注:	调用该函数后,你必须要再次调用nvram_init()才能再次使用nvram库
			释放的资源仅适用于运行进程,共享内存和信号量资源不释放,因为其他进程可能还在使用nvram
*****************************************************************************************/
int nvram_close(char *zone);

/*****************************************************************************************
函数参数:	char *zone		nvram区域名
			char *name		请求的变量名称
函数功能:	nvram库内存读取函数,返回指定区中指定变量名的值
函数返回:	返回字符串指针
函数备注:	当变量名不存在时,函数会返回"",本来应该返回NULL,告知是否存在该变量
			返回""的目的是,预防粗心的程序员忘记判断返回值,导致段错误
			返回字符串的内存空间,在你调用nvram_close()前一直会有效
			但是这里不允许你改写返回空间的内存
*****************************************************************************************/
const char *nvram_get(char *zone, char *name);
#define nvram_safe_get(X,Y) (nvram_get((X),(Y)))

/*****************************************************************************************
函数参数:	char *zone		nvram区域名
函数功能:	nvram库读取函数,返回指定区中所有变量的值和有效变量的数量
			该函数是唯一一个会向标准输出流输出内容的函数
函数返回:	返回值由nvram_error()定义
函数备注:	
*****************************************************************************************/
int nvram_show(char *zone);

/*****************************************************************************************
函数参数:	char *zone		nvram区域名
			char *name		请求的变量名称
			char *value		请求的变量值
函数功能:	nvram库内存写入函数,向指定区中指定变量名写入指定值
			改写的数据存在于内存中,如果要断电保存需要调用一次nvram_commit()
函数返回:	返回值由nvram_error()定义
函数备注:	函数会尝试查找是否存在变量名,查找算法是从index 0开始查找,这算法存在效率问题
			问题初步暴露于nvram_tool renew [zone] [path]命令
			针对renew的改良方法是,保存上一次的index,这样可以最大优化renew的效率
			但是还是无法解决大量的随机nvram_set的查找效率
*****************************************************************************************/
int nvram_set(char *zone, char *name, char *value);

/*****************************************************************************************
函数参数:	char *zone		nvram区域名
函数功能:	nvram库写入函数,清空所有值
			改写的数据存在于内存中,如果要断电保存需要调用一次nvram_commit()
函数返回:	返回值由nvram_error()定义
函数备注:	
*****************************************************************************************/
int nvram_clear(char *zone);

/*****************************************************************************************
函数参数:	char *zone		nvram区域名
函数功能:	nvram库flash写入函数,如果内存中的值被修改过,将内存中所有的值写入flash
函数返回:	返回值由nvram_error()定义
函数备注:	当你需要断电保存写入内存的数据时,调用该函数
			因为写入flash速度有一定延时,你需要考虑调用该函数的进程独占
			推荐大量数据操作后,调用该函数
*****************************************************************************************/
int nvram_commit(char *zone);

/*****************************************************************************************
函数参数:	char *zone		nvram区域名
函数功能:	nvram库flash重读函数,如果内存中的值被修改过,将会丢弃,重新从文件中导入数据
函数返回:	返回值由nvram_error()定义
函数备注:	当你需要强制从文件导入数据时,调用该函数,因为是强制导入,原有内存中的参数将被丢弃
			用于热备时,强制切换配置文件
*****************************************************************************************/
int nvram_reload(char *zone);

#endif
