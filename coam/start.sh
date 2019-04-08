#!/bin/bash

export ORACLE_HOME=/u01/app/oracle/product/11.2.0/xe
export LD_LIBRARY_PATH=${ORACLE_HOME}/lib:${LD_LIBRARY_PATH}
COAM_DIR=/usr/local/etc/coam

/usr/local/bin/coam -f -p ${COAM_DIR}/coam.pid -d ${COAM_DIR}/conf
