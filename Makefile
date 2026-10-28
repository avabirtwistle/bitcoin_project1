CC = clang

CFLAGS = -Wall -Wextra -std=c11 \
	-I/opt/homebrew/opt/cjson/include \
	-I/opt/homebrew/opt/openssl@3/include \
	-I/opt/homebrew/opt/readline/include

LDFLAGS = -L/opt/homebrew/opt/cjson/lib \
	-L/opt/homebrew/opt/openssl@3/lib \
	-L/opt/homebrew/opt/readline/lib

LDLIBS = -lcjson -lcrypto -lreadline -lpthread

TARGET = blockchain

SOURCES = main.c prefix.c start_up.c block.c network.c
HEADERS = prefix.h start_up.h block.h network.h

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)