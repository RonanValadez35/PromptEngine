CXX = g++
CXXFLAGS = -std=c++20 -I/opt/homebrew/include

build/server: src/server.cpp
	$(CXX) $(CXXFLAGS) src/server.cpp -o build/server

server: build/server

clean:
	rm -f build/server
