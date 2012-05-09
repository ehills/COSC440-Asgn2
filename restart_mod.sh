#!/bin/bash

sudo rmmod $1
echo Removing $1...
sudo insmod ./$1.ko
echo Installed $1.ko...
dmesg | tail -20
