CXX = g++
CXXFLAGS = -std=c++20 -I/opt/homebrew/include -Iinclude
LDFLAGS = -L/opt/homebrew/lib

build/server: src/server.cpp
	$(CXX) $(CXXFLAGS) src/server.cpp -o build/server

server: build/server

test: src/ts_queue.cpp tests/test_ts_queue.cpp
	$(CXX) $(CXXFLAGS) src/ts_queue.cpp tests/test_ts_queue.cpp \
	$(LDFLAGS) \
	-lgtest \
	-lgtest_main \
	-pthread \
	-o build/test_queue

clean:
	rm -f build/server build/test_queue