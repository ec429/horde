#!/bin/bash

sudo ./horde --root=/var/www --port=80 --host=`hostname` --uid=`id -u horde` --gid=`id -g horde`
