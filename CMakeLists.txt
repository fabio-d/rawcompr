# rawcompr: Losslessly compress raw streams in multimedia files.
# Copyright (C) 2021  Fabio D'Urso <fabiodurso@hotmail.it>
#
# "rawcompr" is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.8)
project(rawcompr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
link_libraries(PkgConfig::LIBAV)

add_executable(rawcompr
	src/commandline.cpp
	src/decoders.cpp
	src/encoders.cpp
	src/libav.cpp
	src/llrfile.cpp
	src/log.cpp
	src/main.cpp
)
