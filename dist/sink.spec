
Name:           sink
Version:        0.1
Release:	    2%{?dist}
Summary:        sink

Group:          Applications/Desktop
License:        GPL
URL:            https://docs.kolab.org/about/sink
Source0:        sink-%{version}.tar.gz

BuildRequires:  cmake >= 2.8.12
BuildRequires:  extra-cmake-modules
BuildRequires:  flatbuffers-devel >= 1.4
BuildRequires:  gcc-c++
BuildRequires:  kasync-devel
BuildRequires:  kf5-kcoreaddons-devel
BuildRequires:  kf5-kcontacts-devel
BuildRequires:  kmime-devel
BuildRequires:  kimap2-devel
BuildRequires:  libcurl-devel
BuildRequires:  libgit2-devel
BuildRequires:  lmdb-devel
BuildRequires:  qt5-qtbase-devel
BuildRequires:  readline-devel
BuildRequires:  kdav-devel

%description
sink

%package devel
Summary:        Development headers for sink
Requires:       %{name}

%description devel
Development headers for sink

%prep
%setup -q

sed -i \
    -e '/inspectiontest/d' \
    -e '/maildirresourcetest/d' \
    tests/CMakeLists.txt

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

#rm -rf %{buildroot}%{_prefix}/mkspecs/modules/qt_KMime.pri
rm %{buildroot}%{_prefix}/bin/resetmailbox.sh
rm %{buildroot}%{_prefix}/bin/populatemailbox.sh
rm %{buildroot}%{_prefix}/bin/sink_smtp_test

%files
%doc
%{_bindir}/hawd
%{_bindir}/sink_synchronizer
%{_bindir}/sinksh
%{_libdir}/liblibhawd.so
%{_libdir}/libmaildir.so
%{_libdir}/libsink.so.*
%dir %{_libdir}/qt5/plugins/
%{_libdir}/qt5/plugins/sink/

%files devel
%defattr(-,root,root,-)
%{_includedir}/sink/
%{_libdir}/cmake/Sink
%{_libdir}/libsink.so

%changelog
