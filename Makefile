# Copyright (C) 2017 Fredrik Öhrström
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# 

# Establish the source directory where this makefile resides.
ifeq ($(filter /%,$(lastword $(MAKEFILE_LIST))),)
  makefile_path:=$(CURDIR)/$(lastword $(MAKEFILE_LIST))
else
  makefile_path:=$(lastword $(MAKEFILE_LIST))
endif
root_dir:=$(dir $(makefile_path))

SPEC:=$(wildcard build/*/spec.gmk)

ifeq ($(words $(SPEC)),1)
    # Only one configuration exists, thus we built that one.
    include $(SPEC)
    include $(root_dir)/Main.gmk
else
      $(error You have more than one configuration. Run make from the build directory of your choice!)
endif
