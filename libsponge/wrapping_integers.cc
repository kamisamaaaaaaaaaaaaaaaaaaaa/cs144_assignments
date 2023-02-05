#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn)
{
    // seqno = (abs_seqno+isn) % 2^32
    //       = abs_seqno % 2^32 + isn
    //       = isn + uint32_t(abs_seqno)
    // isn+n会调用isn的+号重载，第二个参数类型为uint32，会自动将传入的abs_seqno(uint64_t)转成uint32_t
    // 此时相当于对2^32取模，只要溢出从0开始
    return isn + n;
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint)
{
    WrappingInt32 wrap_checkpoint = wrap(checkpoint, isn);
    uint32_t diff = n - wrap_checkpoint;
    if (diff & 0x80000000 && diff + checkpoint <= UINT32_MAX)
    {
        return checkpoint + diff;
    }
    return checkpoint + static_cast<int32_t>(diff);
}

