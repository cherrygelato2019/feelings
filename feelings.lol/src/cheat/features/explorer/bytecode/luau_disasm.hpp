#ifndef LUAU_DISASM_HPP
#define LUAU_DISASM_HPP

#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace luau_deasm
{
    static constexpr const char* k_active_signature = "\x1bLuau";

    static constexpr int k_type_version_min = 1;
    static constexpr int k_type_version_max = 3;
    static constexpr int k_bytecode_class_version = 100;

    static constexpr std::uint8_t k_const_nil = 0;
    static constexpr std::uint8_t k_const_boolean = 1;
    static constexpr std::uint8_t k_const_number = 2;
    static constexpr std::uint8_t k_const_string = 3;
    static constexpr std::uint8_t k_const_import = 4;
    static constexpr std::uint8_t k_const_table = 5;
    static constexpr std::uint8_t k_const_closure = 6;
    static constexpr std::uint8_t k_const_vector = 7;
    static constexpr std::uint8_t k_const_table_with_constants = 8;
    static constexpr std::uint8_t k_const_integer = 9;
    static constexpr std::uint8_t k_const_class_shape = 10;
    static constexpr std::uint8_t k_const_vectord = 11;

    static constexpr std::uint8_t k_proto_flag_inlinable = 1 << 3;

    static const int k_op_count = 91;

    static const char* k_op_names[k_op_count] = {
        "NOP",              // 0
        "BREAK",            // 1
        "LOADNIL",          // 2
        "LOADB",            // 3
        "LOADN",            // 4
        "LOADK",            // 5
        "MOVE",             // 6
        "GETGLOBAL",        // 7
        "SETGLOBAL",        // 8
        "GETUPVAL",         // 9
        "SETUPVAL",         // 10
        "CLOSEUPVALS",      // 11
        "GETIMPORT",        // 12
        "GETTABLE",         // 13
        "SETTABLE",         // 14
        "GETTABLEKS",       // 15
        "SETTABLEKS",       // 16
        "GETTABLEN",        // 17
        "SETTABLEN",        // 18
        "NEWCLOSURE",       // 19
        "NAMECALL",         // 20
        "CALL",             // 21
        "RETURN",           // 22
        "JUMP",             // 23
        "JUMPBACK",         // 24
        "JUMPIF",           // 25
        "JUMPIFNOT",        // 26
        "JUMPIFEQ",         // 27
        "JUMPIFLE",         // 28
        "JUMPIFLT",         // 29
        "JUMPIFNOTEQ",      // 30
        "JUMPIFNOTLE",      // 31
        "JUMPIFNOTLT",      // 32
        "ADD",              // 33
        "SUB",              // 34
        "MUL",              // 35
        "DIV",              // 36
        "MOD",              // 37
        "POW",              // 38
        "ADDK",             // 39
        "SUBK",             // 40
        "MULK",             // 41
        "DIVK",             // 42
        "MODK",             // 43
        "POWK",             // 44
        "AND",              // 45
        "OR",               // 46
        "ANDK",             // 47
        "ORK",              // 48
        "CONCAT",           // 49
        "NOT",              // 50
        "MINUS",            // 51
        "LENGTH",           // 52
        "NEWTABLE",         // 53
        "DUPTABLE",         // 54
        "SETLIST",          // 55
        "FORNPREP",         // 56
        "FORNLOOP",         // 57
        "FORGLOOP",         // 58
        "FORGPREP_INEXT",   // 59
        "FASTCALL3",        // 60
        "FORGPREP_NEXT",    // 61
        "NATIVECALL",       // 62
        "GETVARARGS",       // 63
        "DUPCLOSURE",       // 64
        "PREPVARARGS",      // 65
        "LOADKX",           // 66
        "JUMPX",            // 67
        "FASTCALL",         // 68
        "COVERAGE",         // 69
        "CAPTURE",          // 70
        "SUBRK",            // 71
        "DIVRK",            // 72
        "FASTCALL1",        // 73
        "FASTCALL2",        // 74
        "FASTCALL2K",       // 75
        "FORGPREP",         // 76
        "JUMPXEQKNIL",      // 77
        "JUMPXEQKB",        // 78
        "JUMPXEQKN",        // 79
        "JUMPXEQKS",        // 80
        "IDIV",             // 81
        "IDIVK",            // 82
        "GETUDATAKS",       // 83
        "SETUDATAKS",       // 84
        "NAMECALLUDATA",    // 85
        "NEWCLASSMEMBER",   // 86
        "CALLFB",           // 87
        "CMPPROTO",         // 88
        "FASTPCALL",        // 89
        "NEWCLASS"          // 90
    };

    struct constant
    {
        int kind = -1;
        bool boolean = false;
        double number = 0.0;
        std::int64_t integer = 0;
        unsigned string_id = 0;
        unsigned import_id = 0;
        unsigned closure_id = 0;
        double vector[4] = {0.0, 0.0, 0.0, 0.0};
        std::vector<int> table_keys;
        std::vector<std::pair<int, int>> table_keys_with_constants;
        std::vector<unsigned> class_shape;
    };

    struct locvar
    {
        unsigned name = 0;
        unsigned startpc = 0;
        unsigned endpc = 0;
        unsigned reg = 0;
    };

    struct proto
    {
        unsigned maxstacksize = 0;
        unsigned numparams = 0;
        unsigned nups = 0;
        bool is_vararg = false;
        unsigned flags = 0;
        std::vector<std::uint8_t> typeinfo;
        std::vector<std::uint32_t> code;
        std::vector<constant> constants;
        std::vector<unsigned> children;
        unsigned linedefined = 0;
        unsigned debugname = 0;
        std::vector<std::uint8_t> lineinfo_offsets;
        std::vector<std::int32_t> lineinfo_intervals;
        int linegaplog2 = 0;
        std::vector<locvar> locvars;
        std::vector<unsigned> upvalues;
        unsigned feedbackvecsize = 0;
        std::uint64_t cost = 0;
    };

    static inline int op_length(unsigned op)
    {
        switch (op)
        {
        case 7:   // GETGLOBAL
        case 8:   // SETGLOBAL
        case 12:  // GETIMPORT
        case 15:  // GETTABLEKS
        case 16:  // SETTABLEKS
        case 20:  // NAMECALL
        case 27:  // JUMPIFEQ
        case 28:  // JUMPIFLE
        case 29:  // JUMPIFLT
        case 30:  // JUMPIFNOTEQ
        case 31:  // JUMPIFNOTLE
        case 32:  // JUMPIFNOTLT
        case 53:  // NEWTABLE
        case 55:  // SETLIST
        case 58:  // FORGLOOP
        case 66:  // LOADKX
        case 74:  // FASTCALL2
        case 75:  // FASTCALL2K
        case 60:  // FASTCALL3
        case 77:  // JUMPXEQKNIL
        case 78:  // JUMPXEQKB
        case 79:  // JUMPXEQKN
        case 80:  // JUMPXEQKS
        case 83:  // GETUDATAKS
        case 84:  // SETUDATAKS
        case 85:  // NAMECALLUDATA
        case 86:  // NEWCLASSMEMBER
        case 87:  // CALLFB
        case 88:  // CMPPROTO
        case 90:  // NEWCLASS
            return 2;
        default:
            return 1;
        }
    }

    class disassembler
    {
    public:
        unsigned bytecode_version() const { return version_; }
        unsigned type_version() const { return typesversion_; }
        const std::vector<std::string>& strings() const { return strings_; }
        const std::vector<proto>& protos() const { return protos_; }
        unsigned main_proto() const { return main_; }
        const std::string& error() const { return error_; }

        bool parse(const std::string& bytes)
        {
            try {
            data_ = bytes;
            pos_ = 0;
            error_.clear();
            warnings_.clear();
            strings_.clear();
            protos_.clear();
            proto_count_ = 0;

            if (data_.size() >= 5 && std::memcmp(data_.data(), k_active_signature, 5) == 0)
            {
                error_ = "unexpected binary chunk wrapper, expected raw Luau bytecode";
                return false;
            }

            std::uint8_t version_byte = 0;
            if (!byte(version_byte))
                return false;
            version_ = version_byte;

            if (version_ == 0)
            {
                error_ = "bytecode version 0 carries an error message";
                return false;
            }

            if ((version_ < 3 || version_ > 14) && version_ != k_bytecode_class_version)
            {
                error_ = "unsupported bytecode version " + std::to_string(version_);
                return false;
            }

            if (version_ >= 4)
            {
                std::uint8_t tv = 0;
                if (!byte(tv))
                    return false;
                typesversion_ = tv;
                if (typesversion_ < k_type_version_min || typesversion_ > k_type_version_max)
                {
                    error_ = "unsupported type version " + std::to_string(typesversion_);
                    return false;
                }
            }

            unsigned string_count = 0;
            if (!varint(string_count))
                return false;
            if (string_count > remaining())
            {
                error_ = "string count overflows chunk";
                return false;
            }
            strings_.reserve(string_count);
            for (unsigned i = 0; i < string_count; ++i)
            {
                unsigned len = 0;
                if (!varint(len))
                    return false;
                if (len > remaining())
                {
                    error_ = "string overflows chunk";
                    return false;
                }
                std::string s(data_.data() + pos_, len);
                pos_ += len;
                strings_.push_back(std::move(s));
            }

            if (typesversion_ == 3)
            {
                for (;;)
                {
                    std::uint8_t index = 0;
                    if (!byte(index))
                        return false;
                    if (index == 0)
                        break;
                    if (!skip_string_ref())
                        return false;
                }
            }

            unsigned proto_count = 0;
            if (!varint(proto_count))
                return false;
            if (proto_count > remaining())
            {
                error_ = "proto count overflows chunk";
                return false;
            }
            proto_count_ = proto_count;
            protos_.reserve(proto_count);
            for (unsigned i = 0; i < proto_count; ++i)
            {
                if (!parse_proto())
                    return false;
            }

            if (!varint(main_))
                return false;
            if (main_ >= protos_.size())
            {
                error_ = "main proto index out of range";
                return false;
            }

            return true;
            } catch (...) {
                error_ = "exception during parse";
                return false;
            }
        }

        std::string disassemble() const
        {
            try {
            std::string out;
            if (!warnings_.empty())
            {
                out += "-- !! disassembler notes:\n";
                out += warnings_;
                out += '\n';
            }
            append(out, "-- Luau bytecode v%u (types v%u) strings=%zu protos=%zu main=%u\n",
                version_, typesversion_, strings_.size(), protos_.size(), main_);

            for (size_t i = 0; i < protos_.size(); ++i)
            {
                disassemble_proto(out, i);
            }

            return out;
            } catch (...) {
                return "-- exception during disassembly\n";
            }
        }

    private:
        bool byte(std::uint8_t& v)
        {
            if (pos_ >= data_.size())
            {
                error_ = "unexpected end of bytecode";
                return false;
            }
            v = static_cast<std::uint8_t>(data_[pos_]);
            ++pos_;
            return true;
        }

        size_t remaining() const
        {
            return data_.size() - pos_;
        }

        void warning(const char* fmt, ...)
        {
            char buf[512];
            va_list args;
            va_start(args, fmt);
            const int n = vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            if (n > 0)
            {
                if (!warnings_.empty())
                    warnings_ += '\n';
                warnings_ += buf;
            }
        }

        template <typename T>
        bool read_le(T& v)
        {
            if (pos_ + sizeof(T) > data_.size())
            {
                error_ = "unexpected end of bytecode";
                return false;
            }
            std::memcpy(&v, data_.data() + pos_, sizeof(T));
            pos_ += sizeof(T);
            return true;
        }

        bool varint(unsigned& value)
        {
            value = 0;
            unsigned shift = 0;
            for (;;)
            {
                std::uint8_t b = 0;
                if (!byte(b))
                    return false;
                value |= static_cast<unsigned>(b & 127) << shift;
                if ((b & 128) == 0)
                    break;
                shift += 7;
                if (shift > 28)
                {
                    error_ = "invalid varint";
                    return false;
                }
            }
            return true;
        }

        bool varint64(std::uint64_t& value)
        {
            value = 0;
            unsigned shift = 0;
            for (;;)
            {
                std::uint8_t b = 0;
                if (!byte(b))
                    return false;
                value |= static_cast<std::uint64_t>(b & 127) << shift;
                if ((b & 128) == 0)
                    break;
                shift += 7;
                if (shift > 63)
                {
                    error_ = "invalid varint64";
                    return false;
                }
            }
            return true;
        }

        bool skip_string_ref()
        {
            unsigned id = 0;
            if (!varint(id))
                return false;
            if (id > strings_.size())
            {
                error_ = "string reference out of range";
                return false;
            }
            return true;
        }

        bool read_string_ref(unsigned& id)
        {
            if (!varint(id))
                return false;
            if (id > strings_.size())
            {
                error_ = "string reference out of range";
                return false;
            }
            return true;
        }

        const std::string& string_at(unsigned id) const
        {
            static const std::string k_empty;
            if (id == 0 || id > strings_.size())
                return k_empty;
            return strings_[id - 1];
        }

        const constant& constant_at(const proto& p, int idx) const
        {
            static const constant k_invalid;
            if (idx < 0 || idx >= static_cast<int>(p.constants.size()))
                return k_invalid;
            return p.constants[idx];
        }

        bool parse_proto()
        {
            unsigned proto_size = 0;
            if (version_ >= 12 && !varint(proto_size))
                return false;
            const size_t proto_start = pos_;

            proto p;

            std::uint8_t maxstack = 0, numparams = 0, nups = 0, vararg = 0;
            if (!byte(maxstack) || !byte(numparams) || !byte(nups) || !byte(vararg))
                return false;
            p.maxstacksize = maxstack;
            p.numparams = numparams;
            p.nups = nups;
            p.is_vararg = vararg != 0;

            if (version_ >= 4)
            {
                std::uint8_t flags = 0;
                if (!byte(flags))
                    return false;
                p.flags = flags;

                if (typesversion_ == 1 || typesversion_ == 2 || typesversion_ == 3)
                {
                    unsigned type_size = 0;
                    if (!varint(type_size))
                        return false;
                    if (type_size)
                    {
                        if (type_size > remaining())
                        {
                            error_ = "type info overflows chunk";
                            return false;
                        }
                        p.typeinfo.assign(data_.data() + pos_, data_.data() + pos_ + type_size);
                        pos_ += type_size;
                    }
                }
            }

            unsigned sizecode = 0;
            if (!varint(sizecode))
                return false;
            if (sizecode * sizeof(std::uint32_t) > remaining())
            {
                error_ = "code overflows chunk";
                return false;
            }
            p.code.resize(sizecode);
            for (unsigned j = 0; j < sizecode; ++j)
            {
                std::uint32_t word = 0;
                if (!read_le(word))
                    return false;
                p.code[j] = word;
            }

            unsigned sizek = 0;
            if (!varint(sizek))
                return false;
            if (sizek > remaining())
            {
                error_ = "constant count overflows chunk";
                return false;
            }
            p.constants.reserve(sizek);
            for (unsigned j = 0; j < sizek; ++j)
            {
                constant c;
                if (!parse_constant(c))
                    return false;
                p.constants.push_back(std::move(c));
            }

            unsigned sizep = 0;
            if (!varint(sizep))
                return false;
            p.children.reserve(sizep);
            for (unsigned j = 0; j < sizep; ++j)
            {
                unsigned fid = 0;
                if (!varint(fid))
                    return false;
                if (fid >= proto_count_)
                    warning("proto #%u child id %u exceeds proto count %u",
                        static_cast<unsigned>(protos_.size()), fid, proto_count_);
                p.children.push_back(fid);
            }

            if (!varint(p.linedefined))
                return false;
            if (!read_string_ref(p.debugname))
                return false;

            std::uint8_t has_lineinfo = 0;
            if (!byte(has_lineinfo))
                return false;
            if (has_lineinfo)
            {
                std::uint8_t log2 = 0;
                if (!byte(log2))
                    return false;
                p.linegaplog2 = log2;

                if (!p.code.empty())
                {
                    int intervals = static_cast<int>((p.code.size() - 1) >> log2) + 1;
                    size_t needed = p.code.size() + static_cast<size_t>(intervals) * sizeof(std::int32_t);
                    if (needed > remaining())
                    {
                        error_ = "line info overflows chunk";
                        return false;
                    }
                    p.lineinfo_offsets.resize(p.code.size());
                    std::uint8_t lastoffset = 0;
                    for (size_t j = 0; j < p.code.size(); ++j)
                    {
                        std::uint8_t v = 0;
                        if (!byte(v))
                            return false;
                        lastoffset += v;
                        p.lineinfo_offsets[j] = lastoffset;
                    }
                    p.lineinfo_intervals.resize(static_cast<size_t>(intervals));
                    int lastline = 0;
                    for (int j = 0; j < intervals; ++j)
                    {
                        std::int32_t v = 0;
                        if (!read_le(v))
                            return false;
                        lastline += v;
                        p.lineinfo_intervals[j] = lastline;
                    }
                }
            }

            std::uint8_t debuginfo = 0;
            if (!byte(debuginfo))
                return false;
            if (debuginfo)
            {
                unsigned sizelocvars = 0;
                if (!varint(sizelocvars))
                    return false;
                if (sizelocvars > remaining())
                {
                    error_ = "locvar count overflows chunk";
                    return false;
                }
                p.locvars.reserve(sizelocvars);
                for (unsigned j = 0; j < sizelocvars; ++j)
                {
                    locvar lv;
                    if (!read_string_ref(lv.name))
                        return false;
                    if (!varint(lv.startpc) || !varint(lv.endpc))
                        return false;
                    std::uint8_t reg = 0;
                    if (!byte(reg))
                        return false;
                    lv.reg = reg;
                    p.locvars.push_back(lv);
                }

                unsigned sizeupvalues = 0;
                if (!varint(sizeupvalues))
                    return false;
                if (sizeupvalues > remaining())
                {
                    error_ = "upvalue count " + std::to_string(sizeupvalues) +
                             " overflows chunk (remaining " + std::to_string(remaining()) + " bytes)";
                    return false;
                }
                p.upvalues.reserve(sizeupvalues);
                for (unsigned j = 0; j < sizeupvalues; ++j)
                {
                    unsigned id = 0;
                    if (!read_string_ref(id))
                        return false;
                    p.upvalues.push_back(id);
                }
            }

            if (version_ >= 11)
            {
                if (!varint(p.feedbackvecsize))
                    return false;
                for (unsigned j = 0; j < p.feedbackvecsize; ++j)
                {
                    std::uint8_t slottype = 0;
                    if (!byte(slottype))
                        return false;
                    unsigned pc = 0;
                    if (!varint(pc))
                        return false;
                }
            }

            if (version_ >= 12)
            {
                if ((p.flags & k_proto_flag_inlinable) != 0)
                {
                    if (!varint64(p.cost))
                        return false;
                }
                pos_ = proto_start + proto_size;
                if (pos_ > data_.size())
                {
                    error_ = "proto size overflows chunk";
                    return false;
                }
            }

            protos_.push_back(std::move(p));
            return true;
        }

        bool parse_constant(constant& c)
        {
            std::uint8_t kind = 0;
            if (!byte(kind))
                return false;
            c.kind = kind;

            switch (kind)
            {
            case k_const_nil:
                break;

            case k_const_boolean:
            {
                std::uint8_t v = 0;
                if (!byte(v))
                    return false;
                c.boolean = v != 0;
                break;
            }

            case k_const_number:
            {
                double v = 0.0;
                if (!read_le(v))
                    return false;
                c.number = v;
                break;
            }

            case k_const_vector:
            {
                float v[4] = {0, 0, 0, 0};
                for (int i = 0; i < 4; ++i)
                {
                    if (!read_le(v[i]))
                        return false;
                }
                for (int i = 0; i < 4; ++i)
                    c.vector[i] = v[i];
                break;
            }

            case k_const_vectord:
            {
                for (int i = 0; i < 4; ++i)
                {
                    if (!read_le(c.vector[i]))
                        return false;
                }
                break;
            }

            case k_const_string:
                if (!read_string_ref(c.string_id))
                    return false;
                break;

            case k_const_import:
                if (!read_le(c.import_id))
                    return false;
                break;

            case k_const_table:
            {
                unsigned keys = 0;
                if (!varint(keys))
                    return false;
                if (keys > remaining())
                {
                    error_ = "table constant too large";
                    return false;
                }
                c.table_keys.reserve(keys);
                for (unsigned i = 0; i < keys; ++i)
                {
                    unsigned key = 0;
                    if (!varint(key))
                        return false;
                    c.table_keys.push_back(static_cast<int>(key));
                }
                break;
            }

            case k_const_table_with_constants:
            {
                unsigned keys = 0;
                if (!varint(keys))
                    return false;
                c.table_keys_with_constants.reserve(keys);
                for (unsigned i = 0; i < keys; ++i)
                {
                    unsigned key = 0;
                    if (!varint(key))
                        return false;
                    std::int32_t const_idx = 0;
                    if (!read_le(const_idx))
                        return false;
                    c.table_keys_with_constants.emplace_back(static_cast<int>(key), const_idx);
                }
                break;
            }

            case k_const_closure:
                if (!varint(c.closure_id))
                    return false;
                if (c.closure_id >= proto_count_)
                    warning("closure references proto id %u exceeds proto count %u",
                        c.closure_id, proto_count_);
                break;

            case k_const_integer:
            {
                std::uint8_t negative = 0;
                if (!byte(negative))
                    return false;
                std::uint64_t magnitude = 0;
                if (!varint64(magnitude))
                    return false;
                c.integer = negative ? static_cast<std::int64_t>(~magnitude + 1) : static_cast<std::int64_t>(magnitude);
                break;
            }

            case k_const_class_shape:
            {
                unsigned cnid = 0, props = 0, methods = 0;
                if (!varint(cnid) || !varint(props) || !varint(methods))
                    return false;
                c.class_shape.push_back(cnid);
                c.class_shape.push_back(props);
                c.class_shape.push_back(methods);
                unsigned members = props + methods;
                for (unsigned i = 0; i < members; ++i)
                {
                    unsigned mid = 0;
                    if (!varint(mid))
                        return false;
                    c.class_shape.push_back(mid);
                }
                break;
            }

            default:
                error_ = "unknown constant kind " + std::to_string(kind);
                return false;
            }

            return true;
        }

        static unsigned insn_a(unsigned w) { return (w >> 8) & 0xff; }
        static unsigned insn_b(unsigned w) { return (w >> 16) & 0xff; }
        static unsigned insn_c(unsigned w) { return (w >> 24) & 0xff; }
        static int insn_d(unsigned w) { return static_cast<int>(w) >> 16; }
        static int insn_e(unsigned w) { return static_cast<int>(w) >> 8; }

        static void append(std::string& out, const char* fmt, ...)
        {
            char buf[512];
            va_list args;
            va_start(args, fmt);
            const int n = vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            if (n > 0)
                out.append(buf, static_cast<size_t>(n));
        }

        static bool printable(const std::string& s)
        {
            for (char ch : s)
            {
                if (static_cast<unsigned char>(ch) < ' ')
                    return false;
            }
            return true;
        }

        static std::string escape_string(const std::string& s, size_t limit)
        {
            std::string out;
            out += '\'';
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (i >= limit)
                {
                    out += "...";
                    break;
                }
                const unsigned char ch = static_cast<unsigned char>(s[i]);
                if (ch < ' ')
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02X", ch);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(ch);
                }
            }
            out += '\'';
            return out;
        }

        std::string format_constant(const proto& p, const constant& c) const
        {
            switch (c.kind)
            {
            case k_const_nil:
                return "nil";
            case k_const_boolean:
                return c.boolean ? "true" : "false";
            case k_const_number:
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.17g", c.number);
                return buf;
            }
            case k_const_integer:
                return std::to_string(c.integer);
            case k_const_string:
            {
                const std::string& s = string_at(c.string_id);
                return printable(s) ? escape_string(s, 32) : escape_string(s, 32);
            }
            case k_const_vector:
            {
                char buf[160];
                snprintf(buf, sizeof(buf), "vector(%.9g, %.9g, %.9g, %.9g)", c.vector[0], c.vector[1], c.vector[2], c.vector[3]);
                return buf;
            }
            case k_const_vectord:
            {
                char buf[160];
                snprintf(buf, sizeof(buf), "vector(%.17g, %.17g, %.17g, %.17g)", c.vector[0], c.vector[1], c.vector[2], c.vector[3]);
                return buf;
            }
            case k_const_import:
                return "import(" + std::to_string(c.import_id) + ")";
            case k_const_table:
                return "{table}";
            case k_const_table_with_constants:
                return "{table}";
            case k_const_closure:
                return "function (P" + std::to_string(c.closure_id) + ")";
            case k_const_class_shape:
                return "class";
            default:
                return "?";
            }
        }

        std::string constant_text(const proto& p, int idx) const
        {
            const constant& c = constant_at(p, idx);
            return format_constant(p, c);
        }

        int decompose_import(unsigned ids, int& id0, int& id1, int& id2) const
        {
            const int count = static_cast<int>(ids >> 30);
            id0 = count > 0 ? static_cast<int>(ids >> 20) & 1023 : -1;
            id1 = count > 1 ? static_cast<int>(ids >> 10) & 1023 : -1;
            id2 = count > 2 ? static_cast<int>(ids) & 1023 : -1;
            return count;
        }

        std::string import_path(const proto& p, unsigned aux) const
        {
            int id0 = -1, id1 = -1, id2 = -1;
            const int count = decompose_import(aux, id0, id1, id2);
            if (count <= 0)
                return "?";
            std::string path;
            int ids[3] = {id0, id1, id2};
            for (int i = 0; i < count; ++i)
            {
                const std::string& s = string_at(constant_at(p, ids[i]).string_id);
                if (i)
                    path += '.';
                path += s;
            }
            return path;
        }

        std::string constant_key(const proto& p, unsigned aux) const
        {
            return string_at(constant_at(p, static_cast<int>(aux)).string_id);
        }

        void disassemble_proto(std::string& out, size_t index) const
        {
            const proto& p = protos_[index];
            append(out, "-- proto #%zu", index);
            if (index == main_)
                out += " <main>";
            append(out, " name='%s'", escape_string(string_at(p.debugname), 64).c_str());
            append(out, " line=%u params=%u vararg=%d nups=%u maxstack=%u flags=0x%02X\n",
                p.linedefined, p.numparams, p.is_vararg ? 1 : 0, p.nups, p.maxstacksize, p.flags);

            for (size_t k = 0; k < p.constants.size(); ++k)
            {
                if (p.constants[k].kind < 0)
                    continue;
                append(out, "    K%zu = %s\n", k, format_constant(p, p.constants[k]).c_str());
            }

            size_t pc = 0;
            while (pc < p.code.size())
            {
                const size_t line_start = out.size();
                const unsigned op = p.code[pc] & 0xff;
                const char* name = op < static_cast<unsigned>(k_op_count) ? k_op_names[op] : "UNKNOWN";

                char prefix[96];
                snprintf(prefix, sizeof(prefix), "[%04zu] %-12s ", pc, name);
                out += prefix;

                const int len = op_length(op);
                const unsigned a = insn_a(p.code[pc]);
                const unsigned b = insn_b(p.code[pc]);
                const unsigned c = insn_c(p.code[pc]);
                const int d = insn_d(p.code[pc]);
                const int e = insn_e(p.code[pc]);
                unsigned aux = 0;
                if (len == 2 && pc + 1 < p.code.size())
                    aux = p.code[pc + 1];

                std::string detail = instruction_detail(p, pc, op, a, b, c, d, e, aux);

                switch (op)
                {
                case 0:   // NOP
                case 1:   // BREAK
                case 62:  // NATIVECALL
                    break;
                case 2:   // LOADNIL
                case 11:  // CLOSEUPVALS
                case 65:  // PREPVARARGS
                    append(out, "%u", a);
                    break;
                case 4:   // LOADN
                case 5:   // LOADK
                case 19:  // NEWCLOSURE
                case 54:  // DUPTABLE
                case 64:  // DUPCLOSURE
                case 56:  // FORNPREP
                case 57:  // FORNLOOP
                case 59:  // FORGPREP_INEXT
                case 61:  // FORGPREP_NEXT
                case 76:  // FORGPREP
                    append(out, "%u %d", a, d);
                    break;
                case 6:   // MOVE
                case 50:  // NOT
                case 51:  // MINUS
                case 52:  // LENGTH
                case 63:  // GETVARARGS
                case 9:   // GETUPVAL
                case 10:  // SETUPVAL
                case 3:   // LOADB
                case 70:  // CAPTURE
                case 22:  // RETURN
                    append(out, "%u %u", a, b);
                    break;
                case 33:  // ADD
                case 34:  // SUB
                case 35:  // MUL
                case 36:  // DIV
                case 37:  // MOD
                case 38:  // POW
                case 39:  // ADDK
                case 40:  // SUBK
                case 41:  // MULK
                case 42:  // DIVK
                case 43:  // MODK
                case 44:  // POWK
                case 45:  // AND
                case 46:  // OR
                case 47:  // ANDK
                case 48:  // ORK
                case 49:  // CONCAT
                case 71:  // SUBRK
                case 72:  // DIVRK
                case 81:  // IDIV
                case 82:  // IDIVK
                case 89:  // FASTPCALL
                    append(out, "%u %u %u", a, b, c);
                    break;
                case 23:  // JUMP
                case 24:  // JUMPBACK
                    append(out, "%d", d);
                    break;
                case 25:  // JUMPIF
                case 26:  // JUMPIFNOT
                    append(out, "%u %d", a, d);
                    break;
                case 27:  // JUMPIFEQ
                case 28:  // JUMPIFLE
                case 29:  // JUMPIFLT
                case 30:  // JUMPIFNOTEQ
                case 31:  // JUMPIFNOTLE
                case 32:  // JUMPIFNOTLT
                case 77:  // JUMPXEQKNIL
                case 78:  // JUMPXEQKB
                case 79:  // JUMPXEQKN
                case 80:  // JUMPXEQKS
                case 88:  // CMPPROTO
                    append(out, "%u %d", a, d);
                    break;
                case 13:  // GETTABLE
                case 14:  // SETTABLE
                case 17:  // GETTABLEN
                case 18:  // SETTABLEN
                case 21:  // CALL
                case 53:  // NEWTABLE
                case 55:  // SETLIST
                case 60:  // FASTCALL3
                case 83:  // GETUDATAKS
                case 84:  // SETUDATAKS
                case 86:  // NEWCLASSMEMBER
                    append(out, "%u %u %u", a, b, c);
                    break;
                case 7:   // GETGLOBAL
                case 8:   // SETGLOBAL
                    append(out, "%u %u", a, c);
                    break;
                case 12:  // GETIMPORT
                    append(out, "%u %d", a, d);
                    break;
                case 15:  // GETTABLEKS
                case 16:  // SETTABLEKS
                case 20:  // NAMECALL
                case 85:  // NAMECALLUDATA
                case 87:  // CALLFB
                case 90:  // NEWCLASS
                    append(out, "%u %u %u", a, b, c);
                    break;
                case 58:  // FORGLOOP
                    append(out, "%u %d", a, d);
                    break;
                case 66:  // LOADKX
                    append(out, "%u", a);
                    break;
                case 67:  // JUMPX
                case 69:  // COVERAGE
                    append(out, "%d", e);
                    break;
                case 68:  // FASTCALL
                    append(out, "%u %u", a, c);
                    break;
                case 73:  // FASTCALL1
                    append(out, "%u %u %u", a, b, c);
                    break;
                case 74:  // FASTCALL2
                case 75:  // FASTCALL2K
                    append(out, "%u %u %u", a, b, c);
                    break;
                default:
                    append(out, "%u %u %u", a, b, c);
                    break;
                }

                if (len == 2)
                {
                    const size_t used = out.size() - line_start;
                    if (used < 24)
                        out.append(24 - used, ' ');
                    else
                        out += ' ';
                    char auxbuf[16];
                    snprintf(auxbuf, sizeof(auxbuf), "0x%08X", aux);
                    out += auxbuf;
                }

                if (!detail.empty())
                {
                    size_t used = out.size() - line_start;
                    if (used < 64)
                        out.append(64 - used, ' ');
                    out += "; ";
                    out += detail;
                }

                out += '\n';
                pc += static_cast<size_t>(len);
            }
        }

        std::string instruction_detail(const proto& p, size_t pc, unsigned op, unsigned a, unsigned b, unsigned c, int d, int e, unsigned aux) const
        {
            switch (op)
            {
            case 2:   // LOADNIL
                return "R" + std::to_string(a) + " = nil";
            case 3:   // LOADB
                return "R" + std::to_string(a) + " = " + (b ? "true" : "false");
            case 4:   // LOADN
                return "R" + std::to_string(a) + " = " + std::to_string(d);
            case 5:   // LOADK
                return "R" + std::to_string(a) + " = K" + std::to_string(d) + " (" + constant_text(p, d) + ")";
            case 6:   // MOVE
                return "R" + std::to_string(a) + " = R" + std::to_string(b);
            case 7:   // GETGLOBAL
                return "R" + std::to_string(a) + " = getglobal '" + constant_key(p, aux) + "'";
            case 8:   // SETGLOBAL
                return "setglobal '" + constant_key(p, aux) + "' = R" + std::to_string(a);
            case 9:   // GETUPVAL
                return "R" + std::to_string(a) + " = U" + std::to_string(b);
            case 10:  // SETUPVAL
                return "U" + std::to_string(b) + " = R" + std::to_string(a);
            case 11:  // CLOSEUPVALS
                return "close upvalues at R" + std::to_string(a);
            case 12:  // GETIMPORT
                return "R" + std::to_string(a) + " = import(" + import_path(p, aux) + ")";
            case 13:  // GETTABLE
                return "R" + std::to_string(a) + " = R" + std::to_string(b) + "[R" + std::to_string(c) + "]";
            case 14:  // SETTABLE
                return "R" + std::to_string(b) + "[R" + std::to_string(c) + "] = R" + std::to_string(a);
            case 15:  // GETTABLEKS
                return "R" + std::to_string(a) + " = R" + std::to_string(b) + "." + constant_key(p, aux);
            case 16:  // SETTABLEKS
                return "R" + std::to_string(b) + "." + constant_key(p, aux) + " = R" + std::to_string(a);
            case 17:  // GETTABLEN
                return "R" + std::to_string(a) + " = R" + std::to_string(b) + "[" + std::to_string(c + 1) + "]";
            case 18:  // SETTABLEN
                return "R" + std::to_string(b) + "[" + std::to_string(c + 1) + "] = R" + std::to_string(a);
            case 19:  // NEWCLOSURE
                return "R" + std::to_string(a) + " = function (P" + std::to_string(d) + ")";
            case 20:  // NAMECALL
            case 85:  // NAMECALLUDATA
                return "R" + std::to_string(a) + " = method R" + std::to_string(b) + "." + constant_key(p, aux);
            case 21:  // CALL
            case 87:  // CALLFB
            {
                std::string s = "call R" + std::to_string(a);
                if (b)
                    s += " (" + std::to_string(b - 1) + " args)";
                if (c)
                    s += " -> " + std::to_string(c - 1) + " results";
                return s;
            }
            case 22:  // RETURN
            {
                std::string s = "return from R" + std::to_string(a);
                if (b)
                    s += " (" + std::to_string(b - 1) + " values)";
                else
                    s += " (all)";
                return s;
            }
            case 23:  // JUMP
            case 24:  // JUMPBACK
                return "-> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 25:  // JUMPIF
            case 26:  // JUMPIFNOT
                return "if " + std::string(op == 25 ? "" : "not ") + "R" + std::to_string(a) + " -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 27:  // JUMPIFEQ
            case 28:  // JUMPIFLE
            case 29:  // JUMPIFLT
            case 30:  // JUMPIFNOTEQ
            case 31:  // JUMPIFNOTLE
            case 32:  // JUMPIFNOTLT
            {
                static const char* cmp[] = {"==", "<=", "<", "~=", ">", ">="};
                return "if R" + std::to_string(a) + " " + cmp[op - 27] + " R" + std::to_string(aux & 0xff) + " -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            }
            case 33:  // ADD
            case 34:  // SUB
            case 35:  // MUL
            case 36:  // DIV
            case 37:  // MOD
            case 38:  // POW
            case 81:  // IDIV
            {
                static const char* arith[] = {"+", "-", "*", "/", "%", "^", "//"};
                const int idx = op == 81 ? 6 : op - 33;
                return "R" + std::to_string(a) + " = R" + std::to_string(b) + " " + arith[idx] + " R" + std::to_string(c);
            }
            case 39:  // ADDK
            case 40:  // SUBK
            case 41:  // MULK
            case 42:  // DIVK
            case 43:  // MODK
            case 44:  // POWK
            case 82:  // IDIVK
            {
                static const char* arith[] = {"+", "-", "*", "/", "%", "^", "//"};
                const int idx = op == 82 ? 6 : op - 39;
                return "R" + std::to_string(a) + " = R" + std::to_string(b) + " " + arith[idx] + " K" + std::to_string(c) + " (" + constant_text(p, c) + ")";
            }
            case 45:  // AND
            case 46:  // OR
                return "R" + std::to_string(a) + " = R" + std::to_string(b) + (op == 45 ? " and " : " or ") + "R" + std::to_string(c);
            case 47:  // ANDK
            case 48:  // ORK
                return "R" + std::to_string(a) + " = R" + std::to_string(b) + (op == 47 ? " and " : " or ") + "K" + std::to_string(c) + " (" + constant_text(p, c) + ")";
            case 49:  // CONCAT
                return "R" + std::to_string(a) + " = concat R" + std::to_string(b) + "..R" + std::to_string(c);
            case 50:  // NOT
                return "R" + std::to_string(a) + " = not R" + std::to_string(b);
            case 51:  // MINUS
                return "R" + std::to_string(a) + " = -R" + std::to_string(b);
            case 52:  // LENGTH
                return "R" + std::to_string(a) + " = #R" + std::to_string(b);
            case 53:  // NEWTABLE
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "R%u = {} array=%u hash=%u", a, aux, b ? (1u << (b - 1)) : 0u);
                return buf;
            }
            case 54:  // DUPTABLE
                return "R" + std::to_string(a) + " = copy K" + std::to_string(d) + " (" + constant_text(p, d) + ")";
            case 55:  // SETLIST
            {
                std::string s = "R" + std::to_string(a) + "[";
                s += std::to_string(aux + 1);
                s += "] = ";
                s += (c ? std::to_string(c - 1) : std::string("all")) + " values from R" + std::to_string(b);
                return s;
            }
            case 56:  // FORNPREP
            case 57:  // FORNLOOP
                return "numeric for R" + std::to_string(a) + " -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 58:  // FORGLOOP
                return "generic for R" + std::to_string(a) + " vars=" + std::to_string(aux & 0xff) + " -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 59:  // FORGPREP_INEXT
            case 61:  // FORGPREP_NEXT
                return "for R" + std::to_string(a) + (op == 59 ? " (ipairs)" : " (pairs)") + " -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 60:  // FASTCALL3
            case 73:  // FASTCALL1
            case 74:  // FASTCALL2
            case 75:  // FASTCALL2K
            case 89:  // FASTPCALL
            {
                std::string s = "fastcall " + std::to_string(a);
                s += " args R" + std::to_string(b);
                if (op == 60)
                {
                    s += ", R" + std::to_string(aux & 0xff);
                    s += ", R" + std::to_string((aux >> 8) & 0xff);
                }
                if (op == 74)
                    s += ", R" + std::to_string(aux & 0xff);
                if (op == 75)
                    s += ", K" + std::to_string(aux & 0xffffff) + " (" + constant_text(p, aux & 0xffffff) + ")";
                return s;
            }
            case 63:  // GETVARARGS
                return b ? "R" + std::to_string(a) + " = " + std::to_string(b - 1) + " varargs" : "R" + std::to_string(a) + " = all varargs";
            case 64:  // DUPCLOSURE
                return "R" + std::to_string(a) + " = cached function K" + std::to_string(d) + " (P" + std::to_string(constant_at(p, d).closure_id) + ")";
            case 65:  // PREPVARARGS
                return "prep varargs (fixed " + std::to_string(a) + ")";
            case 66:  // LOADKX
                return "R" + std::to_string(a) + " = K" + std::to_string(aux) + " (" + constant_text(p, aux) + ")";
            case 67:  // JUMPX
                return "-> pc " + std::to_string(pc + static_cast<size_t>(e) + 1);
            case 68:  // FASTCALL
            case 69:  // COVERAGE
                return "";
            case 70:  // CAPTURE
            {
                static const char* captures[] = {"VAL", "REF", "UPVAL"};
                const char* kind = a < 3 ? captures[a] : "?";
                return "capture " + std::string(kind) + " " + (a == 2 ? ("U" + std::to_string(b)) : ("R" + std::to_string(b)));
            }
            case 71:  // SUBRK
            case 72:  // DIVRK
                return "R" + std::to_string(a) + " = K" + std::to_string(b) + " (" + constant_text(p, b) + ") " + (op == 71 ? "-" : "/") + " R" + std::to_string(c);
            case 76:  // FORGPREP
                return "generic for R" + std::to_string(a) + " -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 77:  // JUMPXEQKNIL
                return "if R" + std::to_string(a) + (aux >> 31 ? " !~ " : " == ") + "nil -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 78:  // JUMPXEQKB
                return "if R" + std::to_string(a) + (aux >> 31 ? " ~= " : " == ") + (aux & 1 ? "true" : "false") + " -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 79:  // JUMPXEQKN
                return "if R" + std::to_string(a) + (aux >> 31 ? " ~= " : " == ") + "K" + std::to_string(aux & 0xffffff) + " (" + constant_text(p, aux & 0xffffff) + ") -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 80:  // JUMPXEQKS
                return "if R" + std::to_string(a) + (aux >> 31 ? " ~= " : " == ") + "K" + std::to_string(aux & 0xffffff) + " (" + constant_text(p, aux & 0xffffff) + ") -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 83:  // GETUDATAKS
            case 84:  // SETUDATAKS
                return (op == 83 ? "R" + std::to_string(a) + " = R" + std::to_string(b) : "R" + std::to_string(b)) + "." + constant_key(p, aux & 0xffff);
            case 86:  // NEWCLASSMEMBER
                return "member R" + std::to_string(a) + "." + constant_key(p, aux & 0xffff) + " = R" + std::to_string(c);
            case 88:  // CMPPROTO
                return "if closure is P" + std::to_string(aux) + " -> pc " + std::to_string(pc + static_cast<size_t>(d) + 1);
            case 90:  // NEWCLASS
                return "R" + std::to_string(a) + " = new class K" + std::to_string(aux & 0xffffff);
            default:
                return "";
            }
        }

        std::string data_;
        size_t pos_ = 0;
        unsigned version_ = 0;
        unsigned typesversion_ = 0;
        unsigned main_ = 0;
        unsigned proto_count_ = 0;
        std::string error_;
        std::string warnings_;
        std::vector<std::string> strings_;
        std::vector<proto> protos_;
    };

    static inline std::string disassemble(const std::string& bytecode)
    {
        disassembler d;
        if (!d.parse(bytecode))
            return "!! disassemble failed: " + d.error() + " (v" + std::to_string(d.bytecode_version()) +
                   ", ty" + std::to_string(d.type_version()) + ")";
        return d.disassemble();
    }
}

#endif