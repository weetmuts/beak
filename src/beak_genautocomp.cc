/*
 Copyright (C) 2016-2019 Fredrik Öhrström

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "beak.h"
#include "beak_implementation.h"
#include "backup.h"
#include "log.h"
#include "origintool.h"
#include "storagetool.h"

static ComponentId COMMANDLINE = registerLogComponent("commandline");

const char *autocomplete =
#include"generated_autocomplete.h"
    ;

void BeakImplementation::genAutoComplete(string filename)
{
    FILE *f = fopen(filename.c_str(),"wb");
    if (!f) {
        error(COMMANDLINE, "Could not open %s\n", filename.c_str());
    }
    fwrite(autocomplete, 1, strlen(autocomplete), f);
    fclose(f);
}
