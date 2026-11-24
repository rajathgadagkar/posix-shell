CXX      := g++
CXXFLAGS := -Wall -Wextra -Werror -std=c++17 -pedantic -Iinclude

SRC_DIR   := src
BUILD_DIR := build
TARGET    := posix-shell

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean rebuild run test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	bash tests/run_tests.sh

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

rebuild: clean all
