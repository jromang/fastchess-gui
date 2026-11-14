# Run in the mbedtls sources after download: its CMakeLists ask for CMake 3.5, which every CMake since 3.27 warns
# about at each configure. "3.5...3.10" says the same minimum and that 3.10 rules are fine: no warning, any CMake.
# A version written so already is left as it is: the script may run again on the same sources.
foreach(file CMakeLists.txt tf-psa-crypto/CMakeLists.txt)
    file(READ ${file} text)
    string(REGEX REPLACE "cmake_minimum_required\\(VERSION ([0-9]+(\\.[0-9]+)*)\\)"
           "cmake_minimum_required(VERSION \\1...3.10)" text "${text}")
    file(WRITE ${file} "${text}")
endforeach()
