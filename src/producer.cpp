#include <diy/mpi/communicator.hpp>
#include "prod-con.hpp"

#include <pio.h>

#define MAX_DIMS 10

herr_t fail_on_hdf5_error(hid_t stack_id, void*)
{
    H5Eprint(stack_id, stderr);
    fprintf(stderr, "An HDF5 error was detected. Terminating.\n");
    exit(1);
}

extern "C" {
void producer_f (
        communicator& local,
        const std::vector<communicator>& intercomms,
        bool shared,
        int metadata,
        int passthru);
}

void producer_f (
        communicator& local,
        const std::vector<communicator>& intercomms,
        bool shared,
        int metadata,
        int passthru)
{
    diy::mpi::communicator local_(local);

    int                     ioproc_stride   = 1;
    int                     ioproc_start    = 0;
    int                     iosysid;
    int                     ncid;
    int                     format          = PIO_IOTYPE_NETCDF4P;
    PIO_Offset              elements_per_pe;
    int                     ioid;
    int                     ndims;
    int                     varid           = -1;
    std::vector<int>        dimid(MAX_DIMS);
    std::vector<PIO_Offset> compdof;
    std::vector<int>        dim_len(MAX_DIMS);
    std::vector<std::string>    dim_name(MAX_DIMS);

    // debug
    fmt::print(stderr, "producer: local comm rank {} size {}\n", local_.rank(), local_.size());

    // VOL plugin and properties
    hid_t plist;

    if (shared)                 // single process, MetadataVOL test
        fmt::print(stderr, "producer: using shared mode MetadataVOL plugin created by prod-con\n");
    else                        // normal multiprocess, DistMetadataVOL plugin
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
    }

    // set Scorpio log level
//     PIOc_set_log_level(5);

    // set HDF5 error handler
    H5Eset_auto(H5E_DEFAULT, fail_on_hdf5_error, NULL);

    // init PIO
    PIOc_Init_Intracomm(local, local_.size(), ioproc_stride, ioproc_start, PIO_REARR_SUBSET, &iosysid);

    // create file
    PIOc_createfile(iosysid, &ncid, &format, "example1.nc", PIO_CLOBBER);

    // define variables
    dim_name[0] = "t";
    dim_name[1] = "x";
    dim_name[2] = "y";
    dim_len[0]  = 3;
    dim_len[1]  = 128;
    dim_len[2]  = 128;
    PIOc_def_dim(ncid, dim_name[1].c_str(), dim_len[1], &dimid[1]);
    PIOc_def_var(ncid, "v1", PIO_INT, 1, &dimid[1], &varid);
    for (int d = 0; d < 3; d++)
        PIOc_def_dim(ncid, dim_name[d].c_str(), dim_len[d], &dimid[d]);
    PIOc_def_var(ncid, "v2", PIO_INT, 3, &dimid[0], &varid);
    PIOc_enddef(ncid);

    //  ------ variable v1 -----

    // decomposition
    elements_per_pe = dim_len[0] / local_.size();
    compdof.resize(elements_per_pe);

    for (int i = 0; i < elements_per_pe; i++)
        compdof[i] = local_.rank() * elements_per_pe + i + 1;        // adding 1 fixes a scorpio bug I don't understand

    PIOc_InitDecomp(iosysid, PIO_INT, 1, &dim_len[1], (PIO_Offset)elements_per_pe, &compdof[0], &ioid, NULL, NULL, NULL);

    // write the data
    std::vector<int> v1(elements_per_pe);
    for (int i = 0; i < elements_per_pe; i++)
        v1[i] = local_.rank() * elements_per_pe + i;
    PIOc_write_darray(ncid, varid, ioid, (PIO_Offset)elements_per_pe, &v1[0], NULL);

    PIOc_freedecomp(iosysid, ioid);

    // -------- v2 --------

    // decomposition
    // even though it's a 3d dataspace, time is taken separately, and the decomposition is the
    // remaining 2d dimensions
    elements_per_pe = dim_len[1] * dim_len[2] / local_.size();
    compdof.resize(elements_per_pe);

    for (int i = 0; i < elements_per_pe; i++)
        compdof[i] = local_.rank() * elements_per_pe + i + 1;        // adding 1 fixes a scorpio bug I don't understand

    // starting dim_len at index 1 because index 0 is the time step
    PIOc_InitDecomp(iosysid, PIO_DOUBLE, 2, &dim_len[1], (PIO_Offset)elements_per_pe, &compdof[0], &ioid, NULL, NULL, NULL);

    // write the data
    std::vector<double> v2(elements_per_pe);
    for (auto t = 0; t < dim_len[0]; t++)      // for all timesteps
    {
        for (int i = 0; i < elements_per_pe; i++)
            v2[i] = t * elements_per_pe * local_.size() + local_.rank() * elements_per_pe + i;;
        PIOc_setframe(ncid, varid, t);
        PIOc_write_darray(ncid, varid, ioid, (PIO_Offset)elements_per_pe, &v2[0], NULL);
    }

    PIOc_freedecomp(iosysid, ioid);

    // debug
    fmt::print(stderr, "*** producer before closing file ***\n");

    // clean up
    PIOc_closefile(ncid);
    PIOc_freedecomp(iosysid, ioid);
    PIOc_finalize(iosysid);

    // debug
    fmt::print(stderr, "*** producer after closing file ***\n");

    if (!shared)
        H5Pclose(plist);

    // signal the consumer that data are ready
    if (passthru && !metadata && !shared)
    {
        for (auto& intercomm: intercomms)
            diy_comm(intercomm).barrier();
    }
}
