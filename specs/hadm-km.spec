# taken from drbd-km.spec

# "uname -r" output of the kernel to build for, the running one
# if none was specified with "--define 'kernelversion <uname -r>'"
# PLEASE: provide both (correctly) or none!!
%{!?kernelversion: %{expand: %%define kernelversion %(uname -r)}}
%{!?kdir: %{expand: %%define kdir /lib/modules/%{kernelversion}/build}}

# encode - to _ to be able to include that in a package name or release "number"
%global krelver  %(echo %{kernelversion} | tr -s '-' '_')

Name:           hadm-km
Version:        2.0.0_%{krelver}
Release:        1%{?dist}
Summary:        hadm kernel module

Group:          System Environment/Kernel
License:        Commercial
URL:            http://skybility.com/product/ha.php
Source:         hadm-2.0.0.tar.gz
Vendor:         Skybility
#BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  gcc

%description
hadm block driver

%prep
%setup -q -n hadm-2.0.0


%build
./autogen.sh
./configure
cd kmod
make KDIR=%{kdir}


%install
cd kmod
mkdir -p $RPM_BUILD_ROOT/lib/modules/%{kernelversion}/kernel/drivers/block
cp hadm_kmod.ko $RPM_BUILD_ROOT/lib/modules/%{kernelversion}/kernel/drivers/block

%clean
rm -rf $RPM_BUILD_ROOT

%files
/lib/modules/%{kernelversion}/kernel

%post
EXTRA_HADM_KO=/lib/modules/%{kernelversion}/kernel/drivers/block/hadm_kmod.ko
real_km=`uname -r`
if  [ $real_km != %{kernelversion} ]; then
    if [ ! -f /lib/modules/$real_km/kernel/drivers/block/hadm_kmod.ko ] ; then
        mkdir -p /lib/modules/$real_km/kernel/drivers/block
        cp $EXTRA_HADM_KO /lib/modules/$real_km/kernel/drivers/block/hadm_kmod.ko
        depmod -a
    fi
fi

uname -r | grep BOOT ||
/sbin/depmod -a -F /boot/System.map-${real_km} ${real_km} >/dev/null 2>&1 || true

%postun
real_km=`uname -r`
/sbin/depmod -a -F /boot/System.map-${real_km} ${real_km} >/dev/null 2>&1 || true
if  [ "$1" -ne "1" ]; then
    if  [ $real_km != %{kernelversion} ]; then
        if [ -f /lib/modules/$real_km/kernel/drivers/block/hadm_kmod.ko ] ; then
            rm -f /lib/modules/$real_km/kernel/drivers/block/hadm_kmod.ko
        fi
    fi
fi

%changelog
