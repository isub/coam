OCI_L		= -L$(ORACLE_HOME)/lib/
LIB			= -lpthread -lclntsh
OCI_H		= -I$(ORACLE_HOME)/rdbms/public
UTL_DIR		= ~/project/utils/src
UTL_H		= -I $(UTL_DIR)

all: clean coam

fast: coam

coam: ./obj/main.o ./obj/coam.o ./obj/dboperation.o ./obj/threadoperation.o ./obj/log.o ./obj/pspacket.o ./obj/config.o ./obj/ipconnector.o ./obj/timemeasurer.o ./obj/dbpool.o
	g++ ./obj/main.o ./obj/coam.o ./obj/dboperation.o ./obj/threadoperation.o ./obj/log.o ./obj/pspacket.o ./obj/config.o ./obj/ipconnector.o ./obj/timemeasurer.o ./obj/dbpool.o -o ../coam $(OCI_L) $(LIB)
./obj/main.o: ./deamon/main.cpp ./deamon/main.h
	g++ -c ./deamon/main.cpp $(OCI_H) -o ./obj/main.o $(UTL_H)
./obj/coam.o: ./manager/coam.cpp ./manager/coam.h
	g++ -c ./manager/coam.cpp $(OCI_H) -o ./obj/coam.o $(UTL_H)
./obj/dboperation.o: ./manager/dboperation.cpp ./manager/coam.h
	g++ -c ./manager/dboperation.cpp $(OCI_H) -o ./obj/dboperation.o $(UTL_H)
./obj/threadoperation.o: ./manager/threadoperation.cpp ./manager/coam.h
	g++ -c ./manager/threadoperation.cpp $(OCI_H) -o ./obj/threadoperation.o $(UTL_H)
./obj/log.o: $(UTL_DIR)/utils/log/log.cpp $(UTL_DIR)/utils/log/log.h
	g++ -c $(UTL_DIR)/utils/log/log.cpp -o ./obj/log.o
./obj/pspacket.o: $(UTL_DIR)/utils/pspacket/pspacket.cpp $(UTL_DIR)/utils/pspacket/pspacket.h
	g++ -c $(UTL_DIR)/utils/pspacket/pspacket.cpp -o ./obj/pspacket.o $(UTL_H)
./obj/config.o: $(UTL_DIR)/utils/config/config.cpp $(UTL_DIR)/utils/config/config.h
	g++ -c $(UTL_DIR)/utils/config/config.cpp -o ./obj/config.o
./obj/ipconnector.o: $(UTL_DIR)/utils/ipconnector/ipconnector.cpp $(UTL_DIR)/utils/ipconnector/ipconnector.h
	g++ -c $(UTL_DIR)/utils/ipconnector/ipconnector.cpp -o ./obj/ipconnector.o
./obj/timemeasurer.o: $(UTL_DIR)/utils/timemeasurer/timemeasurer.h $(UTL_DIR)/utils/timemeasurer/timemeasurer.cpp
	g++ -c $(UTL_DIR)/utils/timemeasurer/timemeasurer.cpp -o ./obj/timemeasurer.o
./obj/dbpool.o: $(UTL_DIR)/utils/dbpool/dbpool.h $(UTL_DIR)/utils/dbpool/dbpool.cpp
	g++ -c $(UTL_DIR)/utils/dbpool/dbpool.cpp -o ./obj/dbpool.o $(UTL_H) $(OCI_H)

install:
	cp -f ../coam /usr/local/bin/coam

clean:
	rm -f ./obj/*.o ../coam