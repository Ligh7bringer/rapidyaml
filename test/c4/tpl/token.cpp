#include "./token.hpp"

namespace c4 {
namespace tpl {

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void TokenBase::parse(cspan *rem, TplLocation *curr_pos)
{
    auto const &s = stoken(), &e = etoken();
    C4_ASSERT(rem->begins_with(s));

    m_start = *curr_pos;
    auto pos = rem->find(e);
    C4_ASSERT(pos != npos);
    C4_ASSERT(pos + e.len <= rem->len);
    m_full_text = rem->subspan(0, pos + e.len);
    C4_ASSERT(m_full_text.len >= e.len + s.len);
    C4_ASSERT(m_full_text.begins_with(s));
    C4_ASSERT(m_full_text.ends_with(e));
    m_interior_text = m_full_text.subspan(s.len, m_full_text.len - (e.len + s.len));

    auto &rp = curr_pos->m_rope_pos;
    C4_ASSERT(curr_pos->m_rope->get(rp.entry)->s.len >= m_full_text.len);
    rp.entry = curr_pos->m_rope->replace(rp.entry, rp.i, m_full_text.len, m_full_text);
    m_rope_entry = rp.entry;
    rp.entry = curr_pos->m_rope->next(rp.entry);
    rp.i = 0;
    m_end = *curr_pos;
    C4_ASSERT(this->subspan() == m_full_text);

    *rem = rem->subspan(m_full_text.len);
}

void TokenBase::mark()
{
    m_start.m_rope->replace(m_start.m_rope_pos.entry, marker());
}

void TokenBase::_do_parse_body(cspan body, TplLocation pos, TokenContainer *cont) const
{
    //size_t id = cont->get_id(this); // prepare for an eventual relocation of 'this' while adding tokens. Fishy, I know.
    C4_ASSERT(pos.m_rope != nullptr);
    while( ! body.empty())
    {
        C4_ASSERT(body == pos.m_rope->subspan(pos.m_rope_pos).subspan(0, body.len));
        size_t tk_pos = cont->next_token(&body, &pos);
        if(tk_pos == NONE) break;
        cont->get(tk_pos)->parse(&body, &pos);
        cont->get(tk_pos)->parse_body(cont);
    }
}

bool TokenBase::eval(NodeRef const& root, cspan key, cspan *value) const
{
    C4_ASSERT(root.valid());
    NodeRef n = root;
    do {
        auto pos = key.find('.');
        if(pos != npos)
        {
            cspan left = key.left_of(pos);
            n = n.find_child(left);
            key = key.right_of(pos);
        }
        else
        {
            pos = key.find('[');
            if(pos != npos)
            {
                cspan left = key.left_of(pos);
                if( ! left.empty())
                {
                    n = n.find_child(left);
                    if( ! n.valid())
                    {
                        break;
                    }
                    key = key.right_of(pos);
                    pos = key.find(']');
                    C4_ASSERT(pos != npos);
                    cspan subkey = key.left_of(pos);
                    key = key.right_of(pos);
                    if( ! this->eval(n, subkey, &subkey))
                    {
                        break;
                    }
                    else
                    {
                        *value = subkey;
                        return true;
                    }
                }
            }
            else
            {
                if(key.begins_with_any("0123456789"))
                {
                    size_t num;
                    bool ret = from_str(key, &num);
                    if(ret)
                    {
                        if(n.num_children() >= num)
                        {
                            n = n[num];
                            key.clear();
                        }
                    }
                }
                else
                {
                    n = n.find_child(key);
                    key.clear();
                }
            }
        }
    } while( ! key.empty() && n.valid());

    if(n.valid())
    {
        if(n.is_map()) *value = "<<<map>>>";
        else if(n.is_seq()) *value = "<<<seq>>>";
        else *value = n.val();
        return true;
    }

    return false;
}

cspan TokenBase::skip_nested(cspan rem) const
{
    auto const& s = stoken(), e = etoken();
    C4_ASSERT(rem.begins_with(s));
    cspan r = rem.subspan(s.len);
    size_t level = 1;
    while( ! r.empty())
    {
        auto result = r.first_of_any(s, e);
        C4_ERROR_IF_NOT(result, "invalid nested sequence");
        C4_ERROR_IF_NOT(result.which == 0 || result.which == 1, "internal error");
        if(result.which == 0)
        {
            ++level;
        }
        else
        {
            C4_ERROR_IF_NOT(level > 0, "internal error");
            --level;
        }
        r = r.subspan(result.pos + (result.which = 0 ? s.len : e.len));
        if(level == 0)
        {
            return r;
        }
    }
    C4_ERROR("never reach this");
    return {};
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

void IfCondition::eval(NodeRef const& root)
{
    m_argval = {};
    if( ! m_arg.empty())
    {
        m_token->eval(root, m_arg, &m_argval);
    }
    m_cmpval = {};
    if( ! m_cmp.empty())
    {
        m_token->eval(root, m_cmp, &m_cmpval);
    }
}

bool IfCondition::resolve(NodeRef const& root)
{
    switch(m_ctype)
    {
    case ARG:        eval(root); return ! m_argval.empty();
    case ARG_EQ_CMP: eval(root); return   m_argval.compare(m_cmpval) == 0;
    case ARG_NE_CMP: eval(root); return   m_argval.compare(m_cmpval) != 0;
    case ARG_GE_CMP: eval(root); return   m_argval.compare(m_cmpval) >= 0;
    case ARG_GT_CMP: eval(root); return   m_argval.compare(m_cmpval) >  0;
    case ARG_LE_CMP: eval(root); return   m_argval.compare(m_cmpval) <= 0;
    case ARG_LT_CMP: eval(root); return   m_argval.compare(m_cmpval) <  0;
    case ARG_IN_CMP:
    case ARG_NOT_IN_CMP:
    {
        bool in_cmp;
        C4_ASSERT(root.is_map());
        NodeRef n = root.find_child(m_cmp);
        if(n.valid())
        {
            if(n.is_map())
            {
                in_cmp = n.find_child(m_arg).valid();
            }
            else if(n.is_seq())
            {
                in_cmp = false;
                for(auto ch : n.children())
                {
                    if(ch.is_val() && ch.val() == m_arg)
                    {
                        in_cmp = true;
                        break;
                    }
                }
            }
            else
            {
                return false;
            }
            return m_ctype == ARG_IN_CMP ? in_cmp : ! in_cmp;
        }
        return false;
    }
    // do the other condition types
    default:
        C4_ERROR("unknown condition type");
        break;
    }
    C4_ERROR("never reach");
    return false;
}

void IfCondition::parse()
{
    /** @todo the scanning is inefficient. Use a for loop to iterate
     * through the string characters instead of calling s.find(). This 
     * was written in a hurry, which explains the lazy use of find(). */

    C4_ASSERT(m_str.first_of("{}*") == npos);
    auto pos = m_str.first_of('<');
    if(pos != npos)
    {
        C4_ASSERT(pos+2 < m_str.len);
        if(m_str[pos+1] == '=')
        {
            m_ctype = ARG_LE_CMP;
            m_arg = m_str.left_of(pos).trim(' ');
            m_cmp = m_str.right_of(pos+1, /*include*/true).trim(' ');
        }
        else
        {
            m_ctype = ARG_LT_CMP;
            m_arg = m_str.left_of(pos).trim(' ');
            m_cmp = m_str.right_of(pos+1, /*include*/true).trim(' ');
        }
        return;
    }

    pos = m_str.first_of('>');
    if(pos != npos)
    {
        C4_ASSERT(pos+2 < m_str.len);
        if(m_str[pos+1] == '=')
        {
            m_ctype = ARG_GE_CMP;
            m_arg = m_str.left_of(pos).trim(' ');
            m_cmp = m_str.right_of(pos+1, /*include*/true).trim(' ');
        }
        else
        {
            m_ctype = ARG_GT_CMP;
            m_arg = m_str.left_of(pos).trim(' ');
            m_cmp = m_str.right_of(pos+1, /*include*/true).trim(' ');
        }
        return;
    }

    pos = m_str.first_of('!');
    if(pos != npos) // !=
    {
        C4_ASSERT(pos+2 < m_str.len);
        C4_ASSERT(m_str[pos+1] == '=');
        m_ctype = ARG_NE_CMP;
        m_arg = m_str.left_of(pos).trim(' ');
        m_cmp = m_str.right_of(pos+1, /*include*/true).trim(' ');
        return;
    }

    pos = m_str.first_of('=');
    if(pos != npos) // ==
    {
        C4_ASSERT(pos+2 < m_str.len);
        C4_ASSERT(m_str[pos+1] == '=');
        m_ctype = ARG_EQ_CMP;
        m_arg = m_str.left_of(pos).trim(' ');
        m_cmp = m_str.right_of(pos+1, /*include*/true).trim(' ');
        return;
    }

    pos = m_str.find(" not in ");
    if(pos != npos) //
    {
        m_ctype = ARG_NOT_IN_CMP;
        m_arg = m_str.left_of(pos).trim(' ');
        m_cmp = m_str.right_of(pos+8, /*include*/true).trim(' ');
        return;
    }

    pos = m_str.find(" in ");
    if(pos != npos) //
    {
        m_ctype = ARG_IN_CMP;
        m_arg = m_str.left_of(pos).trim(' ');
        m_cmp = m_str.right_of(pos+4, /*include*/true).trim(' ');
        return;
    }

    m_ctype = ARG;
    m_arg = m_str.trim(' ');
    m_cmp.clear();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void TokenIf::parse(cspan *rem, TplLocation *curr_pos)
{
    base_type::parse(rem, curr_pos);

    C4_ASSERT(m_full_text.begins_with(stoken()));
    C4_ASSERT(m_full_text.ends_with(etoken()));

    // scan the condition
    cspan s = m_full_text;
    cspan c = _scan_condition(stoken(), &s);
    m_cond_blocks.emplace_back();
    auto *cb = &m_cond_blocks.back();
    cb->condition.init(this, c);
    cb->start = *curr_pos;
    cb->start.m_rope_pos.entry = m_rope_entry;
    C4_ASSERT(m_full_text.has_subspan(s));
    cb->start.m_rope_pos.i = s.begin() - m_full_text.begin();

    m_else_block.clear();
    m_else_block_offs = 0;

    // scan the branches
    while(1)
    {
        auto result = s.first_of_any("{% endif %}", "{% else %}", "{% elif ", "{% if ");
        C4_ERROR_IF_NOT(result, "invalid {% if %} structure");
        if(result.which == 0) // endif
        {
            cb->block = s.subspan(0, result.pos);
            break;
        }
        else if(result.which == 1) // else
        {
            cb->block = s.subspan(0, result.pos);
            s = s.subspan(result.pos + 10); // 10==strlen("{% else %}")
            auto pos = s.find("{% endif %}");
            C4_ERROR_IF(pos == npos, "invalid {% if %} structure");
            s = s.subspan(0, pos);
            m_else_block = s;
            m_else_block_offs = s.begin() - m_full_text.begin();
            break;
        }
        else if(result.which == 2) // elif
        {
            cb->block = s.subspan(0, result.pos);
            s = s.subspan(result.pos);
            auto cond = _scan_condition("{% elif ", &s);
            m_cond_blocks.emplace_back();
            cb = &m_cond_blocks.back();
            cb->condition.init(this, cond);
            cb->start.m_rope = m_start.m_rope;
            cb->start.m_rope_pos.entry = m_rope_entry;
            cb->start.m_rope_pos.i = s.begin() - m_full_text.begin();
        }
        else if(result.which == 3) // nested if
        {
            s = skip_nested(s.subspan(result.pos));
        }
        else
        {
            C4_ERROR("internal error");
        }
    }

    for(auto &cond : m_cond_blocks)
    {
        C4_ASSERT(m_full_text.has_subspan(cond.block));
        cond.block = cond.block.trim("\r\n");
        cond.start.m_rope_pos.i = cond.block.begin() - m_full_text.begin();
    }
    if( ! m_else_block.empty())
    {
        C4_ASSERT(m_full_text.has_subspan(m_else_block));
        m_else_block = m_else_block.trim("\r\n");
        m_else_block_offs = m_else_block.begin() - m_full_text.begin();
    }
}

void TokenIf::parse_body(TokenContainer *cont) const
{
    size_t my_id = cont->get_id(this); // defend against an eventual relocation
#define _c4this static_cast< TokenIf const* >(cont->get(my_id))

    for(auto const& b : _c4this->m_cond_blocks)
    {
        _c4this->_do_parse_body(b.block, b.start, cont);
    }

    if( ! _c4this->m_else_block.empty())
    {
        TplLocation else_pos = {_c4this->m_end.m_rope, {_c4this->m_rope_entry, _c4this->m_else_block_offs}};
        _c4this->_do_parse_body(_c4this->m_else_block, else_pos, cont);
    }

#undef _c4this
}

cspan TokenIf::_scan_condition(cspan token, cspan *s)
{
    C4_ASSERT(s->begins_with(token));
    auto pos = s->find("%}"); // this is where the {% if ... %} tag ends
    C4_ASSERT(pos != npos);
    cspan c = s->range(token.len, pos);
    c = c.trim(' ');
    *s = s->subspan(pos + 2);
    return c;
}

bool TokenIf::resolve(NodeRef const& root, cspan *value) const
{
    for(auto const& cb : m_cond_blocks)
    {
        if(cb.condition.resolve(root))
        {
            *value = cb.block;
            return true;
        }
    }
    *value = m_else_block;
    return true;
}

} // namespace tpl
} // namespace c4
