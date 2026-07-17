/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

#pragma once

#include "fsearch_array.h"
#include "fsearch_database_index_properties.h"

#include <gio/gio.h>
#include <stdbool.h>

// Returns the canonical, fully deterministic comparator chain for a single sort property, e.g.
// SIZE -> [SIZE, NAME, PATH]. FILETYPE has no natural continuation of its own ([FILETYPE]) since
// there's no fast index to derive one from; see fsearch_database_sort_order_chain_prepend().
FsearchDatabaseSortOrderChain
fsearch_database_sort_order_chain_for_property(FsearchDatabaseIndexProperty property);

// Prepends `property` onto `chain` (removing any pre-existing occurrence of `property`) to build
// the full, explicit comparator chain for a manual (non-fast-indexed) sort layered on top of an
// array's previous order -- e.g. sorting by TYPE on top of a SIZE-ordered array yields
// [TYPE, SIZE, NAME, PATH].
FsearchDatabaseSortOrderChain
fsearch_database_sort_order_chain_prepend(FsearchDatabaseSortOrderChain chain, FsearchDatabaseIndexProperty property);

void
fsearch_database_sort_results(FsearchDatabaseSortOrderChain old_chain,
                              FsearchDatabaseIndexProperty new_sort_order,
                              DynamicArray *files_in,
                              DynamicArray *folders_in,
                              DynamicArray *files_fast_sort_index,
                              DynamicArray *folders_fast_sort_index,
                              DynamicArray **files_out,
                              DynamicArray **folders_out,
                              FsearchDatabaseSortOrderChain *chain_out,
                              GCancellable *cancellable);