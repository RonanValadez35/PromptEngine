CXX = g++
CXXFLAGS = -std=c++20 -I/opt/homebrew/include -Iinclude -I/opt/homebrew/opt/curl/include/curl
LDFLAGS = -L/opt/homebrew/lib

build/server: src/server.cpp
	$(CXX) $(CXXFLAGS) src/server.cpp -o build/server

server: build/server

test_queue: src/ts_queue.cpp tests/test_ts_queue.cpp
	$(CXX) $(CXXFLAGS) src/ts_queue.cpp tests/test_ts_queue.cpp \
	$(LDFLAGS) \
	-lgtest \
	-lgtest_main \
	-pthread \
	-o build/test_queue

test_thread_pool: src/ts_queue.cpp src/thread_pool.cpp tests/test_thread_pool.cpp
	$(CXX) $(CXXFLAGS) src/ts_queue.cpp src/thread_pool.cpp tests/test_thread_pool.cpp \
	$(LDFLAGS) \
	-lgtest \
	-lgtest_main \
	-pthread \
	-o build/test_thread_pool

test_ollama_runner: src/ollama_runner.cpp tests/test_ollama_runner.cpp
	$(CXX) $(CXXFLAGS) src/ollama_runner.cpp tests/test_ollama_runner.cpp \
	$(LDFLAGS) \
	-lcurl \
	-o build/test_ollama_runner

debug:
	$(CXX) $(CXXFLAGS) -g \
	tests/test_thread_pool.cpp \
	src/thread_pool.cpp \
	src/ts_queue.cpp \
	$(LDFLAGS) \
	-lgtest -lgtest_main \
	-pthread \
	-o build/test_thread_pool


run_tests: test_queue test_thread_pool
	./build/test_queue
	./build/test_thread_pool
	
run_ollama_test: test_ollama_runner
	./build/test_ollama_runner

clean:
	rm -f build/server build/test_queue build/test_thread_pool build/test_ollama_runner