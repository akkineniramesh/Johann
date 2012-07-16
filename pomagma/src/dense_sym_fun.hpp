#ifndef POMAGMA_DENSE_SYM_FUN_H
#define POMAGMA_DENSE_SYM_FUN_H

#include "util.hpp"
#include "dense_set.hpp"

namespace pomagma
{

// WARNING zero/null items are not allowed

inline size_t unordered_pair_count (size_t i) { return (i * (i + 1)) / 2; }

// a tight binary function in 4x4 word blocks
class dense_sym_fun
{
    // data, in blocks
    const size_t m_item_dim;
    const size_t m_block_dim;
    const size_t m_word_dim;
    Block4x4 * const m_blocks;

    // dense sets for iteration
    Word * const m_Lx_lines;
    mutable Word * m_temp_line; // TODO FIXME this is not thread-safe

    // block wrappers
    template<class T>
    static void sort (T & i, T & j) { if (j < i) { T k = j; j = i; i = k; }  }
    oid_t * _block (int i_, int j_)
    {
        return m_blocks[unordered_pair_count(j_) + i_];
    }
    const oid_t * _block (int i_, int j_) const
    {
        return m_blocks[unordered_pair_count(j_) + i_];
    }

    // set wrappers
public:
    Word * get_Lx_line (oid_t lhs) const
    {
        POMAGMA_ASSERT_RANGE_(5, lhs, m_item_dim);
        return m_Lx_lines + (lhs * m_word_dim);
    }
private:
    bool _get_Lx_bit (oid_t lhs, oid_t rhs) const
    {
        POMAGMA_ASSERT_RANGE_(5, rhs, m_item_dim);
        return bool_ref::index(get_Lx_line(lhs), rhs);
    }
    bool_ref _get_Lx_bit (oid_t lhs, oid_t rhs)
    {
        POMAGMA_ASSERT_RANGE_(5, rhs, m_item_dim);
        return bool_ref::index(get_Lx_line(lhs), rhs);
    }

    // intersection wrappers
    // TODO force callee to allocate the result
    Word * _get_LLx_line (oid_t i, oid_t j) const;

    // ctors & dtors
public:
    dense_sym_fun (size_t item_dim);
    ~dense_sym_fun ();
    void move_from (const dense_sym_fun & other); // for growing

    // function calling
private:
    inline oid_t & value (oid_t lhs, oid_t rhs);
public:
    inline oid_t value (oid_t lhs, oid_t rhs) const;
    oid_t get_value (oid_t lhs, oid_t rhs) const { return value(lhs, rhs); }

    // attributes
    size_t item_dim () const { return m_item_dim; }
    unsigned count_pairs () const; // slow!
    void validate () const;

    // element operations
    void insert (oid_t lhs, oid_t rhs, oid_t val);
    void remove (oid_t lhs, oid_t rhs);
    bool contains (oid_t lhs, oid_t rhs) const
    {
        return _get_Lx_bit(lhs, rhs);
    }

    // support operations
    void remove (
            const oid_t i,
            void remove_value(oid_t)); // rem
    void merge (
            const oid_t i,
            const oid_t j,
            void merge_values(oid_t, oid_t),     // dep, rep
            void move_value(oid_t, oid_t, oid_t)); // moved, lhs, rhs

    // iteration
    class Iterator;
    class LLxx_Iter;
};

inline oid_t & dense_sym_fun::value (oid_t i, oid_t j)
{
    sort(i, j);

    POMAGMA_ASSERT_RANGE_(5, i, m_item_dim);
    POMAGMA_ASSERT_RANGE_(5, j, m_item_dim);

    oid_t * block = _block(i / ITEMS_PER_BLOCK, j / ITEMS_PER_BLOCK);
    return _block2value(block, i & BLOCK_POS_MASK, j & BLOCK_POS_MASK);
}

inline oid_t dense_sym_fun::value (oid_t i, oid_t j) const
{
    sort(i, j);

    POMAGMA_ASSERT_RANGE_(5, i, m_item_dim);
    POMAGMA_ASSERT_RANGE_(5, j, m_item_dim);

    const oid_t * block = _block(i / ITEMS_PER_BLOCK, j / ITEMS_PER_BLOCK);
    return _block2value(block, i & BLOCK_POS_MASK, j & BLOCK_POS_MASK);
}

inline void dense_sym_fun::insert (oid_t lhs, oid_t rhs, oid_t val)
{
    oid_t & old_val = value(lhs, rhs);
    POMAGMA_ASSERT2(old_val, "double insertion: " << lhs << "," << rhs);
    old_val = val;

    bool_ref Lx_bit = _get_Lx_bit(lhs, rhs);
    POMAGMA_ASSERT4(not Lx_bit, "double insertion: " << lhs << "," << rhs);
    Lx_bit.one();

    if (lhs == rhs) return;

    bool_ref Rx_bit = _get_Lx_bit(rhs, lhs);
    POMAGMA_ASSERT4(not Rx_bit, "double insertion: " << lhs << "," << rhs);
    Rx_bit.one();
}

inline void dense_sym_fun::remove (oid_t lhs, oid_t rhs)
{
    oid_t & old_val = value(lhs, rhs);
    POMAGMA_ASSERT2(old_val, "double removal: " << lhs << "," << rhs);
    old_val = 0;

    bool_ref Lx_bit = _get_Lx_bit(lhs, rhs);
    POMAGMA_ASSERT4(Lx_bit, "double removal: " << lhs << "," << rhs);
    Lx_bit.zero();

    if (lhs == rhs) return;

    bool_ref Rx_bit = _get_Lx_bit(rhs, lhs);
    POMAGMA_ASSERT4(Rx_bit, "double removal: " << lhs << "," << rhs);
    Rx_bit.zero();
}

//----------------------------------------------------------------------------
// Iteration over a line

class dense_sym_fun::Iterator : noncopyable
{
    dense_set m_set;
    dense_set::iterator m_iter;
    const dense_sym_fun * m_fun;
    oid_t m_fixed;
    oid_t m_moving;

public:

    // construction
    Iterator (const dense_sym_fun * fun)
        : m_set(fun->m_item_dim, NULL),
          m_iter(m_set, false),
          m_fun(fun),
          m_fixed(0),
          m_moving(0)
    {}
    Iterator (const dense_sym_fun * fun, oid_t fixed)
        : m_set(fun->m_item_dim, fun->get_Lx_line(fixed)),
          m_iter(m_set, false),
          m_fun(fun),
          m_fixed(fixed),
          m_moving(0)
    {
        begin();
    }

    // traversal
private:
    void _set_pos () { m_moving = *m_iter; }
public:
    bool ok () const { return m_iter.ok(); }
    void begin () { m_iter.begin(); if (ok()) _set_pos(); }
    void begin (oid_t fixed)
    {
        m_fixed=fixed;
        m_set.init(m_fun->get_Lx_line(fixed));
        begin();
    }
    void next () { m_iter.next(); if (ok()) _set_pos(); }

    // dereferencing
private:
    void _deref_assert () const
    {
        POMAGMA_ASSERT5(ok(), "dereferenced done dense_set::iter");
    }
public:
    oid_t fixed () const { _deref_assert(); return m_fixed; }
    oid_t moving () const { _deref_assert(); return m_moving; }
    oid_t value () const
    {
        _deref_assert();
        return m_fun->get_value(m_fixed,m_moving);
    }
};

//------------------------------------------------------------------------
// Intersection iteration over 2 lines

class dense_sym_fun::LLxx_Iter : noncopyable
{
    dense_set m_set;
    dense_set::iterator m_iter;
    const dense_sym_fun * m_fun;
    oid_t m_fixed1;
    oid_t m_fixed2;
    oid_t m_moving;

public:

    // construction
    LLxx_Iter (const dense_sym_fun* fun)
        : m_set(fun->m_item_dim, NULL),
          m_iter(m_set, false),
          m_fun(fun)
    {}

    // traversal
    void begin () { m_iter.begin(); if (ok()) m_moving = *m_iter; }
    void begin (oid_t fixed1, oid_t fixed2)
    {
        m_set.init(m_fun->_get_LLx_line(fixed1, fixed2));
        m_iter.begin();
        if (ok()) {
            m_fixed1 = fixed1;
            m_fixed2 = fixed2;
            m_moving = *m_iter;
        }
    }
    bool ok () const { return m_iter.ok(); }
    void next () { m_iter.next(); if (ok()) m_moving = *m_iter; }

    // dereferencing
    oid_t fixed1 () const { return m_fixed1; }
    oid_t fixed2 () const { return m_fixed2; }
    oid_t moving () const { return m_moving; }
    oid_t value1 () const { return m_fun->get_value(m_fixed1, m_moving); }
    oid_t value2 () const { return m_fun->get_value(m_fixed2, m_moving); }
};

} // namespace pomagma

#endif // POMAGMA_DENSE_SYM_FUN_H
