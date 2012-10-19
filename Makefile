 
INSTALL_DIR=~/run/lightServer

all:
	@cd ./common; make
	@cd ./src; make

clean:
	@cd ./common; make clean
	@cd ./src; make clean

install:
	make
	cp ./src/dsmp $(INSTALL_DIR)
	