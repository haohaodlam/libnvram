#!/bin/sh

mkdir -p /etc/nvram/
chmod 777 /etc/nvram/
mkdir -p /tmp/nvram/
touch /etc/nvram/nvram_shmem_route
chmod 644 /etc/nvram/nvram_shmem_route

mkdir -p /usr/local/sbin/
cp nvram_tool /usr/local/sbin/nvram_tool
cp nvram_data_route_default /etc/nvram/nvram_data_route_default

chmod 755 /usr/local/sbin/nvram_tool
rm /usr/local/sbin/nvram_get
rm /usr/local/sbin/nvram_set
rm /usr/local/sbin/nvram_ramset
ln -s  /usr/local/sbin/nvram_tool /usr/local/sbin/nvram_get
ln -s  /usr/local/sbin/nvram_tool /usr/local/sbin/nvram_set
ln -s  /usr/local/sbin/nvram_tool /usr/local/sbin/nvram_ramset

nvram_tool test route /etc/nvram/nvram_data_route_default

chmod 666 /etc/nvram/nvram_data_route
chmod 666 /etc/nvram/nvram_data_route_bak
