
Name:           sink
Version:        0.9
Release:        0%{?dist}
Summary:        sink

Group:          Applications/Desktop
License:        GPL
URL:            https://phabricator.kde.org/project/profile/5/
Source0:        sink-%{version}.tar.xz

BuildRequires:  cmake >= 2.8.12
BuildRequires:  extra-cmake-modules
BuildRequires:  flatbuffers-devel >= 1.4
BuildRequires:  gcc-c++
BuildRequires:  kasync-devel
BuildRequires:  kf5-kcoreaddons-devel
BuildRequires:  kf5-kcontacts-devel
BuildRequires:  kf5-kmime-devel
BuildRequires:  kf5-kcalendarcore-devel
BuildRequires:  kimap2-devel >= 0.3
BuildRequires:  kdav2-devel >= 0.3
BuildRequires:  libcurl-devel
BuildRequires:  libgit2-devel
BuildRequires:  lmdb-devel
BuildRequires:  qt5-qtbase-devel
BuildRequires:  xapian-core-devel >= 1.4

%description
sink

%package devel
Summary:        Development headers for sink
Requires:       %{name}

%description devel
Development headers for sink

%prep
%setup -q

%build
mkdir -p build/
pushd build
%{cmake} \
    -DQT_PLUGIN_INSTALL_DIR:PATH=%{_libdir}/qt5/plugins/ \
    ..

make %{?_smp_mflags}
popd

%install
pushd build
%make_install
popd

rm %{buildroot}%{_prefix}/bin/resetmailbox.sh
rm %{buildroot}%{_prefix}/bin/populatemailbox.sh
rm %{buildroot}%{_prefix}/bin/sink_smtp_test

%files
%doc
%{_bindir}/hawd
%{_bindir}/sink_synchronizer
%{_bindir}/sinksh
%{_libdir}/liblibhawd.so
%{_libdir}/libsink.so.*
%dir %{_libdir}/qt5/plugins/
%{_libdir}/qt5/plugins/sink/

%files devel
%defattr(-,root,root,-)
%{_includedir}/sink/
%{_libdir}/cmake/Sink
%{_libdir}/libsink.so

%changelog
