# Copyright 2013-2020 Lawrence Livermore National Security, LLC and other
# Spack Project Developers. See the top-level COPYRIGHT file for details.
#
# SPDX-License-Identifier: (Apache-2.0 OR MIT)

from spack import *


class MpasOScorpio(CMakePackage):
    """Software for Caching Output and Reads for Parallel I/O (SCORPIO)"""

    homepage = "https://github.com/E3SM-Project/scorpio.git"
    url      = "https://github.com/E3SM-Project/scorpio.git"
    git      = "https://github.com/E3SM-Project/scorpio.git"

    version('master', branch='master')

    depends_on('mpich@4')
    depends_on('hdf5+mpi+hl', type='link')
    depends_on('netcdf-c+mpi', type='link')
    depends_on('parallel-netcdf', type='link')

    variant("netcdf", default=True, description="Build with NetCDF")
    variant("hdf5", default=False, description="Build with HDF5")
    variant("tests", default=False, description="Build tests")
    variant("examples", default=False, description="Build examples")

    def cmake_args(self):
        args = ['-DCMAKE_C_COMPILER=%s' % self.spec['mpich'].mpicc,
                '-DCMAKE_CXX_COMPILER=%s' % self.spec['mpich'].mpicxx,
                '-DCMAKE_FC_COMPILER=%s' % self.spec['mpich'].mpifc,
                '-DBUILD_SHARED_LIBS=true',
                '-DPIO_USE_MALLOC=true',
                '-DCMAKE_C_FLAGS=-fPIC',
                '-DCMAKE_CXX_FLAGS=-fPIC',
                '-DPIO_ENABLE_TIMING=false',
                self.define_from_variant("WITH_NETCDF", "netcdf"),
                self.define_from_variant("WITH_HDF5", "hdf5"),
                self.define_from_variant("PIO_ENABLE_TESTS", "tests"),
                self.define_from_variant("PIO_ENABLE_EXAMPLES", "tests"),
                '-DHDF5_PATH=%s' % self.spec['hdf5'].prefix,
                '-DNetCDF_PATH=%s' % self.spec['netcdf-c'].prefix,
                '-DPnetCDF_PATH=%s' % self.spec['parallel-netcdf'].prefix]

        return args
