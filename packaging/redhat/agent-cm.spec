#
#    agent-cm - Provides computation services on METRICS
#
#    Copyright (C) 2016 Eaton
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

Name:           agent-cm
Version:        0.7.0
Release:        1
Summary:        provides computation services on metrics
License:        GPL-2.0+
URL:            https://eaton.com/
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkg-config
BuildRequires:  systemd-devel
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel
BuildRequires:  malamute-devel
BuildRequires:  libbiosproto-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
agent-cm provides computation services on metrics.

%package -n libagent_cm0
Group:          System/Libraries
Summary:        provides computation services on metrics

%description -n libagent_cm0
agent-cm provides computation services on metrics.
This package contains shared library.

%post -n libagent_cm0 -p /sbin/ldconfig
%postun -n libagent_cm0 -p /sbin/ldconfig

%files -n libagent_cm0
%defattr(-,root,root)
%doc COPYING
%{_libdir}/libagent_cm.so.*

%package devel
Summary:        provides computation services on metrics
Group:          System/Libraries
Requires:       libagent_cm0 = %{version}
Requires:       zeromq-devel
Requires:       czmq-devel
Requires:       malamute-devel
Requires:       libbiosproto-devel

%description devel
agent-cm provides computation services on metrics.
This package contains development files.

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libagent_cm.so
%{_libdir}/pkgconfig/libagent_cm.pc

%prep
%setup -q

%build
sh autogen.sh
%{configure} --with-systemd-units
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%doc README.md
%{_bindir}/bios-agent-cm
%{_prefix}/lib/systemd/system/bios-agent-cm*.service
/usr/lib/tmpfiles.d/bios-agent-cm.conf


%changelog
