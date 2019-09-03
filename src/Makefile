ORACLE_HOME	= /orahome/app/oracle/product/11.2.0/dbhome_1
DEBUG_OPT	= -g -DDEBUG
RELEASE_OPT	= -O2
OCI_L		= -L$(ORACLE_HOME)/lib/
LIB			= -lpthread -lclntsh
OCI_H		= -I$(ORACLE_HOME)/rdbms/public
UTL_DIR		= $(HOME)/project/utils/src
UTL_H		= -I $(UTL_DIR)

fast: coam

rebuild: clean coam

release:
	export CXXFALGS=$(RELEASE_OPT) && $(MAKE) -f Makefile rebuild

debug:
	export CXXFALGS=$(DEBUG_OPT) && $(MAKE) -f Makefile rebuild

coam: ./obj/main.o ./obj/coam.o ./obj/dboperation.o ./obj/threadoperation.o ./obj/log.o ./obj/pspacket.o ./obj/config.o ./obj/ipconnector.o ./obj/timemeasurer.o ./obj/dbpool.o
	g++ ./obj/main.o ./obj/coam.o ./obj/dboperation.o ./obj/threadoperation.o ./obj/log.o ./obj/pspacket.o ./obj/config.o ./obj/ipconnector.o ./obj/timemeasurer.o ./obj/dbpool.o -o ../coam $(OCI_L) $(LIB)
./obj/main.o: ./deamon/main.cpp ./deamon/main.h
	g++ --std=c++0x -c ./deamon/main.cpp $(OCI_H) -o ./obj/main.o $(UTL_H)
./obj/coam.o: ./manager/coam.cpp ./manager/coam.h
	g++ --std=c++0x -c ./manager/coam.cpp $(OCI_H) -o ./obj/coam.o $(UTL_H)
./obj/dboperation.o: ./manager/dboperation.cpp ./manager/coam.h
	g++ --std=c++0x -c ./manager/dboperation.cpp $(OCI_H) -o ./obj/dboperation.o $(UTL_H)
./obj/threadoperation.o: ./manager/threadoperation.cpp ./manager/coam.h
	g++ --std=c++0x -c ./manager/threadoperation.cpp $(OCI_H) -o ./obj/threadoperation.o $(UTL_H)
./obj/log.o: $(UTL_DIR)/utils/log/log.cpp $(UTL_DIR)/utils/log/log.h
	g++ --std=c++0x -c $(UTL_DIR)/utils/log/log.cpp -o ./obj/log.o
./obj/pspacket.o: $(UTL_DIR)/utils/pspacket/pspacket.cpp $(UTL_DIR)/utils/pspacket/pspacket.h
	g++ --std=c++0x -c $(UTL_DIR)/utils/pspacket/pspacket.cpp -o ./obj/pspacket.o $(UTL_H)
./obj/config.o: $(UTL_DIR)/utils/config/config.cpp $(UTL_DIR)/utils/config/config.h
	g++ --std=c++0x -c $(UTL_DIR)/utils/config/config.cpp -o ./obj/config.o
./obj/ipconnector.o: $(UTL_DIR)/utils/ipconnector/ipconnector.cpp $(UTL_DIR)/utils/ipconnector/ipconnector.h
	g++ --std=c++0x -c $(UTL_DIR)/utils/ipconnector/ipconnector.cpp -o ./obj/ipconnector.o
./obj/timemeasurer.o: $(UTL_DIR)/utils/timemeasurer/timemeasurer.h $(UTL_DIR)/utils/timemeasurer/timemeasurer.cpp
	g++ --std=c++0x -c $(UTL_DIR)/utils/timemeasurer/timemeasurer.cpp -o ./obj/timemeasurer.o
./obj/dbpool.o: $(UTL_DIR)/utils/dbpool/dbpool.h $(UTL_DIR)/utils/dbpool/dbpool.cpp
	g++ --std=c++0x -c $(UTL_DIR)/utils/dbpool/dbpool.cpp -o ./obj/dbpool.o $(UTL_H) $(OCI_H)

install:
	cp -f ../coam /usr/local/bin/coam

clean:
	rm -f ./obj/*.o ../coam