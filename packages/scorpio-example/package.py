# Copyright 2013-2020 Lawrence Livermore National Security, LLC and other
# Spack Project Developers. See the top-level COPYRIGHT file for details.
#
# SPDX-License-Identifier: (Apache-2.0 OR MIT)

from spack import *


class ScorpioExample(CMakePackage):
    """Example of Testing Scorpio with LowFive."""

    # this is the actual public repo of the E3SM-Project (use this)
    homepage = "https://github.com/tpeterka/scorpio-example.git"
    url      = "https://github.com/tpeterka/scorpio-example.git"
    git      = "https://github.com/tpeterka/scorpio-example.git"

    version('master', branch='master')

    depends_on('mpich')
    depends_on('mpas-o-scorpio+hdf5', type='link')
    depends_on('hdf5+mpi+hl@1.12.1 ^mpich', type='link')
    depends_on('netcdf-c+mpi', type='link')
    depends_on('parallel-netcdf', type='link')
    depends_on('lowfive@master', type='link')

    def cmake_args(self):
        args = ['-DCMAKE_C_COMPILER=%s' % self.spec['mpich'].mpicc,
                '-DCMAKE_CXX_COMPILER=%s' % self.spec['mpich'].mpicxx,
                '-DBUILD_SHARED_LIBS=false',
                '-DNETCDF_PATH=%s' % self.spec['netcdf-c'].prefix,
                '-DPNETCDF_PATH=%s' % self.spec['parallel-netcdf'].prefix,
                '-DLOWFIVE_PATH=%s' % self.spec['lowfive'].prefix,
                '-DSCORPIO_PATH=%s' % self.spec['mpas-o-scorpio'].prefix]
        return args
