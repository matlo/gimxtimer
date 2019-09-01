CFLAGS += -O3
CXXFLAGS += -O3

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
