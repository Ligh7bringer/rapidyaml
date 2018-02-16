#ifndef _C4_TPL_TOKEN_CONTAINER_HPP_
#define _C4_TPL_TOKEN_CONTAINER_HPP_

#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"

namespace c4 {
namespace tpl {

class TokenBase;

struct TplLocation
{
    Rope *         m_rope;
    Rope::rope_pos m_rope_pos;
    //! @todo:
    //size_t         m_offset;
    //size_t         m_line;
    //size_t         m_column;
};

void register_known_tokens();

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

/** allow user-registered tokens */
#define C4_DECLARE_TOKEN(cls)                           \
    static TokenType _get_type()                        \
    {                                                   \
        return {_get_name(),                            \
                sizeof(cls),                            \
                &_create_inplace,                       \
                &_destroy_inplace,                      \
                &_get_ptr_inplace};                     \
    }                                                   \
    static const char* _get_name()                      \
    {                                                   \
        return #cls;                                    \
    }                                                   \
    static TokenBase* _create_inplace(span mem)         \
    {                                                   \
        C4_ASSERT(sizeof(cls) <= mem.len);              \
        cls *ptr = new ((void*)mem.str) cls();          \
        return ptr;                                     \
    }                                                   \
    static void _destroy_inplace(span mem)              \
    {                                                   \
        C4_ASSERT(sizeof(cls) <= mem.len);              \
        cls *ptr = reinterpret_cast< cls* >(mem.str);   \
        ptr->~cls();                                    \
    }                                                   \
    static TokenBase* _get_ptr_inplace(span mem)        \
    {                                                   \
        C4_ASSERT(sizeof(cls) <= mem.len);              \
        cls *ptr = reinterpret_cast< cls* >(mem.str);   \
        return ptr;                                     \
    }

#define C4_REGISTER_TOKEN(cls) TokenRegistry::register_token< cls >()


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

struct TokenType
{
    using inplace_create  = TokenBase* (*)(span mem);
    using inplace_destroy = void       (*)(span mem);
    using inplace_get_ptr = TokenBase* (*)(span mem);

    const char*     name;
    size_t          size;
    inplace_create  create;
    inplace_destroy destroy;
    inplace_get_ptr get_ptr;
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

/** registers token types and serves type information  */
class TokenRegistry
{
public:


    template< class T >
    static size_t register_token()
    {
        auto t = T::_get_type();
        register_size(t.size);
        get_types().push_back(t);
        T obj;
        get_stokens().push_back(obj.stoken());
        return get_types().size() - 1;
    }

    static std::vector< TokenType >& get_types()
    {
        static std::vector< TokenType > v;
        return v;
    }
    static std::vector< cspan >& get_stokens()
    {
        static std::vector< cspan > v;
        return v;
    }

    static cspan::first_of_any_result match_any(cspan const& rem)
    {
        auto const& t = get_stokens();
        return rem.first_of_any(t.begin(), t.end());
    }

    static TokenType const& get_type(size_t i)
    {
        auto const& t = get_types();
        C4_ASSERT(i >= 0 && i < t.size());
        return t[i];
    }

public:

    static size_t get_max_size() { return _max_size(); }
    static void register_size(size_t sz) { _max_size() = sz > _max_size() ? sz : _max_size(); }

private:

    static size_t& _max_size() { static size_t sz = 0; return *&sz; }

};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class TokenContainer
{
public:

    std::vector< char > m_tokens;
    size_t m_num_tokens;
    size_t m_max_size;
    size_t m_entry_size;

public:

    TokenContainer(size_t cap=0) : m_tokens(), m_num_tokens(0), m_max_size(0), m_entry_size(0)
    {
        m_max_size = TokenRegistry::get_max_size();
        if( ! m_max_size)
        {
            register_known_tokens();
        }
        m_max_size = TokenRegistry::get_max_size();
        m_entry_size = sizeof(size_t) + m_max_size;
        m_tokens.reserve(cap * m_entry_size);
    }

    ~TokenContainer();

    span get_token_span(size_t i)
    {
        C4_ASSERT(i >= 0 && i < m_num_tokens);
        return {&m_tokens[i * m_entry_size], m_entry_size};
    }

    cspan get_token_span(size_t i) const
    {
        C4_ASSERT(i >= 0 && i < m_num_tokens);
        return {&m_tokens[i * m_entry_size], m_entry_size};
    }

    TokenType const& get_type(size_t i) const
    {
        cspan mem = get_token_span(i);
        size_t type_pos;
        memcpy(&type_pos, mem.str, sizeof(size_t));
        auto const& type = TokenRegistry::get_type(type_pos);
        return type;
    }

    TokenBase *get(size_t i)
    {
        span mem = get_token_span(i);
        size_t type_pos;
        memcpy(&type_pos, mem.str, sizeof(size_t));
        auto const& type = TokenRegistry::get_type(type_pos);
        mem = mem.subspan(sizeof(size_t));
        TokenBase *obj = type.get_ptr(mem);
        return obj;
    }

    TokenBase *next_token(cspan *rem, TplLocation *loc)
    {
        C4_ASSERT(TokenRegistry::get_max_size() == m_max_size);
        auto result = TokenRegistry::match_any(*rem);
        if( ! result) return nullptr;
        size_t pos = m_num_tokens++;
        m_tokens.resize(m_tokens.size() + m_entry_size);
        span entry = get_token_span(pos);
        memcpy(entry.str, &result.which, sizeof(size_t));
        span mem = entry.subspan(sizeof(size_t));
        auto const& type = TokenRegistry::get_type(result.which);
        TokenBase *obj = type.create(mem);
        *rem = rem->subspan(result.pos);
        loc->m_rope_pos.i = result.pos;
        return obj;
    }

public:

    struct iterator
    {
        TokenContainer *this_;
        size_t i;

        using value_type = TokenBase*;

        iterator(TokenContainer* t, size_t i_) : this_(t), i(i_) {}

        iterator operator++ () { ++i; return *this; }
        iterator operator-- () { ++i; return *this; }

        TokenBase* operator-> () { return  this_->get(i); }
        TokenBase& operator*  () { return *this_->get(i); }

        bool operator!= (iterator that) const { C4_ASSERT(this_ == that.this_); return i != that.i; }
        bool operator== (iterator that) const { C4_ASSERT(this_ == that.this_); return i == that.i; }
    };

    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, m_num_tokens); }

public:

};

} // namespace tpl
} // namespace c4

#pragma GCC diagnostic pop

#endif /* _C4_TPL_TOKEN_CONTAINER_HPP_ */
