CC      = $(CROSS)gcc
AR      = $(CROSS)ar
STRIP   = $(CROSS)strip
RANLIB  = $(CROSS)ranlib
LDFLAGS = $(EXLDFLAGS)
CFLAGS	= -O2 -Wall -DDEFAULT_NVRAM_SEM_INIT_PATH=\"$(DEFAULT_NVRAM_SEM_INIT_PATH)\" -DDEFAULT_NVRAM_CONF_PATH=\"$(ROOT_PATH)etc/nvram/\" -DDEFAULT_NVRAM_COUNT_MAX=$(DEFAULT_NVRAM_COUNT_MAX) $(EXCFLAGS)

ifeq ($(NVRAM_FLASH),mysql)
NVRAM_FLASH_UTILS = nvram_flash_utils_mysql.o
else
NVRAM_FLASH_UTILS = nvram_flash_utils_fopen.o
endif

EXEC = libnvram.a nvram_tool

all: $(EXEC)
#libnvram.a: nvram_lib.o nvram.h aesende.o aesenc.o aes.o nvram_flash_utils_mtd_open.o aesende.h
libnvram.a: nvram_lib.o nvram.h aesende.o aesenc.o aes.o $(NVRAM_FLASH_UTILS) aesende.h
	$(AR) rc libnvram.a nvram_lib.o aesende.o aesenc.o aes.o $(NVRAM_FLASH_UTILS)
	$(RANLIB) libnvram.a

nvram_tool: nvram_tool.o libnvram.a
	$(CC) $(CFLAGS) nvram_tool.o libnvram.a -o $@ $(LDFLAGS)
	$(STRIP) nvram_tool

clean:
	-rm -f $(EXEC) *.elf *.gdb *.o

install:
	mkdir -p $(INSTALL_ETC_PATH)/nvram/
	chmod 777 $(INSTALL_ETC_PATH)/nvram/
	mkdir -p $(DEFAULT_NVRAM_SEM_INIT_PATH)
	touch $(INSTALL_ETC_PATH)/nvram/nvram_shmem_route
	chmod 644 $(INSTALL_ETC_PATH)/nvram/nvram_shmem_route
	mkdir -p $(INSTALL_EXEC_PATH)
	cp nvram_tool $(INSTALL_EXEC_PATH)/nvram_tool
	chmod 755 $(INSTALL_EXEC_PATH)/nvram_tool
	-rm $(INSTALL_EXEC_PATH)/nvram_get
	-rm $(INSTALL_EXEC_PATH)/nvram_set
	-rm $(INSTALL_EXEC_PATH)/nvram_ramset
	ln -s  nvram_tool $(INSTALL_EXEC_PATH)/nvram_get
	ln -s  nvram_tool $(INSTALL_EXEC_PATH)/nvram_set
	ln -s  nvram_tool $(INSTALL_EXEC_PATH)/nvram_ramset
