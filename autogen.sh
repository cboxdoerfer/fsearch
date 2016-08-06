#! /bin/sh

glib-gettextize --force --copy || exit 1
intltoolize --force --copy --automake || exit 1
autoreconf --force --install -Wno-portability || exit 1
