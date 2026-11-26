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
TEST_TARGET = test_block

SOURCES = main.c prefix.c start_up.c transaction.c mempool.c block.c utxo.c validation.c payment.c network.c
HEADERS = prefix.h start_up.h transaction.h mempool.h block.h utxo.h validation.h payment.h network.h
TEST_SOURCES = tests/test_block.c prefix.c transaction.c mempool.c block.c utxo.c validation.c payment.c

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

$(TEST_TARGET): $(TEST_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I. $(TEST_SOURCES) -o $(TEST_TARGET) $(LDFLAGS) $(LDLIBS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET)
