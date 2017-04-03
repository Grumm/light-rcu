
OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

CC = gcc
CFLAGS += -O2 -Wall -Wextra --std=gnu99
LDFLAGS +=
LIBS += -lpthread
TARGET = test1

default: $(TARGET)
all: default

%.o: %.c $(HEADERS)
     $(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
    $(CC) $(CFLAGS) $(OBJECTS) $(LIBS) -o $@ $(LDFLAGS)

clean:
    -rm -f *.o
    -rm -f $(TARGET)
