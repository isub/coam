ORACLE_HOME=/u01/app/oracle/product/11.2.0/db_1
OCI_L=-L$(ORACLE_HOME)/lib/
LIB=-lpthread -lclntsh
OCI_H=-I$(ORACLE_HOME)/rdbms/public
UTL_DIR=../../utils

all: clean coas

fast: coam

coam: ./obj/main.o ./obj/coam.o ./obj/DBOperation.o ./obj/ThreadOperation.o ./obj/Log.o ./obj/PSPacket.o ./obj/Config.o ./obj/IPConnector.o ./obj/TimeMeasurer.o
	g++ ./obj/main.o ./obj/coam.o ./obj/DBOperation.o ./obj/ThreadOperation.o ./obj/Log.o ./obj/PSPacket.o ./obj/Config.o ./obj/IPConnector.o ./obj/TimeMeasurer.o -o ../coam $(OCI_L) $(LIB)
./obj/main.o: ./Deamon/main.cpp ./Deamon/main.h
	g++ -c ./Deamon/main.cpp $(OCI_H) -o ./obj/main.o
./obj/coam.o: ./Manager/coam.cpp ./Manager/coam.h
	g++ -c ./Manager/coam.cpp $(OCI_H) -o ./obj/coam.o
./obj/DBOperation.o: ./Manager/DBOperation.cpp ./Manager/coam.h
	g++ -c ./Manager/DBOperation.cpp $(OCI_H) -o ./obj/DBOperation.o
./obj/ThreadOperation.o: ./Manager/ThreadOperation.cpp ./Manager/coam.h
	g++ -c ./Manager/ThreadOperation.cpp $(OCI_H) -o ./obj/ThreadOperation.o
./obj/Log.o: $(UTL_DIR)/log/Log.cpp $(UTL_DIR)/log/Log.h
	g++ -c $(UTL_DIR)/log/Log.cpp -o ./obj/Log.o
./obj/PSPacket.o: $(UTL_DIR)/pspacket/PSPacket.cpp $(UTL_DIR)/pspacket/PSPacket.h
	g++ -c $(UTL_DIR)/pspacket/PSPacket.cpp -o ./obj/PSPacket.o
./obj/Config.o: $(UTL_DIR)/config/Config.cpp $(UTL_DIR)/config/Config.h
	g++ -c $(UTL_DIR)/config/Config.cpp -o ./obj/Config.o
./obj/IPConnector.o: $(UTL_DIR)/ipconnector/IPConnector.cpp $(UTL_DIR)/ipconnector/IPConnector.h
	g++ -c $(UTL_DIR)/ipconnector/IPConnector.cpp -o ./obj/IPConnector.o
./obj/TimeMeasurer.o: $(UTL_DIR)/timemeasurer/TimeMeasurer.h $(UTL_DIR)/timemeasurer/TimeMeasurer.cpp
	g++ -c $(UTL_DIR)/timemeasurer/TimeMeasurer.cpp -o ./obj/TimeMeasurer.o

install:
	cp -f ../coam /usr/local/bin/coam

clean:
	rm -f ./obj/*.o ../coas