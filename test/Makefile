ifneq ($(DEBUG),1)
CFLAGS += -Wall -Wextra -Werror -O3
CXXFLAGS += -Wall -Wextra -Werror -O3
else
ifneq ($(OS),Windows_NT)
CFLAGS += -Wall -Wextra -Werror -O0 -g -fsanitize=address -fno-omit-frame-pointer
CXXFLAGS += -Wall -Wextra -Werror -O0 -g -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
else
CFLAGS += -Wall -Wextra -Werror -O0 -g -fno-omit-frame-pointer
CXXFLAGS += -Wall -Wextra -Werror -O0 -g -fno-omit-frame-pointer
endif
endif

CPPFLAGS = -I../..

LDFLAGS = -L../../gimxpoll -L../../gimxlog -L../../gimxtime -L../../gimxtimer -L../../gimxprio
LDLIBS = -lgimxpoll -lgimxlog -lgimxtime -lgimxtimer -lgimxprio

LDLIBS += -lm

BINS=gtimer_test
ifneq ($(OS),Windows_NT)
OUT=$(BINS)
else
OUT=gtimer_test.exe
endif

all: $(BINS)

clean:
	$(RM) $(OUT) *~
