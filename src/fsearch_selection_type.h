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
*/

#pragma once

typedef enum {
    FSEARCH_SELECTION_TYPE_CLEAR,
    FSEARCH_SELECTION_TYPE_ALL,
    FSEARCH_SELECTION_TYPE_INVERT,
    FSEARCH_SELECTION_TYPE_SELECT,
    FSEARCH_SELECTION_TYPE_TOGGLE,
    FSEARCH_SELECTION_TYPE_SELECT_RANGE,
    FSEARCH_SELECTION_TYPE_TOGGLE_RANGE,
    NUM_FSEARCH_SELECTION_TYPES,
} FsearchSelectionType;