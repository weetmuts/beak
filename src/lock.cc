/*
    Copyright (C) 2018 Fredrik Öhrström

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

#ifndef LOCK_H
#define LOCK_H

#include "lock.h"

#include "log.h"

#include <pthread.h>

ComponentId LOCK = registerLogComponent("lock");

void lockMutex(pthread_mutex_t *lock, const char *func, const char *file, int line)
{
    debug(LOCK, "taking %p %s %s:%d\n", &lock, func, file, line);
    pthread_mutex_lock(lock);
    debug(LOCK, "taken  %p %s %s:%d\n", &lock, func, file, line);
}

void unlockMutex(pthread_mutex_t *lock, const char *func, const char *file, int line)
{
    debug(LOCK, "returning %p %s %s:%d\n", &lock, func, file, line);
    pthread_mutex_unlock(lock);
    debug(LOCK, "returned  %p %s %s:%d\n", &lock, func, file, line);
}

#endif
