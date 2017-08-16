
LRCU_DIR = ../../src
OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c)) $(LRCU_DIR)/lrcu.c
HEADERS = $(wildcard *.h) $(wildcard $(LRCU_DIR)/*.h)

CC = gcc
CFLAGS += -I$(LRCU_DIR) -g -O3 -Wall -Wextra --std=gnu99 -Werror
LDFLAGS += -flto
LIBS += -lpthread
TARGET = simple-api-1

default: $(TARGET)
all: default

%.o: %.c $(HEADERS)
	 $(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	echo $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) $(LIBS) -o $@ $(LDFLAGS)

clean:
	-rm -f *.o
	-rm -f $(TARGET)
