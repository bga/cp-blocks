# Copyright 2020 Bga <bga.email@gmail.com>

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#   http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

TEMP ?= /tmp


space :=
space +=
nospaces = $(subst $(space),-,$1)

PROJECT = $(call nospaces,$(shell basename "`pwd`"))

TARGET_EXEC ?= $(PROJECT)

ARCH ?= i386
PLATFORM ?= windows


BUILD_DIR ?= $(TEMP)/$(PROJECT)
SRC_DIRS ?= ./src

SRCS := $(shell find $(SRC_DIRS) -name '*.cpp' -or -name '*.c' -or -name '*.s' | grep -Fv '.bak.')
OBJS := $(subst \,/,$(SRCS:%=$(BUILD_DIR)/%.o))
DEPS := $(OBJS:.o=.d)

INC_DIRS := $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

#CPPFLAGS ?= $(INC_FLAGS) -MMD -MP
CPPFLAGS += -Wall -Wextra
CPPFLAGS += -Wno-unused-variable -Wno-unused-parameter
# CPPFLAGS += -D_WIN32 
CPPFLAGS += -I$(PLATFORM)/include -Iinclude -I../../$(PLATFORM)/include -I../../include -I../../../../!cpp/include
CPPFLAGS += -fPIC
# CPPFLAGS += -o .obj/$(@F)
CPPFLAGS += -fdollars-in-identifiers
CPPFLAGS += -pthread 
CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE 
CPPFLAGS += "-DVERSION=\"1.1.6\"" 

ifeq "$(PLATFORM)" 'windows'
  TARGET_EXEC := $(TARGET_EXEC).exe
  CPPFLAGS += -D_WIN32
  LDFLAGS += -mwindows
endif


ifdef DEBUG
	CPPFLAGS += -ggdb -DDEBUG -Og
else
	CPPFLAGS += -DNDEBUG -O2
endif

#CPPFLAGS += -MMD -MP -MF $(BUILD_DIR)/$(@F).d
CPPFLAGS += -MMD -MP
-include $(DEPS)



# LDFLAGS += -L../../lib/$(PLATFORM)/$(ARCH) -L../../../../!cpp/lib/$(PLATFORM)/$(ARCH) 
LDFLAGS += -lpthread 

all: $(TARGET_EXEC)

$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) -o "$@" $(LDFLAGS)

# assembly
$(BUILD_DIR)/%.s.o: %.s
	$(MKDIR_P) $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# c source
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	$(MKDIR_P) $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@


test-data.bin:
	dd if=/dev/urandom of=test-data.bin bs=1M count=9

test: test-data.bin
	./$(TARGET_EXEC) -m --stat test-data.bin test-data.bin.copy
	@chmod u+w test-data.bin.copy
	@echo
	sha1sum test-data.bin 
	sha1sum test-data.bin.copy 

test-split: test-data.bin
	./$(TARGET_EXEC) -m --split-size=2M --stat test-data.bin test-data.bin.copy
	@chmod u+w test-data.bin.copy.*
	@echo
	sha1sum test-data.bin 
	cat test-data.bin.copy.* | sha1sum 

.PHONY: clean list test all

clean:
	$(RM) -r $(BUILD_DIR)
	$(RM) test-data.bin test-data.bin.copy

MKDIR_P ?= mkdir -p

# [https://stackoverflow.com/a/26339924]
list:
	@LC_ALL=C $(MAKE) -pRrq -f $(firstword $(MAKEFILE_LIST)) : 2>/dev/null | awk -v RS= -F: '/(^|\n)# Files(\n|$$)/,/(^|\n)# Finished Make data base/ {if ($$1 !~ "^[#.]") {print $$1}}' | sort | grep -E -v -e '^[^[:alnum:]]' -e '^$@$$'

# debugging make
print-%:
	@echo $* = $($*)
