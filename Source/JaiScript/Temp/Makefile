CXX = g++
CXXFLAGS = -std=c++20 -I../include -g -O0
LDFLAGS = 

SOURCES = ../src/jaiscript.cpp test_lexer.cpp test_parser.cpp test_main.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = jaiscript_test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run