#include    <thread>

#include    <diy/master.hpp>
#include    <diy/decomposition.hpp>
#include    <diy/assigner.hpp>
#include    "opts.h"

#include    <dlfcn.h>

#include    "prod-con.hpp"

int main(int argc, char* argv[])
{
    int   dim = DIM;

    diy::mpi::environment     env(argc, argv, MPI_THREAD_MULTIPLE);
    diy::mpi::communicator    world;

    int                       mem_blocks        = -1;             // all blocks in memory
    int                       threads           = 1;              // no multithreading
    std::string               prefix            = "./DIY.XXXXXX"; // for saving block files out of core
    // opts does not handle bool correctly, using int instead
    bool                      shared            = false;          // producer and consumer run on the same ranks
    float                     prod_frac         = 1.0 / 3.0;      // fraction of world ranks in producer
    size_t                    local_npoints     = 1e6;            // points per block
    size_t                    local_ngrid_dim   = 1e2;            // grid vertices per dim per block
    std::string               producer_exec     = "./producer.so";    // name of producer executable
    std::string               consumer_exec     = "./consumer.so";    // name of consumer executable
    int                       ntrials           = 1;              // number of trials to run
    bool                      verbose, help;

    // get command line arguments
    using namespace opts;
    Options ops;
    ops
        >> Option('n', "number",    local_npoints,  "number of points per block")
        >> Option('t', "thread",    threads,        "number of threads")
        >> Option(     "memblks",   mem_blocks,     "number of blocks to keep in memory")
        >> Option(     "prefix",    prefix,         "prefix for external storage")
        >> Option('p', "p_frac",    prod_frac,      "fraction of world ranks in producer")
        >> Option('s', "shared",    shared,         "share ranks between producer and consumer (-p ignored)")
        >> Option('r', "prod_exec", producer_exec,  "name of producer executable")
        >> Option('c', "con_exec",  consumer_exec,  "name of consumer executable")
        >> Option('v', "verbose",   verbose,        "print the block contents")
        >> Option('h', "help",      help,           "show help")
        >> Option(     "ntrials",   ntrials,        "number of trials to run")
        ;

    if (!ops.parse(argc,argv) || help)
    {
        if (world.rank() == 0)
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
            std::cout << "Generates a grid and random particles in the domain and redistributes them into correct blocks.\n";
            std::cout << ops;
        }
        return 1;
    }

    int producer_ranks = world.size() * prod_frac;

    // default global data bounds
    Bounds domain { dim };
    // weak scaling wrt producer ranks
    size_t global_ngrid_dim = powf(producer_ranks * powf(local_ngrid_dim, dim), 1.0 / (float)dim);
    if (world.rank() == 0)
        fmt::print(stderr, "producer ranks {} local_npts {} global_npts {} local_ngrid {} global_ngrid {}\n",
                producer_ranks, local_npoints, local_npoints * producer_ranks, pow(local_ngrid_dim, dim), pow(global_ngrid_dim, dim));
    for (auto i = 0; i < dim; i++)
    {
        domain.min[i] = 0;
        domain.max[i] = global_ngrid_dim - 1;
    }

    // blocks, ranks, number of points (one block per rank)
    int prod_nblocks        = producer_ranks;
    bool producer           = world.rank() < producer_ranks;
    int con_nblocks         = world.size() - prod_nblocks;
    size_t global_npoints   = prod_nblocks * local_npoints;         // all block have same number of points

    if (!shared && world.rank() == 0)
        fmt::print(stderr, "space partitioning: producer_ranks: {} consumer_ranks: {}\n", producer_ranks, world.size() - producer_ranks);
    if (shared && world.rank() == 0)
        fmt::print(stderr, "space sharing: producer_ranks = consumer_ranks = world: {}\n", world.size());

    // load tasks
    void* lib_producer = dlopen(producer_exec.c_str(), RTLD_LAZY);
    if (!lib_producer)
        fmt::print(stderr, "Couldn't open producer.hx\n");

    void* lib_consumer = dlopen(consumer_exec.c_str(), RTLD_LAZY);
    if (!lib_consumer)
        fmt::print(stderr, "Couldn't open consumer.hx\n");

    void* producer_f_ = dlsym(lib_producer, "producer_f");
    if (!producer_f_)
        fmt::print(stderr, "Couldn't find producer_f\n");
    void* consumer_f_ = dlsym(lib_consumer, "consumer_f");
    if (!consumer_f_)
        fmt::print(stderr, "Couldn't find consumer_f\n");

    // communicator management
    using communicator = diy::mpi::communicator;
    MPI_Comm intercomm_;
    std::vector<communicator> producer_intercomms, consumer_intercomms;
    communicator p_c_intercomm;
    communicator local;
    communicator producer_comm, consumer_comm;
    producer_comm.duplicate(world);
    consumer_comm.duplicate(world);

    if (shared)
    {
        producer_comm.duplicate(world);
        consumer_comm.duplicate(world);
        p_c_intercomm.duplicate(world);
        producer_intercomms.push_back(p_c_intercomm);
        consumer_intercomms.push_back(p_c_intercomm);
    }
    else
    {
        local = world.split(producer);

        if (producer)
        {
            MPI_Intercomm_create(local, 0, world, /* remote_leader = */ producer_ranks, /* tag = */ 0, &intercomm_);
            producer_intercomms.push_back(communicator(intercomm_));
            producer_comm = local;
        }
        else
        {
            MPI_Intercomm_create(local, 0, world, /* remote_leader = */ 0, /* tag = */ 0, &intercomm_);
            consumer_intercomms.push_back(communicator(intercomm_));
            consumer_comm = local;
        }
    }

    std::mutex exclusive;

    // declare lambdas for the tasks

    auto producer_f = [&]()
    {
        ((void (*) (communicator&, const std::vector<communicator>&,
                    std::mutex&, bool,
                    std::string,
                    int, int,
                    Bounds, int, int,
                    int, int, size_t)) (producer_f_))(producer_comm,
                                                      producer_intercomms,
                                                      exclusive,
                                                      shared,
                                                      prefix,
                                                      threads,
                                                      mem_blocks,
                                                      domain,
                                                      prod_nblocks,
                                                      con_nblocks,
                                                      0,
                                                      dim,
                                                      local_npoints);
    };

    auto consumer_f = [&]()
    {
        ((void (*) (communicator&, const std::vector<communicator>&,
                    std::mutex&, bool,
                    std::string,
                    int, int,
                    Bounds, size_t, int, int,
                    int, int))(consumer_f_))(consumer_comm,
                                        consumer_intercomms,
                                        exclusive,
                                        shared,
                                        prefix,
                                        threads,
                                        mem_blocks,
                                        domain,
                                        global_npoints,
                                        dim,
                                        con_nblocks,
                                        prod_nblocks,
                                        0);
    };

    std::vector<double> times(ntrials);     // elapsed time for each trial
    double sum_time = 0.0;                  // sum of all times
    for (auto i = 0; i < ntrials; i++)
    {
        // timing
        world.barrier();
        double t0 = MPI_Wtime();

        if (!shared)
        {
            if (producer)
                producer_f();
            else
                consumer_f();
        } else
        {
            auto producer_thread = std::thread(producer_f);
            auto consumer_thread = std::thread(consumer_f);

            producer_thread.join();
            consumer_thread.join();
        }

        // timing
        world.barrier();
        times[i] = MPI_Wtime() - t0;
        sum_time += times[i];
        if (world.rank() == 0)
            fmt::print(stderr, "Elapsed time for trial {}\t\t{:.4f} s.\n", i, times[i]);
    }

    // timing stats
    double mean_time    = sum_time / ntrials;
    double var_time     = 0.0;
    for (auto i = 0; i < ntrials; i++)
        var_time += ((times[i] - mean_time) * (times[i] - mean_time));
    var_time /= ntrials;

    if (world.rank() == 0)
    {
        fmt::print(stderr, "\nMean elapsed time for {} trials\t\t{:.4f} s.\n", ntrials, mean_time);
        fmt::print(stderr, "Variance\t\t\t\t{:.4f}\n", var_time);
        fmt::print(stderr, "Standard deviation\t\t\t{:.4f}\n", sqrt(var_time));
        fmt::print(stderr, "Minimum\t\t\t\t\t{:.4f} s.\n", *(std::min_element(times.begin(), times.end())));
        fmt::print(stderr, "Maximum\t\t\t\t\t{:.4f} s.\n", *(std::max_element(times.begin(), times.end())));
    }
}
