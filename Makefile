 
INSTALL_DIR=~/run/lightServer/bin

all:
	@cd ./common; make
	@cd ./src; make

clean:
	@cd ./common; make clean
	@cd ./src; make clean

install:
	make
	cp ./src/lightSvr $(INSTALL_DIR)
	