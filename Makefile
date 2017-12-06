CC := gcc
CPP := g++


#CFLAGS := -ggdb3 -O3 -D__DEBUG
#CFLAGS := -g -O3 -DM5_PROFILING

CFLAGS := -msse2 -DDEBUG
#CFLAGS := -msse2 -D__USE_GNU
#CPPFLAGS := $(CFLAGS)
LFLAGS := -pthread -lrt
M5LFLAGS := -static ~/gem5/m5threads/pthread.o -lrt

ALL := CQtest CQtestM CQtestM4

default: $(ALL)

clean: 
	$(RM) $(ALL) *.o

# Link program for GEM5 execution
%.m5: %.o
	@echo '$(CC) $(M5LFLAGS)  $< -o $@'
	@$(CC) $< $(M5LFLAGS) -o $@
# Assembly listing
%.s: %.c
	@echo '$(CC) $(CFLAGS) -S -fverbose-asm $< -o $@'
	@$(CC)  $(CFLAGS) -fverbose-asm -S $< -o $@

# C Compilation
%.o: %.c
	@echo '$(CC) $(CFLAGS) -c $< -o $@'
	@$(CC)  $(CFLAGS) -c $< -o $@

# Compile & Link program for normal execution
%: %.o
	@echo '$(CC)  $(CFLAGS) $< $(LFLAGS)  -o $@'
	@$(CC) $(CFLAGS) $< $(LFLAGS) -o $@

CQtest: CQtest.o CQ.o CQmain.o
	@echo '$(CC)  CQmain.o CQtest.o CQ.o $(LFLAGS)  -o $@'
	@$(CC) CQmain.o CQtest.o CQ.o $(LFLAGS) -o $@

CQtestM: CQtest.o CQmutex.o CQmain.o
	@echo '$(CC)  CQtest.o CQmutex.o CQmain.o $(LFLAGS)  -o $@'
	@$(CC) CQtest.o CQmutex.o CQmain.o $(LFLAGS) -o $@

CQtestM4: CQtest.o CQmutex4.o CQmain.o
	@echo '$(CC)  CQtest.o CQmutex4.o CQmain.o $(LFLAGS)  -o $@'
	@$(CC) CQtest.o CQmutex4.o CQmain.o $(LFLAGS) -o $@

CQtestM.o: CQtest.c CQ.h Makefile
	@echo '$(CC) $(CFLAGS) -DMUTEX -c CQtest.c -o $@'
	@$(CC)  $(CFLAGS) -DMUTEX -c CQtest.c -o $@

CQmutex.o: CQmutex.c CQ.h Makefile
	@echo '$(CC) $(CFLAGS) -DMUTEX -c $< -o $@'
	@$(CC)  $(CFLAGS) -DMUTEX -c $< -o $@

CQmutex4.o: CQmutex4.c CQ.h Makefile
	@echo '$(CC) $(CFLAGS) -DMUTEX -c $< -o $@'
	@$(CC)  $(CFLAGS) -DMUTEX -c $< -o $@

CQmain.o: CQmain.c CQ.h Makefile
CQtest.o: CQtest.c CQ.h Makefile
CQ.o: CQ.c CQ.h Makefile

