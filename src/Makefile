#ccache,fast C/C++ compiler cache,  if ccache installed, .bash_profile shuld add "export CCACHE=ccache"
GXX = $(CCACHE) g++
CC = $(CCACHE) gcc
EVENT_HOME = ${HOME}/libevent

CCFLAGS = -g -D__LINUX__ -I${EVENT_HOME}/include -I../include/ \
          -I./include
LIB = -L${EVENT_HOME}/lib \
	-levent -lpthread -lcrypt \
	-luuid -lrt -lcrypto -lmemcached
SOURCE := $(wildcard *.c)
OBJECT := $(subst .c,.o,${SOURCE})
SRC := $(wildcard *.cpp)
OBJ := $(SRC:.cpp=.o)
ALIBS=
TARGET = lightSvr

all : ${TARGET}

${TARGET} : ${OBJECT} $(OBJ)
	$(GXX) $(CCFLAGS) -Wall -o $(TARGET) $(OBJECT) $(OBJ) ${LIB}

${OBJECT} : %.o: %.c
	$(GXX)  $(CCFLAGS) -c $< -o $@

${OBJ} : %.o: %.cpp
	$(GXX)  $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET)
	rm -f $(OBJECT)
	rm -f $(OBJ)
		
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
