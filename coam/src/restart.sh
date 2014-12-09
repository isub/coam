#!/bin/bash

DIR=/usr/local/etc/coam

PID=`cat ${DIR}/coam.pid`

echo kill process ${PID}

sudo kill -s sigterm ${PID}

echo Wait for process ${PID} termination

while [ -e /proc/$PID ]
  do
    sleep 1
done

echo Done
echo start new process

sudo ${DIR}/start.sh

