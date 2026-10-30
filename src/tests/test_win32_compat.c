#ifdef _WIN32

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../win32_compat.h"

// Test the strptime implementation
static void test_strptime() {
    struct tm tm;
    char *result;
    
    // Test ISO date format
    memset(&tm, 0, sizeof(tm));
    result = win32_strptime("2023-12-25", "%Y-%m-%d", &tm);
    assert(result != NULL);
    assert(tm.tm_year == 123); // 2023 - 1900
    assert(tm.tm_mon == 11);   // December (0-based)
    assert(tm.tm_mday == 25);
    
    // Test ISO datetime format
    memset(&tm, 0, sizeof(tm));
    result = win32_strptime("2023-12-25 14:30:45", "%Y-%m-%d %H:%M:%S", &tm);
    assert(result != NULL);
    assert(tm.tm_year == 123);
    assert(tm.tm_mon == 11);
    assert(tm.tm_mday == 25);
    assert(tm.tm_hour == 14);
    assert(tm.tm_min == 30);
    assert(tm.tm_sec == 45);
    
    // Test unsupported format
    memset(&tm, 0, sizeof(tm));
    result = win32_strptime("Dec 25, 2023", "%b %d, %Y", &tm);
    assert(result == NULL);
    
    printf("strptime tests passed\n");
}

// Test the fnmatch implementation
static void test_fnmatch() {
    // Test exact match
    assert(win32_fnmatch("hello", "hello", 0) == 0);
    assert(win32_fnmatch("hello", "world", 0) == FNM_NOMATCH);
    
    // Test wildcard
    assert(win32_fnmatch("*.txt", "file.txt", 0) == 0);
    assert(win32_fnmatch("*.txt", "file.doc", 0) == FNM_NOMATCH);
    assert(win32_fnmatch("test*", "testing", 0) == 0);
    assert(win32_fnmatch("test*", "best", 0) == FNM_NOMATCH);
    
    // Test question mark
    assert(win32_fnmatch("test?", "test1", 0) == 0);
    assert(win32_fnmatch("test?", "test", 0) == FNM_NOMATCH);
    assert(win32_fnmatch("test?", "test12", 0) == FNM_NOMATCH);
    
    // Test case insensitive (Windows default)
    assert(win32_fnmatch("Hello", "hello", 0) == 0);
    assert(win32_fnmatch("HELLO", "hello", 0) == 0);
    
    // Test path separators with FNM_PATHNAME
    assert(win32_fnmatch("*/test", "dir/test", FNM_PATHNAME) == 0);
    assert(win32_fnmatch("*/test", "dir\\test", FNM_PATHNAME) == 0);
    
    printf("fnmatch tests passed\n");
}

// Test the strcasestr implementation
static void test_strcasestr() {
    const char *haystack = "Hello World";
    
    assert(win32_strcasestr(haystack, "hello") != NULL);
    assert(win32_strcasestr(haystack, "WORLD") != NULL);
    assert(win32_strcasestr(haystack, "xyz") == NULL);
    assert(win32_strcasestr(haystack, "") == haystack);
    
    printf("strcasestr tests passed\n");
}

// Test UTF-8 conversion functions
static void test_utf8_conversion() {
    const char *utf8_str = "Hello 世界";
    
    // Convert to wide string and back
    wchar_t *wstr = win32_utf8_to_wchar(utf8_str);
    assert(wstr != NULL);
    
    char *utf8_back = win32_wchar_to_utf8(wstr);
    assert(utf8_back != NULL);
    assert(strcmp(utf8_str, utf8_back) == 0);
    
    win32_free_wchar(wstr);
    win32_free_utf8(utf8_back);
    
    // Test NULL handling
    assert(win32_utf8_to_wchar(NULL) == NULL);
    assert(win32_wchar_to_utf8(NULL) == NULL);
    
    printf("UTF-8 conversion tests passed\n");
}

int main() {
    printf("Running Win32 compatibility tests...\n");
    
    test_strptime();
    test_fnmatch();
    test_strcasestr();
    test_utf8_conversion();
    
    printf("All Win32 compatibility tests passed!\n");
    return 0;
}

#else

#include <stdio.h>

int main() {
    printf("Win32 compatibility tests skipped (not on Windows)\n");
    return 0;
}

#endif