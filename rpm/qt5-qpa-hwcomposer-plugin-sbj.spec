Name:       qt5-qpa-hwcomposer-plugin-sbj
Summary:    Qt 5 QPA hwcomposer plugin (sbj variant)
Version:    5.1.0+hwc7
Release:    1
Group:      Qt/Qt
License:    LGPLv2.1 with exception or GPLv3
URL:        http://github.com/mer-hybris/qt5-qpa-hwcomposer-plugin
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5PlatformSupport)
BuildRequires:  pkgconfig(egl)
BuildRequires:  libhybris-sbj-libGLESv2
BuildRequires:  libhybris-sbj-libGLESv2-devel
BuildRequires:  libhybris-sbj-libwayland-egl
BuildRequires:  libhybris-sbj-libwayland-egl-devel
BuildRequires:  pkgconfig(libhardware)
# Comment out the libsync dependency for old hw adaptations
#BuildRequires:  pkgconfig(libsync)
BuildRequires:  pkgconfig(hybris-egl-platform)
BuildRequires:  pkgconfig(android-headers)
BuildRequires:  qt5-qtwayland-wayland_egl-devel
BuildRequires:  wayland-devel
BuildRequires:  pkgconfig(udev)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(mtdev)
Provides: qt5-eglfs-qcom-hwcomposer-plugin

%description
This package contains a Qt 5 QPA plugin using libhybris' Droid
hwcomposer for composing content onto the screen.

%prep
%setup -q

%build
export QTDIR=/usr/share/qt5
cd hwcomposer
%qmake5
make %{_smp_mflags}

%install
rm -rf %{buildroot}
cd hwcomposer
%qmake5_install

# doesn't exist on Qt 5.1, we don't currently care about this for 5.2
rm -f %{buildroot}/usr/lib/cmake/Qt5Gui/Qt5Gui_QEglFSIntegrationPlugin.cmake

%files
%defattr(-,root,root,-)
%{_libdir}/qt5/plugins/platforms/libhwcomposer.so
