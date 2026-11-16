// Adapted from exoteric33 a50471a runtime/src/vu_micro.cpp (MIT).
#pragma once
#include "ps2/vu_ops.hpp"
#include <stdexcept>
namespace ps2native::engine::vu_detail {
using namespace ps2;
using namespace ps2::vu;
struct Unit {
    Vu0Regs* regs{};
    std::uint8_t* code{};
    std::uint8_t* data{};
    std::uint32_t code_mask{},data_qwords{},top{},itop{},number{};
    vuops::FlagPipe* pipe{};
    const std::uint16_t* branch_vi{};
    std::int32_t fsset{-1};
};
struct UpperOut {
    int target = -1;
    Reg128 value{};
    unsigned dest = 0;
    bool flags = false;
    bool has_clip = false;
    std::uint32_t clip = 0;
};

constexpr int kAcc = 32;

PS2_VU_INLINE std::uint32_t mac_flags(const Reg128 &v, unsigned dest) {
    std::uint32_t mac = 0;
    for (int i = 0; i < 4; ++i) {
        if (!(dest & (8u >> i))) continue;
        const std::uint32_t bit = 3u - static_cast<std::uint32_t>(i);
        const std::uint32_t x = v.w[i];
        if (!(x & 0x7F800000u)) mac |= 1u << bit;
        if (x & 0x80000000u) mac |= 1u << (bit + 4);
        if ((x & 0x7FFFFFFFu) == 0x7FFFFFFFu) mac |= 1u << (bit + 12);
    }
    return mac;
}

PS2_VU_INLINE void set_flags(Vu0Regs &r, const Reg128 &v, unsigned dest) {
    const std::uint32_t mac = mac_flags(v, dest);
    r.mac = mac;
    std::uint32_t status = r.status & 0xFF0u;
    if (mac & 0x000Fu) status |= 0x1u;
    if (mac & 0x00F0u) status |= 0x2u;
    if (mac & 0x0F00u) status |= 0x4u;
    if (mac & 0xF000u) status |= 0x8u;
    status |= (status & 0xFu) << 6;
    r.status = (status & ~0x30u) | (r.status & 0x30u);
}

template <typename F>
UpperOut lanes(int target, unsigned dest, bool flags, F f) {
    UpperOut o;
    o.target = target;
    o.dest = dest;
    o.flags = flags;
    for (int i = 0; i < 4; ++i)
        if (dest & (8u >> i)) o.value.w[i] = f(i);
    return o;
}

PS2_VU_INLINE UpperOut upper(Vu0Regs &r, std::uint32_t w) {
    const unsigned dest = (w >> 21) & 15u, ft = (w >> 16) & 31u, fs = (w >> 11) & 31u, fd = (w >> 6) & 31u, bc = w & 3u;
    const Reg128 s = r.vf[fs], t = r.vf[ft], acc = r.acc;
    const std::uint32_t b = t.w[bc];
    const unsigned funct = w & 63u;
    if (funct < 0x3C) {
        switch (funct) {
        case 0x00: case 0x01: case 0x02: case 0x03: return lanes(fd, dest, true, [&](int i) { return fp::add(s.w[i], b); });
        case 0x04: case 0x05: case 0x06: case 0x07: return lanes(fd, dest, true, [&](int i) { return fp::sub(s.w[i], b); });
        case 0x08: case 0x09: case 0x0A: case 0x0B: return lanes(fd, dest, true, [&](int i) { return fp::add(acc.w[i], fp::mul(s.w[i], b)); });
        case 0x0C: case 0x0D: case 0x0E: case 0x0F: return lanes(fd, dest, true, [&](int i) { return fp::sub(acc.w[i], fp::mul(s.w[i], b)); });
        case 0x10: case 0x11: case 0x12: case 0x13: return lanes(fd, dest, false, [&](int i) { return fp::max(s.w[i], b); });
        case 0x14: case 0x15: case 0x16: case 0x17: return lanes(fd, dest, false, [&](int i) { return fp::min(s.w[i], b); });
        case 0x18: case 0x19: case 0x1A: case 0x1B: return lanes(fd, dest, true, [&](int i) { return fp::mul(s.w[i], b); });
        case 0x1C: return lanes(fd, dest, true, [&](int i) { return fp::mul(s.w[i], r.q); });
        case 0x1D: return lanes(fd, dest, false, [&](int i) { return fp::max(s.w[i], r.i); });
        case 0x1E: return lanes(fd, dest, true, [&](int i) { return fp::mul(s.w[i], r.i); });
        case 0x1F: return lanes(fd, dest, false, [&](int i) { return fp::min(s.w[i], r.i); });
        case 0x20: return lanes(fd, dest, true, [&](int i) { return fp::add(s.w[i], r.q); });
        case 0x21: return lanes(fd, dest, true, [&](int i) { return fp::add(acc.w[i], fp::mul(s.w[i], r.q)); });
        case 0x22: return lanes(fd, dest, true, [&](int i) { return fp::add(s.w[i], r.i); });
        case 0x23: return lanes(fd, dest, true, [&](int i) { return fp::add(acc.w[i], fp::mul(s.w[i], r.i)); });
        case 0x24: return lanes(fd, dest, true, [&](int i) { return fp::sub(s.w[i], r.q); });
        case 0x25: return lanes(fd, dest, true, [&](int i) { return fp::sub(acc.w[i], fp::mul(s.w[i], r.q)); });
        case 0x26: return lanes(fd, dest, true, [&](int i) { return fp::sub(s.w[i], r.i); });
        case 0x27: return lanes(fd, dest, true, [&](int i) { return fp::sub(acc.w[i], fp::mul(s.w[i], r.i)); });
        case 0x28: return lanes(fd, dest, true, [&](int i) { return fp::add(s.w[i], t.w[i]); });
        case 0x29: return lanes(fd, dest, true, [&](int i) { return fp::add(acc.w[i], fp::mul(s.w[i], t.w[i])); });
        case 0x2A: return lanes(fd, dest, true, [&](int i) { return fp::mul(s.w[i], t.w[i]); });
        case 0x2B: return lanes(fd, dest, false, [&](int i) { return fp::max(s.w[i], t.w[i]); });
        case 0x2C: return lanes(fd, dest, true, [&](int i) { return fp::sub(s.w[i], t.w[i]); });
        case 0x2D: return lanes(fd, dest, true, [&](int i) { return fp::sub(acc.w[i], fp::mul(s.w[i], t.w[i])); });
        case 0x2E: {
            UpperOut o;
            o.target = static_cast<int>(fd);
            o.dest = 0xEu;
            o.flags = true;
            o.value.w[0] = fp::sub(acc.w[0], fp::mul(s.w[1], t.w[2]));
            o.value.w[1] = fp::sub(acc.w[1], fp::mul(s.w[2], t.w[0]));
            o.value.w[2] = fp::sub(acc.w[2], fp::mul(s.w[0], t.w[1]));
            return o;
        }
        case 0x2F: return lanes(fd, dest, false, [&](int i) { return fp::min(s.w[i], t.w[i]); });
        default: throw std::runtime_error("Unsupported VU upper instruction");
        }
    }
    const unsigned index = ((w >> 6) & 31u) * 4u + (funct & 3u);
    switch (index) {
    case 0x00: case 0x01: case 0x02: case 0x03: return lanes(kAcc, dest, true, [&](int i) { return fp::add(s.w[i], b); });
    case 0x04: case 0x05: case 0x06: case 0x07: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(s.w[i], b); });
    case 0x08: case 0x09: case 0x0A: case 0x0B: return lanes(kAcc, dest, true, [&](int i) { return fp::add(acc.w[i], fp::mul(s.w[i], b)); });
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(acc.w[i], fp::mul(s.w[i], b)); });
    case 0x10: case 0x11: case 0x12: case 0x13: {
        static const int kFrac[4] = {0, 4, 12, 15};
        return lanes(ft, dest, false, [&](int i) { return fp::from_fixed(s.sw[i], kFrac[index & 3u]); });
    }
    case 0x14: case 0x15: case 0x16: case 0x17: {
        static const int kFrac[4] = {0, 4, 12, 15};
        return lanes(ft, dest, false, [&](int i) { return fp::to_fixed(s.w[i], kFrac[index & 3u]); });
    }
    case 0x18: case 0x19: case 0x1A: case 0x1B: return lanes(kAcc, dest, true, [&](int i) { return fp::mul(s.w[i], b); });
    case 0x1C: return lanes(kAcc, dest, true, [&](int i) { return fp::mul(s.w[i], r.q); });
    case 0x1D: return lanes(ft, dest, false, [&](int i) { return s.w[i] & fp::kAbs; });
    case 0x1E: return lanes(kAcc, dest, true, [&](int i) { return fp::mul(s.w[i], r.i); });
    case 0x1F: {
        UpperOut o;
        o.has_clip = true;
        o.clip = vuops::clip_value(r, s, t.w[3]);
        return o;
    }
    case 0x20: return lanes(kAcc, dest, true, [&](int i) { return fp::add(s.w[i], r.q); });
    case 0x21: return lanes(kAcc, dest, true, [&](int i) { return fp::add(acc.w[i], fp::mul(s.w[i], r.q)); });
    case 0x22: return lanes(kAcc, dest, true, [&](int i) { return fp::add(s.w[i], r.i); });
    case 0x23: return lanes(kAcc, dest, true, [&](int i) { return fp::add(acc.w[i], fp::mul(s.w[i], r.i)); });
    case 0x24: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(s.w[i], r.q); });
    case 0x25: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(acc.w[i], fp::mul(s.w[i], r.q)); });
    case 0x26: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(s.w[i], r.i); });
    case 0x27: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(acc.w[i], fp::mul(s.w[i], r.i)); });
    case 0x28: return lanes(kAcc, dest, true, [&](int i) { return fp::add(s.w[i], t.w[i]); });
    case 0x29: return lanes(kAcc, dest, true, [&](int i) { return fp::add(acc.w[i], fp::mul(s.w[i], t.w[i])); });
    case 0x2A: return lanes(kAcc, dest, true, [&](int i) { return fp::mul(s.w[i], t.w[i]); });
    case 0x2C: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(s.w[i], t.w[i]); });
    case 0x2D: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(acc.w[i], fp::mul(s.w[i], t.w[i])); });
    case 0x2E: {
        UpperOut o;
        o.target = kAcc;
        o.dest = 0xEu;
        o.flags = true;
        o.value.w[0] = fp::mul(s.w[1], t.w[2]);
        o.value.w[1] = fp::mul(s.w[2], t.w[0]);
        o.value.w[2] = fp::mul(s.w[0], t.w[1]);
        return o;
    }
    case 0x2F: return UpperOut{};
    default: throw std::runtime_error("Unsupported VU upper instruction");
    }
}

PS2_VU_INLINE void commit(Vu0Regs &r, const UpperOut &o) {
    if (o.target < 0) return;
    if (o.target > 0) {
        Reg128 &dst = o.target == kAcc ? r.acc : r.vf[o.target];
        for (int i = 0; i < 4; ++i)
            if (o.dest & (8u >> i)) dst.w[i] = o.value.w[i];
    }
    if (o.flags) set_flags(r, o.value, o.dest);
}

PS2_VU_INLINE std::uint8_t *qword(Unit &u, std::uint32_t index) {
    return u.data + (index & (u.data_qwords - 1)) * 16;
}

PS2_VU_INLINE void store_vi(Vu0Regs &r, unsigned index, std::uint32_t value) {
    if (index) r.vi[index] = static_cast<std::uint16_t>(value);
}

struct BranchView {
    struct Step {
        std::uint16_t before[16] = {};
        std::uint8_t def = 0;
    };
    Step hist[4] = {};
    std::uint16_t view[16] = {};

    // Before an instruction runs: what its branch, if it is one, reads.
    void arm(Unit &u, const Vu0Regs &r) {
        const unsigned reg = hist[0].def;
        if (reg == 0 || (reg & 0x80u)) {
            u.branch_vi = nullptr;
            return;
        }
        unsigned j = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (hist[i].def == reg) j = i;
            else break;
        }
        std::memcpy(view, r.vi, sizeof view);
        view[reg] = hist[j].before[reg];
        u.branch_vi = view;
    }
    // Keep the registers as they are before this instruction for the next one.
    void snapshot(const Vu0Regs &r, std::uint16_t (&now)[16]) const { std::memcpy(now, r.vi, sizeof now); }
    PS2_VU_INLINE void commit(const std::uint16_t (&was)[16], std::uint32_t lo, std::uint32_t hi) {
        for (int i = 3; i > 0; --i) hist[i] = hist[i - 1];
        std::memcpy(hist[0].before, was, sizeof hist[0].before);
        const unsigned reg = vi_written_reg(lo, hi);
        hist[0].def = static_cast<std::uint8_t>(reg == 0 ? 0 : branch_delay_exempt(lo) ? (0x80u | reg) : reg);
    }
};

PS2_VU_INLINE void lower(Unit &u, std::uint32_t w, std::uint32_t pc, std::uint32_t &npc, bool &branch) {
    Vu0Regs &r = *u.regs;
    const unsigned dest = (w >> 21) & 15u, it = (w >> 16) & 15u, is = (w >> 11) & 15u, id = (w >> 6) & 15u;
    const std::uint32_t ft = (w >> 16) & 31u, fs = (w >> 11) & 31u;
    const std::int32_t imm11 = static_cast<std::int32_t>(w << 21) >> 21;
    const auto clip_flags = [&]() { return u.pipe ? u.pipe->clip() : r.clip; };
    const auto status_flags = [&]() { return u.pipe ? u.pipe->status() : r.status; };
    const auto mac_flags_now = [&]() { return u.pipe ? u.pipe->mac() : r.mac; };
    const auto branch_to = [&](std::uint32_t target) {
        npc = target & u.code_mask;
        branch = true;
    };
    const auto bvi = [&](unsigned k) -> std::uint16_t { return u.branch_vi ? u.branch_vi[k] : r.vi[k]; };
    if ((w >> 25) == 0x40u) {
        const unsigned funct = w & 63u;
        if (funct < 0x3C) {
            switch (funct) {
            case 0x30: store_vi(r, id, r.vi[is] + r.vi[it]); break;
            case 0x31: store_vi(r, id, r.vi[is] - r.vi[it]); break;
            case 0x32: store_vi(r, it, r.vi[is] + static_cast<std::uint32_t>(static_cast<std::int32_t>(((w >> 6) & 31u) << 27) >> 27)); break;
            case 0x34: store_vi(r, id, r.vi[is] & r.vi[it]); break;
            case 0x35: store_vi(r, id, r.vi[is] | r.vi[it]); break;
            default:
                throw std::runtime_error("Unsupported VU lower instruction");
            }
            return;
        }
        const unsigned index = ((w >> 6) & 31u) * 4u + (funct & 3u);
        const unsigned fsf = (w >> 21) & 3u, ftf = (w >> 23) & 3u;
        switch (index) {
        case 0x30: if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].w[i] = r.vf[fs].w[i]; break;
        case 0x31: if (ft) { const Reg128 s = r.vf[fs]; for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].w[i] = s.w[(i + 1) & 3]; } break;
        case 0x34: {
            const std::uint8_t *q = qword(u, r.vi[is]);
            if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(&r.vf[ft].w[i], q + i * 4, 4);
            store_vi(r, is, r.vi[is] + 1u);
            break;
        }
        case 0x35: {
            std::uint8_t *q = qword(u, r.vi[it]);
            for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &r.vf[fs].w[i], 4);
            store_vi(r, it, r.vi[it] + 1u);
            break;
        }
        case 0x36: {
            store_vi(r, is, r.vi[is] - 1u);
            const std::uint8_t *q = qword(u, r.vi[is]);
            if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(&r.vf[ft].w[i], q + i * 4, 4);
            break;
        }
        case 0x37: {
            store_vi(r, it, r.vi[it] - 1u);
            std::uint8_t *q = qword(u, r.vi[it]);
            for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &r.vf[fs].w[i], 4);
            break;
        }
        case 0x38: {
            const std::uint32_t before = u.pipe ? u.pipe->begin_q(r) : r.q;
            vu::div(r, r.vf[fs].w[fsf], r.vf[ft].w[ftf]);
            if (u.pipe) u.pipe->end_q(r, before, 7);
            break;
        }
        case 0x39: {
            const std::uint32_t before = u.pipe ? u.pipe->begin_q(r) : r.q;
            vu::sqrt(r, r.vf[ft].w[ftf]);
            if (u.pipe) u.pipe->end_q(r, before, 7);
            break;
        }
        case 0x3A: {
            const std::uint32_t before = u.pipe ? u.pipe->begin_q(r) : r.q;
            vu::rsqrt(r, r.vf[fs].w[fsf], r.vf[ft].w[ftf]);
            if (u.pipe) u.pipe->end_q(r, before, 13);
            break;
        }
        case 0x3B: if (u.pipe) u.pipe->flush_q(r); break;
        case 0x3C: store_vi(r, it, r.vf[fs].w[fsf]); break;
        case 0x3D: if (ft) { const std::uint32_t v = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(r.vi[is]))); for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].w[i] = v; } break;
        case 0x3E: {
            const std::uint8_t *q = qword(u, r.vi[is]);
            for (int i = 0; i < 4; ++i)
                if (dest & (8u >> i)) {
                    std::uint16_t v;
                    std::memcpy(&v, q + i * 4, 2);
                    store_vi(r, it, v);
                    break;
                }
            break;
        }
        case 0x3F: {
            std::uint8_t *q = qword(u, r.vi[is]);
            const std::uint32_t v = r.vi[it];
            for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &v, 4);
            break;
        }
        case 0x40:
            r.r = vu::rand_next(r.r);
            if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].w[i] = r.r | fp::kOne;
            break;
        case 0x41: if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].w[i] = (r.r & 0x7FFFFFu) | fp::kOne; break;
        case 0x42: r.r = r.vf[fs].w[fsf] & 0x7FFFFFu; break;
        case 0x43: r.r = (r.r ^ r.vf[fs].w[fsf]) & 0x7FFFFFu; break;
        case 0x64: if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].w[i] = r.p; break;
        // g_top and g_itop are VU1's, set by the VIF1 on the thread that runs VU1; VU0 (run from the EE thread) has no TOP and its own
        // ITOP in the VIF0. Sharing them lost VU1 its TOP whenever the VIF0 started a program, and gave VU0 whatever VU1 had latched.
        case 0x68: store_vi(r, it, u.number ? u.top : 0u); break;
        case 0x69: store_vi(r, it, u.itop); break;
        case 0x6C: ps2::vu::kick(u.number,r.vi[is]); break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x78: case 0x79: case 0x7A: case 0x7C: case 0x7D: case 0x7E:
            vuops::efu(r, index, fs, fsf);
            break;
        case 0x7B: break;
        default:
            throw std::runtime_error("Unsupported VU lower instruction");
        }
        return;
    }
    const std::uint32_t target = pc + 8u + static_cast<std::uint32_t>(imm11) * 8u;
    switch (w >> 25) {
    case 0x00: {
        const std::uint8_t *q = qword(u, r.vi[is] + static_cast<std::uint32_t>(imm11));
        if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(&r.vf[ft].w[i], q + i * 4, 4);
        break;
    }
    case 0x01: {
        std::uint8_t *q = qword(u, r.vi[it] + static_cast<std::uint32_t>(imm11));
        for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &r.vf[fs].w[i], 4);
        break;
    }
    case 0x04: {
        const std::uint8_t *q = qword(u, r.vi[is] + static_cast<std::uint32_t>(imm11));
        for (int i = 0; i < 4; ++i)
            if (dest & (8u >> i)) {
                std::uint16_t v;
                std::memcpy(&v, q + i * 4, 2);
                store_vi(r, it, v);
                break;
            }
        break;
    }
    case 0x05: {
        std::uint8_t *q = qword(u, r.vi[is] + static_cast<std::uint32_t>(imm11));
        const std::uint32_t v = r.vi[it];
        for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &v, 4);
        break;
    }
    case 0x08: store_vi(r, it, r.vi[is] + (((w >> 10) & 0x7800u) | (w & 0x7FFu))); break;
    case 0x09: store_vi(r, it, r.vi[is] - (((w >> 10) & 0x7800u) | (w & 0x7FFu))); break;
    case 0x10: store_vi(r, 1, (clip_flags() & 0xFFFFFFu) == (w & 0xFFFFFFu) ? 1u : 0u); break;
    case 0x11:
        r.clip = w & 0xFFFFFFu;
        if (u.pipe) u.pipe->set_clip(r.clip, u.pipe->t - 4u);
        break;
    case 0x12: store_vi(r, 1, (clip_flags() & w & 0xFFFFFFu) ? 1u : 0u); break;
    case 0x13: store_vi(r, 1, ((clip_flags() | w) & 0xFFFFFFu) == 0xFFFFFFu ? 1u : 0u); break;
    case 0x14: store_vi(r, it, (status_flags() & 0xFFFu) == ((((w >> 10) & 0x800u) | (w & 0x7FFu))) ? 1u : 0u); break;
    case 0x15:
        if (u.pipe) {
            u.fsset = static_cast<std::int32_t>((((w >> 10) & 0x800u) | (w & 0x7FFu)) & 0xFC0u);
            break;
        }
        r.status = (status_flags() & 0x3Fu) | ((((w >> 10) & 0x800u) | (w & 0x7FFu)) & 0xFC0u);
        break;
    case 0x16: store_vi(r, it, status_flags() & (((w >> 10) & 0x800u) | (w & 0x7FFu))); break;
    case 0x17: store_vi(r, it, (status_flags() & 0xFFFu) | (((w >> 10) & 0x800u) | (w & 0x7FFu))); break;
    case 0x18: store_vi(r, it, (mac_flags_now() & 0xFFFFu) == r.vi[is] ? 1u : 0u); break;
    case 0x1A: store_vi(r, it, mac_flags_now() & r.vi[is]); break;
    case 0x1B: store_vi(r, it, (mac_flags_now() & 0xFFFFu) | r.vi[is]); break;
    case 0x1C: store_vi(r, it, clip_flags() & 0xFFFu); break;
    case 0x20: branch_to(target); break;
    case 0x21: store_vi(r, it, (pc + 16u) / 8u); branch_to(target); break;
    case 0x24: branch_to(static_cast<std::uint32_t>(r.vi[is]) * 8u); break;
    case 0x25: { const std::uint32_t t = static_cast<std::uint32_t>(r.vi[is]) * 8u; store_vi(r, it, (pc + 16u) / 8u); branch_to(t); break; }
    case 0x28: if (bvi(is) == bvi(it)) branch_to(target); break;
    case 0x29: if (bvi(is) != bvi(it)) branch_to(target); break;
    case 0x2C: if (static_cast<std::int16_t>(bvi(is)) < 0) branch_to(target); break;
    case 0x2D: if (static_cast<std::int16_t>(bvi(is)) > 0) branch_to(target); break;
    case 0x2E: if (static_cast<std::int16_t>(bvi(is)) <= 0) branch_to(target); break;
    case 0x2F: if (static_cast<std::int16_t>(bvi(is)) >= 0) branch_to(target); break;
    default:
        throw std::runtime_error("Unsupported VU lower instruction");
    }
}

}
