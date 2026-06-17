CXX      := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -O2
TARGET   := dino
OBJS     := main.o dino.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Rebuild both .o files when the header changes
main.o: main.cpp dino.h
	$(CXX) $(CXXFLAGS) -c main.cpp

dino.o: dino.cpp dino.h
	$(CXX) $(CXXFLAGS) -c dino.cpp

clean:
	rm -f $(TARGET) $(OBJS)
