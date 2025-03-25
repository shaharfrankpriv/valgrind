/*--------------------------------------------------------------------*/
/*--- Cachegrind: a CG client request header                     ---*/
/*---                                               cachegrind.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Cachegrind, a Valgrind tool for cache
   profiling programs.

   Copyright (C) 2002-2017 Nicholas Nethercote
      njn@valgrind.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

#ifndef __CACHEGRIND_H
#define __CACHEGRIND_H

#include "valgrind.h"

/* Client requests for Cachegrind */
typedef enum {
   VG_USERREQ__ANALYZE_MEMORY = VG_USERREQ_TOOL_BASE('C', 'G')
} CachegrindClientRequest;

/* Trigger stack and heap memory analysis from within the client program.
   This will print information about current stack pointers for all threads
   and information about heap memory regions. */
#define CACHEGRIND_ANALYZE_MEMORY \
   VALGRIND_DO_CLIENT_REQUEST_STMT(VG_USERREQ__ANALYZE_MEMORY, 0, 0, 0, 0, 0)

#endif /* __CACHEGRIND_H */ 