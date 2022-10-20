#include <diy/mpi/communicator.hpp>
#include <thread>
#include "prod-con.hpp"

using communicator = diy::mpi::communicator;

extern "C"
{
void consumer_f (communicator& local, const std::vector<communicator>& intercomms,
                 std::mutex& exclusive, bool shared,
                 std::string prefix,
                 int threads, int mem_blocks,
                 Bounds domain,
                 size_t global_num_points,
                 int dim,
                 int con_nblocks,
                 int prod1_nblocks,
                 int prod2_blocks);
}

void consumer_f (communicator& local, const std::vector<communicator>& intercomms,
                 std::mutex& exclusive, bool shared,
                 std::string prefix,
                 int threads, int mem_blocks,
                 Bounds domain,
                 size_t global_num_points,
                 int dim,
                 int con_nblocks,
                 int prod1_nblocks,
                 int prod2_nblocks)
{
    // wait for data to be ready
    if (!shared)
    {
        for (auto& intercomm: intercomms)
            intercomm.barrier();
    }
    else
    {
        int a;                                  // it doesn't matter what we receive, for synchronization only
        for (auto& intercomm : intercomms)
            intercomm.recv(local.rank(), 0, a);
    }

    // diy setup for the consumer
    diy::FileStorage                con_storage(prefix);
    diy::Master                     con_master(local,
            threads,
            mem_blocks,
            &Block::create,
            &Block::destroy,
            &con_storage,
            &Block::save,
            &Block::load);
    size_t local_num_points = global_num_points / con_nblocks;
    AddBlock                        con_create(con_master, local_num_points, global_num_points, con_nblocks);
    diy::ContiguousAssigner         con_assigner(local.size(), con_nblocks);
    diy::RegularDecomposer<Bounds>  con_decomposer(dim, domain, con_nblocks);
    con_decomposer.decompose(local.rank(), con_assigner, con_create);
    diy::RegularDecomposer<Bounds>  prod1_decomposer(dim, domain, prod1_nblocks);

    // receive the grid data
    con_master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
            { b->recv_block_grid(cp, local, intercomms.front(), prod1_decomposer); });

    // receive the particle data
    if (prod2_nblocks)
    {
        diy::RegularDecomposer<Bounds>  prod2_decomposer(dim, domain, prod2_nblocks);
        con_master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { b->recv_block_points(cp, global_num_points, con_nblocks, local, intercomms.back(), prod2_decomposer); });
    }
    else
        con_master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { b->recv_block_points(cp, global_num_points, con_nblocks, local, intercomms.back(), prod1_decomposer); });
}

