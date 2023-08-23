#pragma once

#include    <vector>
#include    <cassert>
#include    <thread>
#include    <mutex>

#include    <fmt/format.h>

#include    <hdf5.h>

#ifdef      LOWFIVE_PATH

#include    <lowfive/vol-metadata.hpp>
#include    <lowfive/vol-dist-metadata.hpp>
#include    <lowfive/log.hpp>
#include    <lowfive/H5VOLProperty.hpp>
#include    <lowfive/log.hpp>

namespace l5        = LowFive;

#endif

using communicator  = MPI_Comm;
using diy_comm      = diy::mpi::communicator;


enum {producer_task, producer1_task, producer2_task, consumer_task, consumer1_task, consumer2_task};

