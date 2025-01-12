#!/bin/bash
sudo ip tuntap add dev tap1 mode tap user $(whoami)
sudo ip link set tap1 up
sudo ip link set dev tap1 master br0
