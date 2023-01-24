#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg)
{
    // old_abs_acko为下一个顺位的数据段的第一个字符的abs_seqno
    // ps:整个数据段用 syn char1 char2 char3 ... fin来表示，字符的abs_seqno从0开始，即syn的abs_seqno=0
    // 因此一开始什么数据都没得到时对应old_abs_acko=0（希望得到从syn开始的第一段）
    uint64_t old_abs_ackno = 0;
    if (isn.has_value())
    {
        // 1、如果后面的数据段到达，第一个数据段没来的话，isn不会被赋值，为空，此时该判断不会进入，
        // old_abs_ackno就是初始化的0，表示会等待第一个数据段
        // 2、如果第一个数据段已经来了，isn已经被赋值，此时会重新计算old_abs_ackno，因为上一个到达的数据段
        // 可能会更新old_abs_ackno（上一个数据段是上上个的顺位）
        old_abs_ackno = abs_ackno();
    }

    if (seg.header().syn && !isn.has_value())
    {
        // 如果是第一个数据段的话，将其所带的seqno（syn的seqno）作为isn
        //! isn.has_value()不加也可以，因为第一个数据包来之前isn本身为空
        isn = make_optional<WrappingInt32>(seg.header().seqno);
    }

    if (!isn.has_value())
    {
        // 若isn为空即第一个数据包没来，则后面数据包只要来了就丢掉
        return false;
    }

    // 将bytestream中已经存放的顺序数据量作为checkpoint用于seqno到abs_seqno的转化
    uint64_t checkpoint = _reassembler.stream_out()
                              .bytes_written(); // bytes_written is a little bit small, but good enough for checkpoint
    // 计算当前数据包的abs_seqno
    uint64_t abs_seq = unwrap(seg.header().seqno, isn.value(), checkpoint);
    // 获取window大小（bytestream的剩余容量）
    uint64_t old_window_size = window_size();

    // 处理ACK数据包，全部数据发送完之后，还会紧接着发一个ACK确认帧，该数据包的编号为fin对应的编号+1
    // 也有可能比fin对应的编号+1大（发了多个），并且数据包的内容为空，此时收到确认帧返回真即可（表示收到了ACK）
    // ps:fin_abs_seq不是fin对应的abs_seqno，而是fin的下一个位置对应的abs_seqno
    // 在测试样例中，不加这个条件判断也可以过
    if (fin_abs_seq && abs_seq >= fin_abs_seq && seg.length_in_sequence_space() == 0)
    {
        return true;
    }

    // 流量控制+防止旧数据重发
    // 1、流量控制：bystream中存放的连续一段的最后一个字符的编号为old_abs_ackno，剩余容量为old_window_size，则可以装下的
    // 最后一个字符的编号应该为old_abs_ackno+old_window_size-1,所有编号比这个大的字符即使发过来也没地方放，直接返回false
    // 告诉发送方不要再发了。流量控制其实就是通过bystream的有限容量实现的，因为容量有限，可放进去的最后的一个字节编号有
    // 上界，超过这个上界的字节一律不接受，并告诉发送方不要发这么快，前面编号的字节还没存取完。
    // 2、防止旧数据重发：发过来的数据包的最后一个字符的编号应该为：数据包第一个字符的编号（abs_seq）+ 数据字符数 - 1，
    // 若最后一个字符的编号<=bystream已经接受到的连续一段字符的最大编号，那说明整段数据应该是bystream已经接受的数据的
    // 一部分，此时无需重发直接返回false
    if (!(abs_seq < old_abs_ackno + old_window_size && abs_seq + seg.length_in_sequence_space() > old_abs_ackno))
    {
        // 有一种情况例外，也就是ACK确认帧，流量控制要求收到的字符编号不能越界，但确认帧的编号是恰好月结的(fin的下一个位置)
        // 此时特殊处理，返回true，代表已收到确认帧
        return seg.length_in_sequence_space() == 0 && abs_seq == old_abs_ackno;
    }

    // 判断当前收到的数据包有没有越过右界（bystream可存放的最大编号），即数据左边界编号<=bystream可存放的最大编号
    // 此时存在可放入bystream的一部分
    bool all_fill = abs_seq + seg.length_in_sequence_space() <= old_abs_ackno + old_window_size;
    // 上述变量用于保证当最后一个数据段到达，并且当前该数据段可放入bystream时，才记录整个数据段的最大编号（的下一个位置）
    // 其实为什么要加all_fill呢，是为了防止到达的fin数据段属于其它的完整数据嘛
    if (all_fill && seg.header().fin)
    { // only when fin also fall in the window
        fin_abs_seq = abs_seq + seg.length_in_sequence_space();
    }

    // 1、由于对于数据的第一个字符来说abs_seq是从1开始的（syn char1 char2 ... 对应 0 1 2 ...，char1从1开始）
    // 而对于reassembler来说数据第一个字符的序号是0(char1从0开始)，因此传入reassembler之前序号全部减1。如
    //(char1,char2)送进去时编号为0
    // 2、如果是第一个数据段(包含syn)则不用处理，直接用其0编号即可，符合要求。即(syn char1 char2)到达会把(char1,char2)
    // 送到reassembler，编号为0
    uint64_t stream_indices = abs_seq > 0 ? abs_seq - 1 : 0;
    std::string payload(seg.payload().copy());
    // 如何判断当前数据段是否为最后一个，只需判断fin_abs_seq = abs_seq + seg.length_in_sequence_space()即可
    // stream_indices + seg.payload().size() + 2和abs_seq + seg.length_in_sequence_space()是等价的
    // 因为abs_seq=stream_indices+1，seg.length_in_sequence_space()=seg.payload().size()+1(fin的话要加1)
    // 因此abs_seq + seg.length_in_sequence_space()=stream_indices + seg.payload().size() + 2
    _reassembler.push_substring(payload, stream_indices, stream_indices + seg.payload().size() + 2 == fin_abs_seq);

    return true;
}

// 计算bystream中希望获得的下个字节的编号（前面连续一段的最后一个字节的下个位置的编号）
uint64_t TCPReceiver::abs_ackno() const
{

    // 若已经写了char1 char2 char3到bytestream（编号对应为1 2 3），那么下个要获取的字符编号为char4（4）
    // 即bytes_written()+1=3+1=4
    uint64_t abs_ackno_without_fin = 1 + _reassembler.stream_out().bytes_written();

    // 边界情况，如syn char1 char2 char3 fin(0 1 2 3 4)，那么如果char1 char2 char3已经写进去了，所有数据已经写完
    // 则此时期待的下个位置的编号应该是fin下个字符的编号，abs_ackno_without_fin得到的只是fin的编号（1+3=4），因此要+1
    if (abs_ackno_without_fin + 1 == fin_abs_seq)
    {
        return fin_abs_seq;
    }
    return abs_ackno_without_fin;
}

optional<WrappingInt32> TCPReceiver::ackno() const
{
    if (!isn.has_value())
    {
        return nullopt;
    }
    return make_optional<WrappingInt32>(wrap(abs_ackno(), isn.value()));
}

size_t TCPReceiver::window_size() const { return _reassembler.stream_out().remaining_capacity(); }
