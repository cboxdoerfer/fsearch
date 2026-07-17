%global giturl  https://github.com/cboxdoerfer/fsearch

Name:    fsearch
Summary: A fast file search utility for Unix-like systems based on GTK 3
Epoch:   1
Version: 0.3.1
Release: 1%{?dist}
License: GPL-2.0-or-later
URL:     https://github.com/cboxdoerfer/fsearch
Source0: %{giturl}/archive/%{version}/%{name}-%{version}.tar.gz


BuildRequires: meson
BuildRequires: ninja-build
BuildRequires: gcc
BuildRequires: gtk3-devel
BuildRequires: glib2-devel
BuildRequires: appstream
BuildRequires: desktop-file-utils
BuildRequires: itstool


%description
FSearch is a fast file search utility, inspired by Everything Search Engine. It's written in C and based on GTK 3.

%prep
%setup -q -n fsearch-%{version} -c

mv fsearch-%{version} build

%build
export LDFLAGS="%{?__global_ldflags} -pthread"
pushd build
%meson -Dchannel=copr-stable
%meson_build -v
popd

%install
pushd build
%meson_install

desktop-file-install \
  --dir=%{buildroot}%{_datadir}/applications/ \
  %{buildroot}%{_datadir}/applications/io.github.cboxdoerfer.FSearch.desktop
popd

%find_lang %{name} --with-gnome

%files -f %{name}.lang
%{_bindir}/fsearch
%{_datadir}/applications/io.github.cboxdoerfer.FSearch.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.github.cboxdoerfer.FSearch.svg
%{_datadir}/man/man1/fsearch.1.gz
%{_datadir}/metainfo/io.github.cboxdoerfer.FSearch.metainfo.xml

%changelog
* Fri Jul 17 2026 Christian Boxdörfer <christian.boxdoerfer@posteo.de> - 1:0.3.1-1
- Fix blocking UI when loading icons
- Fix option 'action after file open' not getting saved
- Remove duplicate language to fix build on newer gettext versions
* Sun Jul 12 2026 Christian Boxdörfer <christian.boxdoerfer@posteo.de> - 1:0.3-1
- DB rewrite
- DB: Add support for filesystem monitoring
- DB: Allow files and folders to be excluded based on fixed patterns, wildcards or regular expressions
- DB: Allow included folders to be scheduled and scanned after launch independently
- DB: Make rescanning more efficient
- DB: Store database config in DB file
- DB: Add checksum to DB file
- Make icon and file info gathering asynchronous (no UI blocking)
- Reduce memory usage
- Numerous bug fixes and stability and performance improvements