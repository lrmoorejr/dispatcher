# Copyright 2026 L. Richard Moore Jr.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.18)
project(dispatcher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(Catch2 3 REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Dispatcher/BatchDispatcher/SlowDispatcher all describe their parameter space
# with a Flattener<>, so pull that header in from its own repo rather than
# vendoring a copy that could drift. When this repo is embedded in a parent
# build that already provides Flattener.hpp (e.g. commons), that copy wins
# instead, since it'll already be on the include path.
if(NOT COMMAND FetchContent_Declare)
	include(FetchContent)
endif()
FetchContent_Declare(
	flattener
	GIT_REPOSITORY https://github.com/lrmoorejr/flattener.git
	GIT_TAG main
)
FetchContent_MakeAvailable(flattener)
include_directories(${flattener_SOURCE_DIR})

add_executable(Dispatcher-test Dispatcher-test.cpp Dispatcher.hpp)
target_link_libraries(Dispatcher-test PRIVATE Catch2::Catch2WithMain Threads::Threads)

add_executable(BatchDispatcher-test BatchDispatcher-test.cpp BatchDispatcher.hpp)
target_link_libraries(BatchDispatcher-test PRIVATE Catch2::Catch2WithMain Threads::Threads)

add_executable(SlowDispatcher-test SlowDispatcher-test.cpp SlowDispatcher.hpp)
target_link_libraries(SlowDispatcher-test PRIVATE Catch2::Catch2WithMain Threads::Threads)

enable_testing()
include(Catch)
catch_discover_tests(Dispatcher-test)
catch_discover_tests(BatchDispatcher-test)
catch_discover_tests(SlowDispatcher-test)
