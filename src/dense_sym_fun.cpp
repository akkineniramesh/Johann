
#include "dense_sym_fun.hpp"
#include "aligned_alloc.hpp"
#include <cstring>

namespace pomagma
{

//ctor & dtor
dense_sym_fun::dense_sym_fun (int num_items)
    : N(num_items),
      M((N+DSF_STRIDE)/DSF_STRIDE),
      m_blocks(pomagma::alloc_blocks<Block4x4W>(unordered_pair_count(M))),
      m_set(N,NULL),
      m_Lx_lines(pomagma::alloc_blocks<Line>((N+1) * num_lines())),
      m_temp_line(pomagma::alloc_blocks<Line>(1 * num_lines()))
{
    POMAGMA_DEBUG("creating dense_sym_fun with " << unordered_pair_count(M) << " blocks");
    POMAGMA_ASSERT(0, N < (1<<15), "dense_sym_fun is too large");
    POMAGMA_ASSERT(0, m_blocks, "failed to allocate blocks");
    POMAGMA_ASSERT(0, m_Lx_lines, "failed to allocate Lx lines");
    POMAGMA_ASSERT(0, m_temp_line, "failed to allocate temp lines");

    //initialize to zero
    bzero(m_blocks, unordered_pair_count(M) * sizeof(Block4x4W));
    bzero(m_Lx_lines, (N+1) * num_lines() * sizeof(Line));
}
dense_sym_fun::~dense_sym_fun ()
{
    pomagma::free_blocks(m_blocks);
    pomagma::free_blocks(m_Lx_lines);
    pomagma::free_blocks(m_temp_line);
}
void dense_sym_fun::move_from (const dense_sym_fun& other)
{//for growing
    POMAGMA_DEBUG("Copying dense_sym_fun");

    //copy data
    unsigned minM = min(M, other.M);
    for (unsigned j_=0; j_<minM; ++j_) {
        int* destin = _block(0,j_);
        const int* source = other._block(0,j_);
        memcpy(destin, source, sizeof(Block4x4W) * (1+j_));
    }

    //copy sets
    unsigned minN = min(N, other.N);
    unsigned minL = min(num_lines(), other.num_lines());
    for (unsigned i=1; i<=minN; ++i) {
        memcpy(get_Lx_line(i), other.get_Lx_line(i), sizeof(Line) * minL);
    }
}

//diagnostics
unsigned dense_sym_fun::size () const
{
    unsigned result = 0;
    for (unsigned i=1; i<=N; ++i) {
        result += _get_Lx_set(i).size();
    }
    return result;
}
void dense_sym_fun::validate () const
{
    POMAGMA_DEBUG("Validating dense_sym_fun");

    POMAGMA_DEBUG("validating line-block consistency");
    for (unsigned i_=0; i_<M; ++i_) {
    for (unsigned j_=i_; j_<M; ++j_) {
        const int* block = _block(i_,j_);

        for (unsigned _i=0; _i<DSF_STRIDE; ++_i) {
        for (unsigned _j=0; _j<DSF_STRIDE; ++_j) {
            unsigned i = i_*DSF_STRIDE+_i; if (i==0 or N<i) continue;
            unsigned j = j_*DSF_STRIDE+_j; if (j<i  or N<j) continue;
            int val = _block2value(block,_i,_j);

            if (val) {
                POMAGMA_ASSERT(0, contains(i,j),
                        "invalid: found unsupported value: "<<i<<','<<j);
            } else {
                POMAGMA_ASSERT(0, not contains(i,j),
                        "invalid: found supported null value: "<<i<<','<<j);
            }
        }}
    }}
}

//dense_sym_fun operations
void dense_sym_fun::remove(const int i,
                           void remove_value(int)) //rem
{
    POMAGMA_ASSERT(4, 0<i and i<=int(N), "item out of bounds: " << i);

    for (Iterator iter(this,i); not iter.done(); iter.next()) {
        int k = iter.moving();
        int& dep = value(k,i);
        remove_value(dep);
        _get_Lx_set(k).remove(i);
        dep = 0;
    }
    _get_Lx_set(i).zero();
}
void dense_sym_fun::merge(const int i, //dep
                          const int j, //rep
                          void merge_values(int,int),   //dep,rep
                          void move_value(int,int,int)) //moved,lhs,rhs
{
    POMAGMA_ASSERT(4, j!=i, "in dense_sym_fun::merge, tried to merge with self");
    POMAGMA_ASSERT(4, 0<i and i<=int(N), "dep out of bounds: " << i);
    POMAGMA_ASSERT(4, 0<j and j<=int(N), "rep out of bounds: " << j);

    //(i,i) -> (i,j)
    if (contains(i,i)) {
        int& dep = value(i,i);
        int& rep = value(j,j);
        _get_Lx_set(i).remove(i);
        if (rep) {
            merge_values(dep,rep);
        } else {
            move_value(dep, j, j);
            _get_Lx_set(j).insert(j);
            rep = dep;
        }
        dep = 0;
    }

    //(k,i) --> (j,j) for k!=i
    for (Iterator iter(this,i); iter; iter.next()) {
        int k = iter.moving();
        int& dep = value(k,i);
        int& rep = value(k,j);
        _get_Lx_set(k).remove(i); //sets m_set
        if (rep) {
            merge_values(dep,rep);
        } else {
            move_value(dep, k, j);
            m_set.insert(j); //ie, _get_Lx_set(k).insert(j), as above
            rep = dep;
        }
        dep = 0;
    }
    dense_set Lx_rep = _get_Lx_set(j);
    dense_set Lx_dep = _get_Lx_set(i);
    Lx_rep.merge(Lx_dep);
}

//intersection iteration
Line* dense_sym_fun::_get_LLx_line (int i, int j) const
{
    Line* i_line = get_Lx_line(i);
    Line* j_line = get_Lx_line(j);
    for (oid_t k_=0; k_<num_lines(); ++k_) {
        m_temp_line[k_] = i_line[k_] & j_line[k_];
    }
    return m_temp_line;
}

}


