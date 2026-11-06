# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-src"
  "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-build"
  "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-subbuild/stb-populate-prefix"
  "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-subbuild/stb-populate-prefix/tmp"
  "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp"
  "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-subbuild/stb-populate-prefix/src"
  "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/pavel/Документы/Computer_graphics_MAI_lab3/build-debug/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
