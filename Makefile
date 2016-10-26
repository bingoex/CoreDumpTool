ifeq ($(ARCH),32)
        CFLAGS += -m32 -march=pentium4
else
        CFLAGS += -m64
endif

CFLAGS += -g -Wall -D_GNU_SOURCE -D_MP_MODE -Wno-write-strings -Werror -DTIXML_USE_STL

OBJ := segv_api.o
TARGET := libsegv.a

%.o: %.cpp
	@echo -e Compiling $<
	@$(CXX) $(CFLAGS) -c -o $@ $<  
%.o: %.c
	@echo -e Compiling $<
	@$(CC) $(CFLAGS) -c -o $@ $<  

all : $(TARGET)

$(TARGET) : $(OBJ)
	@echo -e  Linking $@
	@ar crs $@ $^ 
	@chmod +x $@

clean:
	@echo "Cleaning..."
	@rm -f $(OBJ) $(TARGET)
	


