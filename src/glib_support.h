/*
   FSearch - A fast file search utility
   Copyright © 2017 Christian Boxdörfer

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

#include <glib.h>

#ifndef G_DEFINE_AUTOPTR_CLEANUP_FUNC
#define G_DEFINE_AUTOPTR_CLEANUP_FUNC(TypeName, func) \
  typedef TypeName *_GLIB_AUTOPTR_TYPENAME(TypeName);                                                           \
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS                                                                              \
  static inline void _GLIB_AUTOPTR_FUNC_NAME(TypeName) (TypeName **_ptr) { if (*_ptr) (func) (*_ptr); }         \
G_GNUC_END_IGNORE_DEPRECATIONS
#endif

#ifndef _GLIB_AUTOPTR_FUNC_NAME
#define _GLIB_AUTOPTR_FUNC_NAME(TypeName) glib_autoptr_cleanup_##TypeName
#endif

#ifndef _GLIB_AUTOPTR_TYPENAME
#define _GLIB_AUTOPTR_TYPENAME(TypeName)  TypeName##_autoptr
#endif

#ifndef _GLIB_DEFINE_AUTOPTR_CHAINUP
#define _GLIB_DEFINE_AUTOPTR_CHAINUP(ModuleObjName, ParentName) \
  typedef ModuleObjName *_GLIB_AUTOPTR_TYPENAME(ModuleObjName);                                          \
  static inline void _GLIB_AUTOPTR_FUNC_NAME(ModuleObjName) (ModuleObjName **_ptr) {                     \
    _GLIB_AUTOPTR_FUNC_NAME(ParentName) ((ParentName **) _ptr); }
#endif


#ifndef G_DECLARE_FINAL_TYPE
#define G_DECLARE_FINAL_TYPE(ModuleObjName, module_obj_name, MODULE, OBJ_NAME, ParentName) \
  GType module_obj_name##_get_type (void);                                                               \
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS                                                                       \
  typedef struct _##ModuleObjName ModuleObjName;                                                         \
  typedef struct { ParentName##Class parent_class; } ModuleObjName##Class;                               \
                                                                                                         \
  G_DEFINE_AUTOPTR_CLEANUP_FUNC(ParentName, g_object_unref)                                              \
  _GLIB_DEFINE_AUTOPTR_CHAINUP (ModuleObjName, ParentName)                                               \
                                                                                                         \
  static inline ModuleObjName * MODULE##_##OBJ_NAME (gpointer ptr) {                                     \
    return G_TYPE_CHECK_INSTANCE_CAST (ptr, module_obj_name##_get_type (), ModuleObjName); }             \
  static inline gboolean MODULE##_IS_##OBJ_NAME (gpointer ptr) {                                         \
    return G_TYPE_CHECK_INSTANCE_TYPE (ptr, module_obj_name##_get_type ()); }                            \
G_GNUC_END_IGNORE_DEPRECATIONS
#endif
