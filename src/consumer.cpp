#include <diy/mpi/communicator.hpp>
#include <thread>
#include "prod-con.hpp"

#include <netcdf.h>
#include <pio.h>

#define NDIM 1
#define DIM_LEN 1024
#define DIM_NAME "x"
#define VAR_NAME "foo"
#define START_DATA_VAL 100

extern "C"
{
void consumer_f (
        communicator& local,
        const std::vector<communicator>& intercomms,
        bool shared,
        int metadata,
        int passthru);
}

void consumer_f (
        communicator& local,
        const std::vector<communicator>& intercomms,
        bool shared,
        int metadata,
        int passthru)
{
    diy::mpi::communicator local_(local);

    // enable netCDF logging
    int level = 5;      // 1: min, 5: max
    nc_set_log_level(level);

    // PIO defs
    int my_rank = local_.rank();
    int ntasks  = local_.size();
    int ioproc_stride = 1;
    int ioproc_start = 0;
    int iosysid;
    int ncid;
    int format = PIO_IOTYPE_NETCDF4P;
    int *buffer = NULL;
    PIO_Offset elements_per_pe;
    PIO_Offset *compdof = NULL;
    int dim_len[1] = {DIM_LEN};
    int ioid;
    int varid = -1;

    // debug
    fmt::print(stderr, "consumer: local comm rank {} size {}\n", my_rank, ntasks);

    // wait for data to be ready
    if (passthru && !metadata && !shared)
    {
        for (auto& intercomm: intercomms)
            diy_comm(intercomm).barrier();
    }

    // VOL plugin and properties
    hid_t plist;

    if (shared)                     // single process, MetadataVOL test
        fmt::print(stderr, "consumer: using shared mode MetadataVOL plugin created by prod-con\n");
    else                            // normal multiprocess, DistMetadataVOL plugin
    {
        l5::DistMetadataVOL& vol_plugin = l5::DistMetadataVOL::create_DistMetadataVOL(local, intercomms);
        plist = H5Pcreate(H5P_FILE_ACCESS);

        if (passthru)
            H5Pset_fapl_mpio(plist, local, MPI_INFO_NULL);

        l5::H5VOLProperty vol_prop(vol_plugin);
        if (!getenv("HDF5_VOL_CONNECTOR"))
            vol_prop.apply(plist);

        // set lowfive properties
        if (passthru)
            vol_plugin.set_passthru("example1.nc", "*");
        if (metadata)
            vol_plugin.set_memory("example1.nc", "*");
        vol_plugin.set_intercomm("example1.nc", "*", 0);
    }

    // init PIO
    PIOc_Init_Intracomm(local, ntasks, ioproc_stride, ioproc_start, PIO_REARR_SUBSET, &iosysid);

    // decomposition
    elements_per_pe = DIM_LEN / ntasks;
    compdof = (MPI_Offset*)(malloc(elements_per_pe * sizeof(PIO_Offset)));

    for (int i = 0; i < elements_per_pe; i++)
        compdof[i] = (my_rank * elements_per_pe + i + 1) + 10;

    PIOc_InitDecomp(iosysid, PIO_INT, NDIM, dim_len, (PIO_Offset)elements_per_pe, compdof, &ioid, NULL, NULL, NULL);
    free(compdof);

    // debug
    fmt::print(stderr, "*** consumer before opening file ***\n");

    // open file for reading
    PIOc_openfile(iosysid, &ncid, &format, "example1.nc", PIO_NOWRITE);

    // debug
    fmt::print(stderr, "*** consumer after opening file and before inquiring variable ID ***\n");

    // read the metadata (get variable ID)
    PIOc_inq_varid(ncid, VAR_NAME, &varid);

    // debug
    fmt::print(stderr, "*** consumer after inquiring variable ID {} and before reading data ***\n", varid);

    // read the data
    buffer = (int*)(malloc(elements_per_pe * sizeof(int)));
    PIOc_read_darray(ncid, varid, ioid, (PIO_Offset)elements_per_pe, buffer);
    // check the data values
    for (int i = 0; i < elements_per_pe; i++)
    {
        if (buffer[i] != START_DATA_VAL + my_rank)
        {
            fmt::print(stderr, "*** consumer error: buffer[{}] = {} which should be {} ***\n", i, buffer[i], START_DATA_VAL + my_rank);
            abort();
        }
    }
    free(buffer);

    // debug
    fmt::print(stderr, "*** consumer after reading data and before closing file ***\n");

    // clean up
    PIOc_closefile(ncid);
    PIOc_freedecomp(iosysid, ioid);
    PIOc_finalize(iosysid);
    if (!shared)
        H5Pclose(plist);

    // debug
    fmt::print(stderr, "*** consumer after closing file ***\n");
}

