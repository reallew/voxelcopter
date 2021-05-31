SOURCES=voxelcopter.cpp
SOURCEOBJECTS=$(SOURCES:.cpp=.o)
FLAGS=-O3 -std=c++20 -march=native -Wall `sdl2-config --cflags` -Wfatal-errors
LFLAGS=-lSDL2 -lSDL2_image -pthread
BIN=voxelcopter

$(BIN): $(SOURCEOBJECTS)
	g++ $(FLAGS) -o $(BIN) $(SOURCEOBJECTS) $(LFLAGS)

./%.o: ./%.cpp
	g++ $(FLAGS) -c -o $@ $<

clean:
	rm -f *.o $(BIN)
