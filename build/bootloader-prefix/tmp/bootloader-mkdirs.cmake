# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/alrescha/esp32/esp-idf/components/bootloader/subproject"
  "/home/alrescha/esp32/projects/m5_midi/build/bootloader"
  "/home/alrescha/esp32/projects/m5_midi/build/bootloader-prefix"
  "/home/alrescha/esp32/projects/m5_midi/build/bootloader-prefix/tmp"
  "/home/alrescha/esp32/projects/m5_midi/build/bootloader-prefix/src/bootloader-stamp"
  "/home/alrescha/esp32/projects/m5_midi/build/bootloader-prefix/src"
  "/home/alrescha/esp32/projects/m5_midi/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/alrescha/esp32/projects/m5_midi/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/alrescha/esp32/projects/m5_midi/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
