#!/bin/sh

# This script was develped for Ubuntu 12.04

i='sudo apt-get install -y'

# install developer tools

$i make
$i g++
$i makedepend || $i xutils-dev
$i flex bison
$i liblapack-dev libatlas-dev
$i libatlas-dev
$i freeglut3-dev
$i python-numpy python-scipy python-matplotlib python-dev

