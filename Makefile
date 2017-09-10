
ROOT_DIR = $(PWD)
LRCU_DIR = $(ROOT_DIR)/src
INCLUDE_DIR = $(ROOT_DIR)/include
EXAMPLES_DIR = $(ROOT_DIR)/examples
TESTS_DIR = $(ROOT_DIR)/tests
KERNEL_DIR = /home/grumm/devel/linux-4.9.45/

OBJECTS_LIB = $(patsubst %.c, %.o, $(wildcard $(LRCU_DIR)/*.c))
HEADERS_LIB = $(wildcard $(LRCU_DIR)/*.h) $(wildcard $(INCLUDE_DIR)/lrcu/*.h)
TARGET_LIB = liblightrcu.a

TARGET_EX1_DIR = $(EXAMPLES_DIR)/simple-api-1
TARGET_EX1 = $(TARGET_EX1_DIR)/example-1
HEADERS_EX1 = $(wildcard $(TARGET_EX1_DIR)/*.h)
OBJECTS_EX1 = $(patsubst %.c, %.o, $(wildcard $(TARGET_EX1_DIR)/*.c))

TARGET_EX2_DIR = $(EXAMPLES_DIR)/linux-module
TARGET_EX2 = $(TARGET_EX2_DIR)/cross-platform
HEADERS_EX2 = $(wildcard $(TARGET_EX2_DIR)/*.h)
OBJECTS_EX2 = $(patsubst %.c, %.o, $(wildcard $(TARGET_EX2_DIR)/*.c))

TARGET_EX = $(TARGET_EX1) $(TARGET_EX2)

HEADERS_ALL = $(HEADERS_LIB) $(HEADERS_EX1)

CC = cc
CFLAGS += -I$(INCLUDE_DIR) -L$(ROOT_DIR)
CFLAGS +=  --std=gnu99 -g -O3 -Wall -Wextra -Werror
LDFLAGS += -flto
AR = ar
ARFLAGS = rcs

LIBS += -lpthread
LIB_TARGET += -llightrcu

all: lib examples
default: all

%.o: %.c $(HEADERS_ALL)
	 $(CC) $(CFLAGS) -c $< -o $@

$(TARGET_LIB): $(OBJECTS_LIB)
	$(AR) $(ARFLAGS) $(TARGET_LIB) $(OBJECTS_LIB)

$(TARGET_EX1): $(OBJECTS_EX1)
	$(CC) $(CFLAGS) $(OBJECTS_EX1) $(LIBS) $(LIB_TARGET) -o \
		$(TARGET_EX1) $(LDFLAGS)

$(TARGET_EX2): $(OBJECTS_EX2)
	$(CC) $(CFLAGS) $(OBJECTS_EX2) $(LIBS) $(LIB_TARGET) -o \
		$(TARGET_EX2) $(LDFLAGS)

examples: $(TARGET_EX)

lib: $(TARGET_LIB)

kernel_module:
	make -C $(KERNEL_DIR) M=$(LRCU_DIR) CONFIG_LRCU=m

kernel_example:
	make -C $(KERNEL_DIR) M=$(TARGET_EX2_DIR) #-I$(INCLUDE_DIR)

linux: kernel_module kernel_example

clean:
	-rm -f $(OBJECTS_LIB)
	-rm -f $(TARGET_LIB)
	-rm -f $(OBJECTS_EX1)
	-rm -f $(OBJECTS_EX2)
	-rm -f $(TARGET_EX)
	cd $(LRCU_DIR)
	#make -C $(KERNEL_DIR) M=$(LRCU_DIR) CONFIG_LRCU=m clean
	#make -C $(KERNEL_DIR) M=$(TARGET_EX2_DIR) clean
