
RED = \\e[1m\\e[31m
DARKRED = \\e[31m
GREEN = \\e[1m\\e[32m
DARKGREEN = \\e[32m
BLUE = \\e[1m\\e[34m
DARKBLUE = \\e[34m
YELLOW = \\e[1m\\e[33m
DARKYELLOW = \\e[33m
MAGENTA = \\e[1m\\e[35m
DARKMAGENTA = \\e[35m
CYAN = \\e[1m\\e[36m
DARKCYAN = \\e[36m
RESET = \\e[m
CRESET =  ;echo -ne \\e[m; test -s $@


ifeq ($(ARCH),32)
        CFLAGS += -m32 -march=pentium4
else
        CFLAGS += -m64
endif

%.o: %.cpp
	@echo -e Compiling $(GREEN)$<$(RESET) ...$(RED)
	@$(CXX) $(CFLAGS) -c -o $@ $<  $(CRESET)   
%.o: %.c
	@echo -e Compiling $(GREEN)$<$(RESET) ...$(RED)
	@$(CC) $(CFLAGS) -c -o $@ $<  $(CRESET)

CFLAGS += -g -Wall -D_GNU_SOURCE -D_MP_MODE -Wno-write-strings -Werror -DTIXML_USE_STL

OBJ := segv_api.o
TARGET := libsegv.a

all : $(TARGET)

$(TARGET) : $(OBJ)
	@echo -e  Linking $(CYAN)$@$(RESET) ...$(RED) 
	@ar crs $@ $^ $(CRESET)
	@chmod +x $@

clean:
	@echo "Cleaning..."
	@rm -f $(OBJ) $(TARGET)
	


