#pragma once

#include    <vector>
#include    <cassert>
#include    <diy/types.hpp>
#include    <diy/point.hpp>
#include    <diy/log.hpp>
#include    <diy/grid.hpp>
#include    <diy/reduce.hpp>
#include    <diy/partners/broadcast.hpp>

template<unsigned D>
using SimplePoint           = diy::Point<float, D>;
template<unsigned D>
using GridPoint             = diy::Point<unsigned long long, D>;
template<unsigned D>
using Grid                  = diy::GridRef<unsigned long long, D>;
using communicator          = diy::mpi::communicator;
using Bounds                = diy::DiscreteBounds;
using Link                  = diy::RegularGridLink;
using Coordinate            = Bounds::Coordinate;
using Decomposer            = diy::RegularDecomposer<Bounds>;
using DivisionsVector       = Decomposer::DivisionsVector;

enum {producer_task, producer1_task, producer2_task, consumer_task, consumer1_task, consumer2_task};

// checks if a point is in bounds in all dims
template<unsigned D>
bool in(const GridPoint<D>&     pt,
        const Bounds&           bounds)
{
    for (auto i = 0; i < D; i++)
    {
        if (pt[i] < bounds.min[i] || pt[i] > bounds.max[i])
            return false;
    }
    return true;
}

// checks if a point is in bounds in all dims
template<unsigned D>
bool in(const SimplePoint<D>&   pt,
        const Bounds&           bounds)
{
    for (auto i = 0; i < D; i++)
    {
        if (pt[i] < bounds.min[i] || pt[i] > bounds.max[i])
            return false;
    }
    return true;
}

// TODO: move this into RegularDecomposer itself (this is a slightly modified point_to_gids)
std::vector<int>    bounds_to_gids(const Bounds& bounds, const Decomposer& decomposer)
{
    int dim = decomposer.dim;
    std::vector< std::pair<Coordinate, Coordinate> > ranges(dim);
    for (int i = 0; i < dim; ++i)
    {
        Coordinate& bottom = ranges[i].first;
        Coordinate& top    = ranges[i].second;

        Coordinate l = bounds.min[i];
        Coordinate r = bounds.max[i];

        // taken from top_bottom
        top     = diy::detail::BoundsHelper<Bounds>::upper(r, decomposer.divisions[i], decomposer.domain.min[i], decomposer.domain.max[i], false /* share_face[axis] */);
        bottom  = diy::detail::BoundsHelper<Bounds>::lower(l, decomposer.divisions[i], decomposer.domain.min[i], decomposer.domain.max[i], false /* share_face[axis] */);

        if (true /* !wrap[i] */)
        {
            bottom  = std::max(static_cast<Coordinate>(0), bottom);
            top     = std::min(static_cast<Coordinate>(decomposer.divisions[i]), top);
        }
    }

    // look up gids for all combinations
    std::vector<int> gids;
    DivisionsVector coords(dim), location(dim);
    while(location.back() < ranges.back().second - ranges.back().first)
    {
        for (int i = 0; i < dim; ++i)
            coords[i] = ranges[i].first + location[i];
        gids.push_back(decomposer.coords_to_gid(coords, decomposer.divisions));

        location[0]++;
        unsigned i = 0;
        while (i < dim-1 && location[i] == ranges[i].second - ranges[i].first)
        {
            location[i] = 0;
            ++i;
            location[i]++;
        }
    }

    return gids;
}

// block structure
template<unsigned DIM>
struct PointBlock
{
    typedef SimplePoint<DIM>                            Point;

    PointBlock(const Bounds& core_, const Bounds& bounds_, const Bounds& domain_):
        core(core_),
        bounds(bounds_),
        domain(domain_)                 {}

    // allocate a new block
    static void* create()               { return new PointBlock; }

    // free a block
    static void destroy(void* b)        { delete static_cast<PointBlock*>(b); }

    // serialize the block and write it
    static void save(const void* b_, diy::BinaryBuffer& bb)
    {
        const PointBlock* b = static_cast<const PointBlock*>(b_);
        diy::save(bb, b->bounds);
        diy::save(bb, b->domain);
        diy::save(bb, b->points);
        diy::save(bb, b->grid);
        diy::save(bb, b->box);
    }

    // read the block and deserialize it
    static void load(void* b_, diy::BinaryBuffer& bb)
    {
        PointBlock* b = static_cast<PointBlock*>(b_);
        diy::load(bb, b->bounds);
        diy::load(bb, b->domain);
        diy::load(bb, b->points);
        diy::load(bb, b->grid);
        diy::load(bb, b->box);
    }

    // initialize a regular grid of points in a block
    void generate_grid()                            // local block bounds
    {
        GridPoint<DIM> shape, vertex;

        // virtual grid covering local block bounds (for indexing only, no associated data)
        for (auto i = 0; i < DIM; i++)
            shape[i] = bounds.max[i] - bounds.min[i] + 1;
        Grid<DIM> bounds_grid(NULL, shape);

        // virtual grid covering global domain (for indexing only, no associated data)
        for (auto i = 0; i < DIM; i++)
            shape[i] = domain.max[i] - domain.min[i] + 1;
        Grid<DIM> domain_grid(NULL, shape);

        // assign globally unique values to the grid scalars in the block
        // equal to global linear idx of the grid point
        grid.resize(bounds_grid.size());
        for (auto i = 0; i < bounds_grid.size(); i++)
        {
            vertex = bounds_grid.vertex(i);         // vertex in the local block
            for (auto j = 0; j < DIM; j++)
                vertex[j] += bounds.min[j];         // converted to global domain vertex
            grid[i] = domain_grid.index(vertex);
        }

        // allocate second grid for received data to check against original
        recv_grid.resize(grid.size());
    }

    // initialize a set of points in a block with values in a known sequence
    void generate_points(int      gid,
                         size_t   n)             // number of points
    {
        points.resize(n);
        for (auto i = 0; i < n; ++i)
        {
            for (unsigned j = 0; j < DIM; ++j)
                points[i][j] = gid * n + i;
        }
    }

    // send the block grid data
    void send_block_grid(
            const diy::Master::ProxyWithLink& cp,
            const communicator& local,
            const communicator& intercomm,
            const Decomposer& con_decomposer)
    {
        // local grid for location of grid point
        GridPoint<DIM> shape, vertex, dom_vertex;
        for (auto i = 0; i < DIM; i++)
            shape[i] = domain.max[i] - domain.min[i] + 1;
        Grid<DIM> domain_grid(NULL, shape);
        for (auto i = 0; i < DIM; i++)
            shape[i] = bounds.max[i] - bounds.min[i] + 1;
        Grid<DIM> bounds_grid(NULL, shape);

        // get the gids of the consumer decomposition that intersect the local block bounds
        std::vector<int> int_gids = bounds_to_gids(bounds, con_decomposer);

        // memory buffers for messages
        grid_bbs.resize(int_gids.size());

        // requests for nonblocking sends
        grid_requests.resize(int_gids.size());      // maximum possible number of requests
        size_t nreqs = 0;                           // actual number of requests

        // send to each of the intersecting gids
        // NB: data redistribution is only for one block per process
        // uses low-level MPI send, with block gid as the rank
        for (auto i = 0; i < int_gids.size(); i++)
        {
            // bounds of consumer block
            Bounds con_bounds {DIM};
            con_decomposer.fill_bounds(con_bounds, int_gids[i]);

            // bounds of intersection
            Bounds int_bounds {DIM};
            for (auto j = 0; j < DIM; j++)
            {
                int_bounds.min[j] = std::max(bounds.min[j], con_bounds.min[j]);
                int_bounds.max[j] = std::min(bounds.max[j], con_bounds.max[j]);
                shape[j]            = int_bounds.max[j] - int_bounds.min[j] + 1;
            }
            Grid<DIM> int_grid(NULL, shape);

            // iterate over points in intersection and serialize them
            size_t npts = int_grid.size();
            for (auto j = 0; j < npts; j++)
            {
                vertex = int_grid.vertex(j);                            // vertex in intersected grid
                for (auto k = 0; k < DIM; k++)
                {
                    dom_vertex[k]   = vertex[k] + int_bounds.min[k];    // vertex in global domain grid
                    vertex[k]       = dom_vertex[k] - bounds.min[k];    // vertex in local block grid
                }
                size_t idx = bounds_grid.index(vertex);
                diy::save(grid_bbs[i], grid[idx]);
            }   // points in intersection

            if (npts)
                grid_requests[nreqs++] = intercomm.isend(int_gids[i], 0, grid_bbs[i].buffer);
        }   // intersecting gids in consumer

        grid_requests.resize(nreqs);                    // resize to the actual number of requests posted
    }

    // send the block particle data
    void send_block_points(
            const diy::Master::ProxyWithLink&   cp,
            size_t                              global_num_pts,
            size_t                              prod_num_blks,              // number of blocks in producer
            const                               communicator& local,
            const communicator&                 intercomm,
            const Decomposer&                   con_decomposer)
    {
        size_t prod_npts    = global_num_pts / prod_num_blks;               // nominal local number of points in a producer block, excluding remainder
        size_t con_npts     = global_num_pts / con_decomposer.nblocks;      // nominal local number of points in a consumer block, excluding remainder
        size_t min_prod_val = cp.gid() * prod_npts;
        size_t max_prod_val = min_prod_val + points.size() - 1;

        // memory buffers for messages
        point_bbs.resize(con_decomposer.nblocks);

        // requests for nonblocking sends
        point_requests.resize(con_decomposer.nblocks);          // maximum possible number of requests
        size_t nreqs = 0;                                       // actual number of requests posted

        // send to each of the intersecting gids
        // NB: data redistribution is only for one block per process
        // uses low-level MPI send, with block gid as the rank
        for (auto i = 0; i < con_decomposer.nblocks; i++)
        {
            // check if gids of producer and consumer intersect
            size_t min_con_val = i * con_npts;
            size_t max_con_val = (i + 1) * con_npts - 1;
            if (i == con_decomposer.nblocks - 1)
                max_con_val = global_num_pts - 1;
            if (max_con_val < min_prod_val || min_con_val > max_prod_val)
                continue;

            //  bounds of intersection
            size_t min_int_val = std::max(min_prod_val, min_con_val);
            size_t max_int_val = std::min(max_prod_val, max_con_val);
            size_t npts = max_int_val - min_int_val + 1;

            // iterate over points in intersection and serialize them
            for (auto j = 0; j < npts; j++)
            {
                size_t idx = min_int_val + j - min_prod_val;        // index in local points
                diy::save(point_bbs[i], points[idx]);
            }   // points in intersection

            if (npts)
                point_requests[nreqs++] = intercomm.isend(i, 0, point_bbs[i].buffer);
        }   // consumer blocks

        point_requests.resize(nreqs);            // resize to actual number of requests posted
    }

    // receive the block grid data
    void recv_block_grid(
            const diy::Master::ProxyWithLink& cp,
            const communicator& local,
            const communicator& intercomm,
            const Decomposer& prod_decomposer)
    {
        // local and global grids for location of grid point
        GridPoint<DIM> shape, vertex;
        for (auto i = 0; i < DIM; i++)
            shape[i] = bounds.max[i] - bounds.min[i] + 1;
        Grid<DIM> bounds_grid(NULL, shape);
        for (auto i = 0; i < DIM; i++)
            shape[i] = domain.max[i] - domain.min[i] + 1;
        Grid<DIM> domain_grid(NULL, shape);

        // get the gids of the producer decomposition that intersect the local block bounds
        std::vector<int> int_gids = bounds_to_gids(bounds, prod_decomposer);

        // receive from each of the intersecting gids
        // NB: data redistribution is only for one block per process
        // uses low-level MPI send, with block gid as the rank
        size_t tot_npts = 0;
        for (auto i = 0; i < int_gids.size(); i++)
        {
            // bounds of producer block
            Bounds prod_bounds {DIM};
            prod_decomposer.fill_bounds(prod_bounds, int_gids[i]);

            // bounds of intersection
            Bounds int_bounds {DIM};
            for (auto j = 0; j < DIM; j++)
            {
                int_bounds.min[j] = std::max(bounds.min[j], prod_bounds.min[j]);
                int_bounds.max[j] = std::min(bounds.max[j], prod_bounds.max[j]);
                shape[j]            = int_bounds.max[j] - int_bounds.min[j] + 1;
            }
            Grid<DIM> int_grid(NULL, shape);

            diy::MemoryBuffer bb;
            size_t npts = int_grid.size();
            if (npts)
            {
                intercomm.recv(int_gids[i], 0, bb.buffer);
                for (auto j = 0; j < npts; j++)
                {
                    unsigned long long value;
                    diy::load(bb, value);

                    // determine storage location in recv_grid
                    // matches the way points were generated in generate_grid()
                    vertex = domain_grid.vertex(value);                 // vertex in global grid of received point
                    for (auto k = 0; k < DIM; k++)
                        vertex[k] -= bounds.min[k];                     // converted to local block vertex
                    size_t idx = bounds_grid.index(vertex);             // index in local block
                    recv_grid[idx] = value;
                }
            }
            tot_npts += npts;
        }
        recv_grid.resize(tot_npts);

        // check that the values match
        for (size_t i = 0; i < tot_npts; ++i)
        {
            if (grid[i] != recv_grid[i])
            {
                fmt::print("Error: consumer gid {} grid[{}] = {} but does not match recv_grid[{}] = {}, tot_npts {}\n",
                        cp.gid(), i, grid[i], i, recv_grid[i], tot_npts);
                abort();
            }
        }
    }

    // receive the block particle data
    void recv_block_points(
            const diy::Master::ProxyWithLink&   cp,
            size_t                              global_num_pts,
            size_t                              con_num_blks,               // number of blocks in consumer
            const communicator&                 local,
            const communicator&                 intercomm,
            const Decomposer&                   prod_decomposer)
    {
        size_t prod_npts    = global_num_pts / prod_decomposer.nblocks;     // nominal local number of points in a producer block, excluding remainder
        size_t con_npts     = global_num_pts / con_num_blks;                // nominal local number of points in a consumer block, excluding remainder
        size_t min_con_val  = cp.gid() * con_npts;
        size_t max_con_val  = min_con_val + points.size() - 1;

        Point pt;
        std::vector<Point>  recv_points;                                    // points being read back

        // receive from each of the intersecting gids
        // NB: data redistribution is only for one block per process
        // uses low-level MPI send, with block gid as the rank
        size_t tot_npts = 0;
        for (auto i = 0; i < prod_decomposer.nblocks; i++)
        {
            // check if gids of producer and consumer intersect
            size_t min_prod_val = i * prod_npts;
            size_t max_prod_val = (i + 1) * prod_npts - 1;
            if (i == prod_decomposer.nblocks - 1)
                max_prod_val = global_num_pts - 1;
            if (max_prod_val < min_con_val || min_prod_val > max_con_val)
                continue;

            //  bounds of intersection
            size_t min_int_val = std::max(min_prod_val, min_con_val);
            size_t max_int_val = std::min(max_prod_val, max_con_val);
            size_t npts = max_int_val - min_int_val + 1;

            diy::MemoryBuffer bb;
            if (npts)
            {
                intercomm.recv(i, 0, bb.buffer);
                for (auto j = 0; j < npts; j++)
                {
                    diy::load(bb, pt);
                    recv_points.push_back(pt);
                }
            }
        }

        // check that the values match
        for (auto i = 0; i < tot_npts; ++i)
        {
            if (recv_points[i][0] < min_con_val  || recv_points[i][0] > max_con_val)
            {
                fmt::print(stderr, "Error: for gid {}, recv_points[{}] = {} is out of bounds [{} : {}]\n",
                        cp.gid(), i, recv_points[i], min_con_val, max_con_val);
                abort();
            }
        }
    }

    // wait for all requests to finish
    void wait_requests(const diy::Master::ProxyWithLink&   cp)
    {
        for (auto i = 0; i < grid_requests.size(); i++)
            grid_requests[i].wait();
        for (auto i = 0; i < point_requests.size(); i++)
            point_requests[i].wait();
    }

    // block data
    Bounds              bounds      { DIM };        // local block bounds incl. ghost
    Bounds              core        { DIM };        // local block bounds excl. ghost
    Bounds              domain      { DIM };        // global domain bounds
    Bounds              box         { DIM };        // temporary box for whatever purpose
    std::vector<Point>  points;                     // unstructured set of points
    std::vector<unsigned long long>  grid;          // scalars linearized from a structured grid
    std::vector<unsigned long long>  recv_grid;     // data received from others
    std::vector<std::pair<Bounds, int>> query_bounds;   // bounds requested by other blocks
    size_t              global_num_points;          // total number of unstructured points
    std::vector<diy::MemoryBuffer> grid_bbs;        // memory buffers for sending grid data to consumer blocks
    std::vector<diy::MemoryBuffer> point_bbs;       // memory buffers for sending particles data to consumer blocks
    std::vector<diy::mpi::request> grid_requests;   // requests for grid data isends
    std::vector<diy::mpi::request> point_requests;  // requests for particles data isends

private:
    PointBlock()                                  {}
};

template<unsigned DIM>
struct AddPointBlock
{
    typedef   PointBlock<DIM>                                     Block;

    AddPointBlock(diy::Master&  master_,
                  size_t        local_num_points_,
                  size_t        global_num_points_,
                  size_t        global_num_blocks_):
        master(master_),
        local_num_points(local_num_points_),
        global_num_points(global_num_points_),
        global_num_blocks(global_num_blocks_)
        {}

    // this is the function that is needed for diy::decompose
    void  operator()(int            gid,            // block global id
                     const  Bounds& core,           // block bounds without any ghost added
                     const  Bounds& bounds,         // block bounds including any ghost region added
                     const  Bounds& domain,         // global data bounds
                     const  Link&   link)           // neighborhood
        {
            Block*          b   = new Block(core, bounds, domain);
            Link*           l   = new Link(link);
            diy::Master&    m   = const_cast<diy::Master&>(master);

            m.add(gid, b, l);

            // NB, only the producer really needs to generate the grid and the points because the
            // consumer gets them from the workflow, but we are re-using the same AddPointBlock
            // function for both producer and consumer in this example

            // adjust local number of points if global number of points does not divide global number of blocks evenly
            if (local_num_points * global_num_blocks < global_num_points && gid == global_num_blocks - 1)
                local_num_points = global_num_points - local_num_points * (global_num_blocks - 1);

            b->domain               = domain;
            b->global_num_points    = global_num_points;

            b->generate_grid();                             // initialize block with regular grid
            b->generate_points(gid, local_num_points);      // initialize block with set of points
        }

    diy::Master&    master;
    size_t          local_num_points;
    size_t          global_num_points;
    size_t          global_num_blocks;
};

static const unsigned DIM = 3;
typedef     PointBlock<DIM>             Block;
typedef     AddPointBlock<DIM>          AddBlock;

