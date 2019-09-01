ifeq ($(OS),Windows_NT)
OBJECTS += gerror.o
OBJECTS += $(patsubst %.c,%.o,$(wildcard src/windows/*.c))
else
OBJECTS += $(patsubst %.c,%.o,$(wildcard src/linux/*.c))
endif

CPPFLAGS += -Iinclude -I. -I../
CFLAGS += -fPIC

LDFLAGS += -L../gimxlog -L../gimxtime
LDLIBS += -lgimxlog -lgimxtime

include Makedefs

ifeq ($(OS),Windows_NT)
gerror.o: ../gimxcommon/src/windows/gerror.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<
endif
