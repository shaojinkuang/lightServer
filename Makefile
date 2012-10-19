 
INSTALL_DIR=~/run/lightServer

all:
	@cd ./src; make

clean:
	@cd ./src; make clean

install:
	make
	cp ./src/dsmp $(INSTALL_DIR)
	