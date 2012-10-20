#############################################################################
# Primesense Experience Engine library template makefile.
# This file should not be made, but only included from other makefiles.
#
# Project makefile should define the following BEFORE including this file:
# SRC_FILES - a list of all source files
# LIB_NAME - output name
# INC_DIRS - a list of additional include directories
# LIB_DIRS - a list of additional library directories
# USED_LIBS - a list of libraries to link with
# DEFINES - [Optional] additional preprocessor defines
# CFLAGS - [Optional] additional flags for the compiler
# LDFLAGS - [Optional] additional flags for the linker
#############################################################################


SRC_FILES = main.cpp Interface.cpp 

EXE_NAME = KinectDetector

ifndef TARGETFS
	TARGETFS=/
endif

INC_DIRS += ../../Include =/usr/include/ni ../../Source/XnCommon =/usr/include/ni
BIN_DIR = ../Bin

USED_LIBS += OpenNI XnVNite_1_5_2




# take this file's dir
COMMON_CPP_MAKE_FILE_DIR = $(dir $(lastword $(MAKEFILE_LIST)))


ifndef _COMMON_DEFS_MAKE_
_COMMON_DEFS_MAKE_=1

# some defaults
ifndef CFG
    CFG = Release
endif

# find out the platform on which we're running
MACHINE = $(shell uname -m)
ifneq (,$(findstring x86_64,$(MACHINE)))
	HOST_PLATFORM = x64
else ifneq (,$(findstring x86,$(MACHINE)))
	HOST_PLATFORM = x86
else ifneq (,$(findstring i686,$(MACHINE)))
	HOST_PLATFORM = x86
else ifneq (,$(findstring i386,$(MACHINE)))
	HOST_PLATFORM = x86
else ifneq (,$(findstring arm,$(MACHINE)))
	HOST_PLATFORM = Arm
else
	DUMMY:=$(error Can't determine host platform)
endif

# now check if this is a cross-compilation or not
ifeq "$(PLATFORM)" ""
	PLATFORM = $(HOST_PLATFORM)
else
	ifneq "$(PLATFORM)" "$(HOST_PLATFORM)"
		# cross compiling. Take CXX and STAGING_DIR from environment
		PLATFORM_UPPER = $(shell echo $(PLATFORM) | tr 'a-z' 'A-Z')
		DUMMY:=$(eval CXX = $($(PLATFORM_UPPER)_CXX))
		DUMMY:=$(eval TARGET_SYS_ROOT = $($(PLATFORM_UPPER)_STAGING))
		
		ifeq "$(and $(CXX), $(TARGET_SYS_ROOT))" ""
			DUMMY:=$(error Cross-Compilation error. Can't find $(PLATFORM_UPPER)_CXX and $(PLATFORM_UPPER)_STAGING)
		endif
	endif
endif

# expand file list
SRC_FILES_LIST = $(wildcard $(SRC_FILES))

# define the intermediate directory
INT_DIR = $(PLATFORM)-$(CFG)

# define output directory
OUT_DIR = $(BIN_DIR)/$(PLATFORM)-$(CFG)

# full path to output file
OUTPUT_FILE = $(OUT_DIR)/$(OUTPUT_NAME)

# take this file's dir
COMMON_MAK_DIR = $(dir $(lastword $(MAKEFILE_LIST)))

# get the OS type
OSTYPE := $(shell uname -s)

# platform specific args
include $(COMMON_MAK_DIR)Platform.$(PLATFORM)

endif # _COMMON_DEFS_MAKE_


# define a function to figure .o file for each source file (placed under intermediate directory)
SRC_TO_OBJ = $(addprefix ./$(INT_DIR)/,$(addsuffix .o,$(notdir $(basename $1))))

# create a list of all object files
OBJ_FILES = $(call SRC_TO_OBJ,$(SRC_FILES_LIST))

# define a function to translate any source file to its dependency file (note that the way we create
# dep files, as a side affect of compilation, always puts the files in the INT_DIR with suffix .d)
SRC_TO_DEP = $(addprefix ./$(INT_DIR)/,$(addsuffix .d,$(notdir $(basename $1))))

# create a list of all dependency files
DEP_FILES = $(call SRC_TO_DEP,$(SRC_FILES_LIST))

# older version of gcc doesn't support the '=' symbol in include dirs, so we replace it ourselves with sysroot
INC_DIRS_FROM_SYSROOT = $(patsubst =/%,$(TARGET_SYS_ROOT)/%,$(INC_DIRS))

# append the -I switch to each include directory
INC_DIRS_OPTION = $(foreach dir,$(INC_DIRS_FROM_SYSROOT),-I$(dir))

# append the -L switch to each library directory
LIB_DIRS_OPTION = $(foreach dir,$(LIB_DIRS),-L$(dir)) -L$(OUT_DIR)

# append the -l switch to each library used
USED_LIBS_OPTION = $(foreach lib,$(USED_LIBS),-l$(lib))

# append the -D switch to each define
DEFINES_OPTION = $(foreach def,$(DEFINES),-D$(def))

# tell compiler to use the target system root
ifdef TARGET_SYS_ROOT
	CFLAGS += --sysroot=$(TARGET_SYS_ROOT)
	LDFLAGS += --sysroot=$(TARGET_SYS_ROOT)
endif

# set Debug / Release flags
# Note that the -w flag has been added both for the Debug and Release configurations. It should be changed..
ifeq "$(CFG)" "Debug"
	CFLAGS += -O0 -g -w
endif
ifeq "$(CFG)" "Release"
	CFLAGS += -O2 -DNDEBUG -w
endif

CFLAGS += $(INC_DIRS_OPTION) $(DEFINES_OPTION)
LDFLAGS += $(LIB_DIRS_OPTION) $(USED_LIBS_OPTION)

# some lib / exe specifics
ifneq "$(LIB_NAME)" ""
	OUTPUT_NAME = lib$(LIB_NAME).so
	CFLAGS += -fPIC -fvisibility=hidden
	ifneq ("$(OSTYPE)","Darwin")
		LDFLAGS += -Wl,--no-undefined
		OUTPUT_NAME = lib$(LIB_NAME).so
		OUTPUT_COMMAND = $(CXX) -o $(OUTPUT_FILE) $(OBJ_FILES) $(LDFLAGS) -shared
	else
		LDFLAGS += -undefined error
		OUTPUT_NAME = lib$(LIB_NAME).dylib
		OUTPUT_COMMAND = $(CXX) -o $(OUTPUT_FILE) $(OBJ_FILES) $(LDFLAGS) -dynamiclib -headerpad_max_install_names
	endif
endif
ifneq "$(EXE_NAME)" ""
	OUTPUT_NAME = $(EXE_NAME)
	OUTPUT_COMMAND = $(CXX) -o $(OUTPUT_FILE) $(OBJ_FILES) $(LDFLAGS)
endif
ifneq "$(SLIB_NAME)" ""
	CFLAGS += -fPIC
	OUTPUT_NAME = lib$(SLIB_NAME).a
	OUTPUT_COMMAND = $(AR) -cq $(OUTPUT_FILE) $(OBJ_FILES)
endif

define CREATE_SRC_TARGETS
# create a target for the object file (the CXX command creates both an .o file
# and a .d file)
ifneq ("$(OSTYPE)","Darwin")
$(call SRC_TO_OBJ,$1) : $1 | $(INT_DIR)
	$(CXX) -MD -MP -MT "$(call SRC_TO_DEP,$1) $$@" -c $(CFLAGS) -o $$@ $$<
else
$(call SRC_TO_OBJ,$1) : $1 | $(INT_DIR)
	$(CXX) -c $(CFLAGS) -o $$@ $$<
endif
endef


#############################################################################
# Targets
#############################################################################
.PHONY: clean-objs clean-defs

.PHONY: all clean clean-$(OUTPUT_FILE)

# define the target 'all' (it is first, and so, default)
all: $(OUTPUT_FILE)

# Intermediate directory
$(INT_DIR):
	mkdir -p $(INT_DIR)

# Output directory
$(OUT_DIR):
	mkdir -p $(OUT_DIR)

# Final output file
$(OUTPUT_FILE): $(SRC_FILES_LIST) | $(OUT_DIR)

clean-$(OUTPUT_FILE):
	rm -rf $(OUTPUT_FILE)
	
clean: clean-$(OUTPUT_FILE)



# create targets for each source file
$(foreach src,$(SRC_FILES_LIST),$(eval $(call CREATE_SRC_TARGETS,$(src))))

# include all dependency files (we don't need them the first time, so we can use -include)
-include $(DEP_FILES)

$(OUTPUT_FILE): $(OBJ_FILES)
	$(OUTPUT_COMMAND)

clean-objs:
	rm -rf $(OBJ_FILES)
	
clean-defs:
	rm -rf $(DEP_FILES)

clean: clean-objs clean-defs
