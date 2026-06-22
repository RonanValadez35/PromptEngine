CXX = g++
CXXFLAGS = -std=c++20 -I/opt/homebrew/include -Iinclude -I/opt/homebrew/opt/curl/include/curl
LDFLAGS = -L/opt/homebrew/lib -L/opt/homebrew/opt/libpq/lib

build/server: src/server.cpp src/ts_queue.cpp src/thread_pool.cpp src/ollama_runner.cpp src/job_registry.cpp
	$(CXX) $(CXXFLAGS) \
	src/server.cpp \
	src/ts_queue.cpp \
	src/thread_pool.cpp \
	src/ollama_runner.cpp \
	src/job_registry.cpp \
	$(LDFLAGS) \
	-lcurl \
	-pthread \
	-o build/server

server: build/server

run_server: build/server
	./build/server

debug:
	$(CXX) $(CXXFLAGS) -g \
	tests/test_thread_pool.cpp \
	src/thread_pool.cpp \
	src/ts_queue.cpp \
	$(LDFLAGS) \
	-lgtest -lgtest_main \
	-pthread \
	-o build/test_thread_pool


bench: src/ts_queue.cpp src/thread_pool.cpp tests/mock_ollama_runner.cpp tests/bench_thread_pool.cpp
	$(CXX) $(CXXFLAGS) \
	src/ts_queue.cpp \
	src/thread_pool.cpp \
	tests/mock_ollama_runner.cpp \
	tests/bench_thread_pool.cpp \
	$(LDFLAGS) \
	-pthread \
	-o build/bench

test_db_pool: src/db_pool.cpp tests/test_db_pool.cpp
	$(CXX) $(CXXFLAGS) src/db_pool.cpp tests/test_db_pool.cpp \
	$(LDFLAGS) \
	-lpqxx \
	-lpq \
	-lgtest \
	-lgtest_main \
	-pthread \
	-o build/test_db_pool
	
test_job_store: src/job_store.cpp src/db_pool.cpp tests/test_job_store_functions.cpp
	$(CXX) $(CXXFLAGS) src/job_store.cpp src/db_pool.cpp tests/test_job_store_functions.cpp \
	$(LDFLAGS) \
	-lpqxx \
	-lpq \
	-lgtest \
	-lgtest_main \
	-pthread \
	-o build/test_job_store


clean:
	rm -f build/server build/test_queue build/test_thread_pool build/test_ollama_runner


######################################################################
# Depreciated:

# test_queue: src/ts_queue.cpp tests/test_ts_queue.cpp
# 	$(CXX) $(CXXFLAGS) src/ts_queue.cpp tests/test_ts_queue.cpp \
# 	$(LDFLAGS) \
# 	-lgtest \
# 	-lgtest_main \
# 	-pthread \
# 	-o build/test_queue

# test_thread_pool: src/ts_queue.cpp src/thread_pool.cpp tests/test_thread_pool.cpp
# 	$(CXX) $(CXXFLAGS) src/ts_queue.cpp src/thread_pool.cpp tests/test_thread_pool.cpp \
# 	$(LDFLAGS) \
# 	-lgtest \
# 	-lgtest_main \
# 	-pthread \
# 	-o build/test_thread_pool

# test_ollama_runner: src/ollama_runner.cpp tests/test_ollama_runner.cpp
# 	$(CXX) $(CXXFLAGS) src/ollama_runner.cpp tests/test_ollama_runner.cpp \
# 	$(LDFLAGS) \
# 	-lcurl \
# 	-o build/test_ollama_runner

# run_tests: test_queue test_thread_pool
# 	./build/test_queue
# 	./build/test_thread_pool
	
# run_ollama_test: test_ollama_runner
# 	./build/test_ollama_runner
######################################################################
