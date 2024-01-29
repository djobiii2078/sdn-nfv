#!/bin/bash

net_int_2="enp0s8"

mac_int_2="00:08.0"

#Mise a jour des paquets existants
sudo apt update && sudo apt upgrade -y

#Installation des paquets et des modules necessaires pour DPDK
sudo apt install -y wget
sudo apt install -y build-essential
sudo apt install -y python3
sudo apt install -y pkgconf
sudo apt install -y libnuma-dev
sudo apt install -y meson
sudo apt install -y python3-pyelftools

# Creer le repertoire temporaire
mkdir -p $(pwd)/tmp

cd $(pwd)/tmp

# Telecharger DPDK
wget http://fast.dpdk.org/rel/dpdk-23.11.tar.xz

tar xf dpdk-23.11.tar.xz

cd $(pwd)/dpdk-23.11

# Compiler DPDK avec meson et ninja
meson -Dexamples=all build
ninja -C build

cd $(pwd)/build

sudo ninja install
sudo ldconfig

ip link set dev $net_int_2 down

sudo modprobe uio
sudo modprobe uio_pci_generic

sudo dpdk-devbind.py -b uio_pci_generic $mac_int_2

# Configurer des hugepages
sudo mkdir -p /dev/hugepages
sudo mountpoint -q /dev/hugepages || sudo mount -t hugetlbfs nodev /dev/hugepages
echo 4096 | sudo tee /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages

sudo examples/dpdk-helloworld -l 0-1 -n 4

sudo app/dpdk-testpmd -l 0-1 -n 4 -- -i --portmask=0x1 --total-num-mbufs=2048 --mbuf-size=2048
