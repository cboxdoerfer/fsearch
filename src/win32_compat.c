#ifdef _WIN32

#include "win32_compat.h"
#include <windows.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <glib.h>

// Windows implementation of flock
int win32_flock(int fd, int operation) {
    HANDLE handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    OVERLAPPED overlapped = {0};
    DWORD flags = 0;
    
    if (operation & LOCK_EX) {
        flags = LOCKFILE_EXCLUSIVE_LOCK;
    }
    if (operation & LOCK_NB) {
        flags |= LOCKFILE_FAIL_IMMEDIATELY;
    }
    
    if (LockFileEx(handle, flags, 0, MAXDWORD, MAXDWORD, &overlapped)) {
        return 0;
    }
    return -1;
}

// Windows implementation of strcasestr
char *win32_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) {
        return NULL;
    }
    
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return (char *)haystack;
    }
    
    for (const char *h = haystack; *h; h++) {
        if (g_ascii_strncasecmp(h, needle, needle_len) == 0) {
            return (char *)h;
        }
    }
    return NULL;
}

// Windows implementation of lstat with Unicode support
int win32_lstat(const char *path, struct stat *buf) {
    if (!path || !buf) {
        return -1;
    }
    
    // Try Unicode version first for better international character support
    wchar_t *wpath = win32_utf8_to_wchar(path);
    if (wpath) {
        int result = _wstat64(wpath, (struct _stat64*)buf);
        win32_free_wchar(wpath);
        return result;
    }
    
    // Fallback to ANSI version
    return stat(path, buf);
}

// Simple fnmatch implementation for Windows
static int match_pattern(const char *pattern, const char *string, int flags) {
    const char *p = pattern;
    const char *s = string;
    
    while (*p && *s) {
        if (*p == '*') {
            // Handle wildcard
            p++;
            if (*p == '\0') {
                return 0; // Match
            }
            
            while (*s) {
                if (match_pattern(p, s, flags) == 0) {
                    return 0;
                }
                s++;
            }
            return FNM_NOMATCH;
        } else if (*p == '?') {
            // Match any single character
            p++;
            s++;
        } else {
            // Literal character match
            int match;
            if (flags & FNM_PATHNAME && (*p == '/' || *p == '\\')) {
                // Handle path separators
                match = (*s == '/' || *s == '\\');
            } else {
                // Case-insensitive comparison on Windows
                match = (tolower(*p) == tolower(*s));
            }
            
            if (!match) {
                return FNM_NOMATCH;
            }
            p++;
            s++;
        }
    }
    
    // Handle remaining pattern characters
    while (*p == '*') {
        p++;
    }
    
    return (*p == '\0' && *s == '\0') ? 0 : FNM_NOMATCH;
}

int win32_fnmatch(const char *pattern, const char *string, int flags) {
    if (!pattern || !string) {
        return FNM_NOMATCH;
    }
    
    return match_pattern(pattern, string, flags);
}

// Enhanced strptime implementation for Windows
// Supports:
// - "%Y" (4-digit year)
// - "%y" (2-digit year)
// - "%m" (month)
// - "%d" (day)
// - "%H" (hour)
// - "%M" (minute)
// - "%S" (second)
// - Combinations like "%Y-%m-%d", "%y-%m-%d", "%Y-%m-%d %H:%M:%S", etc.
char *win32_strptime(const char *s, const char *format, struct tm *tm) {
    if (!s || !format || !tm) {
        return NULL;
    }
    
    // Clear the tm structure
    memset(tm, 0, sizeof(struct tm));
    
    const char *f = format;
    const char *p = s;
    int part_count = 0;
    
    while (*f && *p) {
        if (*f == '%') {
            f++;
            int consumed = 0;
            int n;
            switch (*f) {
            case 'Y': // 4-digit year
                if (sscanf(p, "%4d%n", &n, &consumed) != 1 || consumed != 4) {
                    return NULL;
                }
                tm->tm_year = n - 1900;
                p += consumed;
                part_count++;
                break;
            case 'y': // 2-digit year
                if (sscanf(p, "%2d%n", &n, &consumed) != 1 || consumed != 2) {
                    return NULL;
                }
                tm->tm_year = (n < 69) ? (n + 100) : n;
                p += consumed;
                part_count++;
                break;
            case 'm': // month
                if (sscanf(p, "%2d%n", &n, &consumed) != 1 || consumed == 0) {
                    return NULL;
                }
                tm->tm_mon = n - 1;
                p += consumed;
                part_count++;
                break;
            case 'd': // day
                if (sscanf(p, "%2d%n", &n, &consumed) != 1 || consumed == 0) {
                    return NULL;
                }
                tm->tm_mday = n;
                p += consumed;
                part_count++;
                break;
            case 'H': // hour (00-23)
                if (sscanf(p, "%2d%n", &n, &consumed) != 1 || consumed == 0) {
                    return NULL;
                }
                tm->tm_hour = n;
                p += consumed;
                part_count++;
                break;
            case 'M': // minute (00-59)
                if (sscanf(p, "%2d%n", &n, &consumed) != 1 || consumed == 0) {
                    return NULL;
                }
                tm->tm_min = n;
                p += consumed;
                part_count++;
                break;
            case 'S': // second (00-59)
                if (sscanf(p, "%2d%n", &n, &consumed) != 1 || consumed == 0) {
                    return NULL;
                }
                tm->tm_sec = n;
                p += consumed;
                part_count++;
                break;
            default:
                // Unsupported format specifier
                return NULL;
            }
            f++;
        } else {
            // Match literal characters (like '-', ':', ' ')
            if (*f != *p) {
                return NULL;
            }
            f++;
            p++;
        }
    }
    
    // If we didn't parse any parts or didn't consume entire string, fail
    if (part_count == 0 || *p != '\0') {
        return NULL;
    }
    
    return (char *)p;
}

// Wide string conversion utilities for Unicode support
wchar_t *win32_utf8_to_wchar(const char *utf8_str) {
    if (!utf8_str) {
        return NULL;
    }
    
    // Calculate required buffer size
    int wchar_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
    if (wchar_len <= 0) {
        return NULL;
    }
    
    // Allocate buffer and convert
    wchar_t *wchar_str = malloc(wchar_len * sizeof(wchar_t));
    if (!wchar_str) {
        return NULL;
    }
    
    if (MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wchar_str, wchar_len) == 0) {
        free(wchar_str);
        return NULL;
    }
    
    return wchar_str;
}

char *win32_wchar_to_utf8(const wchar_t *wchar_str) {
    if (!wchar_str) {
        return NULL;
    }
    
    // Calculate required buffer size
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wchar_str, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) {
        return NULL;
    }
    
    // Allocate buffer and convert
    char *utf8_str = malloc(utf8_len);
    if (!utf8_str) {
        return NULL;
    }
    
    if (WideCharToMultiByte(CP_UTF8, 0, wchar_str, -1, utf8_str, utf8_len, NULL, NULL) == 0) {
        free(utf8_str);
        return NULL;
    }
    
    return utf8_str;
}

void win32_free_wchar(wchar_t *wchar_str) {
    if (wchar_str) {
        free(wchar_str);
    }
}

void win32_free_utf8(char *utf8_str) {
    if (utf8_str) {
        free(utf8_str);
    }
}

// Unicode-aware directory opening
DIR *win32_opendir_unicode(const char *dirname) {
    if (!dirname) {
        return NULL;
    }
    
    // Convert UTF-8 dirname to wide string
    wchar_t *wdirname = win32_utf8_to_wchar(dirname);
    if (!wdirname) {
        // Fallback to ANSI version
        return win32_opendir(dirname);
    }
    
    // Check if dirname is too long to safely append L"\\*" and null terminator
    size_t wdirname_len = wcslen(wdirname);
    if (wdirname_len + 2 >= MAX_PATH) {
        win32_free_wchar(wdirname);
        return NULL;
    }
    
    DIR *dirp = malloc(sizeof(DIR));
    if (!dirp) {
        win32_free_wchar(wdirname);
        return NULL;
    }
    
    // Construct search pattern
    wchar_t wsearch_path[MAX_PATH];
    swprintf(wsearch_path, MAX_PATH, L"%ls\\*", wdirname);
    win32_free_wchar(wdirname);
    
    // Use Unicode version of FindFirstFile
    dirp->handle = FindFirstFileW(wsearch_path, &dirp->find_data);
    if (dirp->handle == INVALID_HANDLE_VALUE) {
        free(dirp);
        return NULL;
    }
    
    dirp->first = 1;
    return dirp;
}

struct dirent *win32_readdir_unicode(DIR *dirp) {
    if (!dirp || dirp->handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    
    if (dirp->first) {
        dirp->first = 0;
    } else {
        if (!FindNextFileW(dirp->handle, &dirp->find_data)) {
            return NULL;
        }
    }
    
    // Convert wide string filename to UTF-8
    char *utf8_name = win32_wchar_to_utf8(dirp->find_data.cFileName);
    if (utf8_name) {
        strncpy(dirp->entry.d_name, utf8_name, sizeof(dirp->entry.d_name) - 1);
        dirp->entry.d_name[sizeof(dirp->entry.d_name) - 1] = '\0';
        win32_free_utf8(utf8_name);
    } else {
        // Fallback: truncate wide string to ASCII
        size_t len = wcstombs(dirp->entry.d_name, dirp->find_data.cFileName, sizeof(dirp->entry.d_name) - 1);
        if (len == (size_t)-1) {
            dirp->entry.d_name[0] = '\0';
        } else {
            dirp->entry.d_name[len] = '\0';
        }
    }
    
    return &dirp->entry;
}

// Windows directory reading implementations (ANSI versions for backward compatibility)
DIR *win32_opendir(const char *dirname) {
    if (!dirname) {
        return NULL;
    }
    
    // Check if dirname is too long to safely append "\\*" and null terminator
    // We need: strlen(dirname) + strlen("\\*") + 1 (null terminator) <= MAX_PATH
    if (strlen(dirname) + 3 > MAX_PATH) {
        // Handle error: dirname too long
        return NULL;
    }
    
    DIR *dirp = malloc(sizeof(DIR));
    if (!dirp) {
        return NULL;
    }
    
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", dirname);
    
    dirp->handle = FindFirstFileA(search_path, (LPWIN32_FIND_DATAA)&dirp->find_data);
    if (dirp->handle == INVALID_HANDLE_VALUE) {
        free(dirp);
        return NULL;
    }
    
    dirp->first = 1;
    return dirp;
}

struct dirent *win32_readdir(DIR *dirp) {
    if (!dirp || dirp->handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    
    if (dirp->first) {
        dirp->first = 0;
    } else {
        if (!FindNextFileA(dirp->handle, (LPWIN32_FIND_DATAA)&dirp->find_data)) {
            return NULL;
        }
    }
    
    strncpy(dirp->entry.d_name, ((LPWIN32_FIND_DATAA)&dirp->find_data)->cFileName, sizeof(dirp->entry.d_name) - 1);
    dirp->entry.d_name[sizeof(dirp->entry.d_name) - 1] = '\0';
    
    return &dirp->entry;
}

int win32_closedir(DIR *dirp) {
    if (!dirp) {
        return -1;
    }
    
    if (dirp->handle != INVALID_HANDLE_VALUE) {
        FindClose(dirp->handle);
    }
    free(dirp);
    return 0;
}

int win32_dirfd(DIR *dirp) {
    // Windows doesn't have file descriptors for directories like Unix
    // Return a dummy value - this might need adjustment based on usage
    return -1;
}

int win32_fstatat(int dirfd, const char *pathname, struct stat *buf, int flags) {
    // Since Windows doesn't have directory file descriptors like Unix,
    // we need to get the current directory path and construct the full path.
    // This is a limitation of our Windows compatibility layer.
    
    if (!pathname || !buf) {
        return -1;
    }
    
    // If pathname is already absolute, use Unicode lstat directly
    if (pathname[0] == '/' || pathname[0] == '\\' ||
        (pathname[0] && pathname[1] == ':')) {
        return win32_lstat(pathname, buf);
    }
    
    // For relative paths, we need to get the current working directory
    // and construct the full path using Unicode functions
    wchar_t wfull_path[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, wfull_path) == 0) {
        return -1;
    }
    
    // Convert pathname to wide string
    wchar_t *wpathname = win32_utf8_to_wchar(pathname);
    if (!wpathname) {
        // Fallback to ANSI version
        char full_path[MAX_PATH];
        if (GetCurrentDirectoryA(sizeof(full_path), full_path) == 0) {
            return -1;
        }
        
        size_t len = strlen(full_path);
        if (len > 0 && full_path[len-1] != '\\' && full_path[len-1] != '/') {
            if (len + 1 >= MAX_PATH) {
                return -1;
            }
            strncat(full_path, "\\", MAX_PATH - len - 1);
        }
        
        if (len + strlen(pathname) >= MAX_PATH) {
            return -1;
        }
        strncat(full_path, pathname, MAX_PATH - len - 1);
        
        return stat(full_path, buf);
    }
    
    // Append path separator if needed
    size_t wlen = wcslen(wfull_path);
    if (wlen > 0 && wfull_path[wlen-1] != L'\\' && wfull_path[wlen-1] != L'/') {
        if (wlen + 1 >= MAX_PATH) {
            win32_free_wchar(wpathname);
            return -1;
        }
        wcscat(wfull_path, L"\\");
        wlen++;
    }
    
    // Append the filename
    if (wlen + wcslen(wpathname) >= MAX_PATH) {
        win32_free_wchar(wpathname);
        return -1;
    }
    wcscat(wfull_path, wpathname);
    win32_free_wchar(wpathname);
    
    // Use Unicode stat
    return _wstat64(wfull_path, (struct _stat64*)buf);
}

#endif // _WIN32
