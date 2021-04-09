cmake_minimum_required(VERSION 3.16)
project(smartespresso)

set(CMAKE_CXX_STANDARD 14)

add_executable(smartespresso smart-espressor.cpp)

# https://stackoverflow.com/questions/52468551/include-pistache-in-c-project
# https://stackoverflow.com/questions/28536435/how-to-link-a-static-library-to-an-executable-using-cmake/28537032#28537032
# https://stackoverflow.com/questions/39598323/how-to-properly-link-libraries-with-cmake/39600062#39600062
TARGET_LINK_LIBRARIES(smartespresso pistache)
TARGET_LINK_LIBRARIES(smartespresso pthread)