#include <diy/mpi/communicator.hpp>
#include <thread>
#include "prod-con.hpp"

#include <netcdf.h>
#include <pio.h>

#define MAX_DIMS 10

herr_t fail_on_hdf5_error(hid_t stack_id, void*)
{
    H5Eprint(stack_id, stderr);
    fprintf(stderr, "An HDF5 error was detected. Terminating.\n");
    exit(1);
}

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

    int                     ioproc_stride   = 1;
    int                     ioproc_start    = 0;
    int                     iosysid;
    int                     ncid;
    int                     format          = PIO_IOTYPE_NETCDF4P;
    PIO_Offset              elements_per_pe;
    int                     ioid;
    int                     ndims;
    int                     varid1          = -1;
    int                     varid2          = -1;
    std::vector<PIO_Offset> compdof;
    std::vector<int>        dim_len(MAX_DIMS);
    std::vector<int>        dimid_v1(MAX_DIMS);
    std::vector<int>        dimid_v2(MAX_DIMS);

    // debug
    fmt::print(stderr, "consumer: local comm rank {} size {}\n", local_.rank(), local_.size());

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

    // set Scorpio log level
    PIOc_set_log_level(5);

    // set HDF5 error handler
    H5Eset_auto(H5E_DEFAULT, fail_on_hdf5_error, NULL);

    // init PIO
    PIOc_Init_Intracomm(local, local_.size(), ioproc_stride, ioproc_start, PIO_REARR_SUBSET, &iosysid);

    // open file for reading
    PIOc_openfile(iosysid, &ncid, &format, "example1.nc", PIO_NOWRITE);

    // variable sizes
    int ntime_steps = 3;
    dim_len[0]  = 128;
    dim_len[1]  = 128;

    //  ------ variable v1 -----

    // decomposition
    elements_per_pe = dim_len[0] / local_.size();
    compdof.resize(elements_per_pe);
    for (int i = 0; i < elements_per_pe; i++)
        compdof[i] = local_.rank() * elements_per_pe + i + 1;        // scorpio's compdof starts at 1, not 0
    PIOc_InitDecomp(iosysid, PIO_INT, 1, &dim_len[0], (PIO_Offset)elements_per_pe, &compdof[0], &ioid, NULL, NULL, NULL);

    // read the metadata (get variable ID)
    PIOc_inq_varid(ncid, "v1", &varid1);

    // debug
    fmt::print(stderr, "*** consumer after inquiring variable ID {} for v1 ***\n", varid1);

    // read the data
    std::vector<int> v1(elements_per_pe);
    PIOc_read_darray(ncid, varid1, ioid, (PIO_Offset)elements_per_pe, &v1[0]);
    // check the data values
    for (int i = 0; i < elements_per_pe; i++)
    {
        if (v1[i] != local_.rank() * elements_per_pe + i)
        {
            fmt::print(stderr, "*** consumer error: v1[{}] = {} which should be {} ***\n", i, v1[i], local_.rank() * elements_per_pe + i);
            abort();
        }
    }

    // -------- v2 --------

    // decomposition
    elements_per_pe = dim_len[0] * dim_len[1] / local_.size();
    compdof.resize(elements_per_pe);
    for (int i = 0; i < elements_per_pe; i++)
        compdof[i] = local_.rank() * elements_per_pe + i + 1;        // adding 1 fixes a scorpio bug I don't understand
    PIOc_InitDecomp(iosysid, PIO_DOUBLE, 2, &dim_len[0], (PIO_Offset)elements_per_pe, &compdof[0], &ioid, NULL, NULL, NULL);

    // read the metadata (get variable ID)
    PIOc_inq_varid(ncid, "v2", &varid2);

    // debug
    fmt::print(stderr, "*** consumer after inquiring variable ID {} for v2  ***\n", varid2);

    // read the data
    std::vector<double> v2(elements_per_pe);
    for (auto t = 0; t < ntime_steps; t++)      // for all timesteps
    {
        PIOc_setframe(ncid, varid2, t);
        PIOc_read_darray(ncid, varid2, ioid, (PIO_Offset)elements_per_pe, &v2[0]);

        // check the data values
        for (int i = 0; i < elements_per_pe; i++)
        {
            if (v2[i] != t * elements_per_pe * local_.size() + local_.rank() * elements_per_pe + i)
            {
                fmt::print(stderr, "*** consumer error: v2[{}] = {} which should be {} ***\n", i, v2[i], t * elements_per_pe * local_.size() + local_.rank() * elements_per_pe + i);
                abort();
            }
        }
    }

    // clean up
    PIOc_closefile(ncid);
    PIOc_freedecomp(iosysid, ioid);
    PIOc_finalize(iosysid);
    if (!shared)
        H5Pclose(plist);

    // debug
    fmt::print(stderr, "*** consumer after closing file ***\n");
}

