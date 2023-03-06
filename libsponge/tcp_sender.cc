#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>
// #include <iostream>
#include <algorithm>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()})), _initial_retransmission_timeout{retx_timeout}, _stream(capacity), _rto{retx_timeout} {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

// 该函数的作用就是向receiver发送seg，填receiver的window
void TCPSender::fill_window()
{
    // 如果syn没有发送，则发送第一个数据包，然后return
    if (!_syn_sent)
    {
        _syn_sent = true;
        TCPSegment seg;
        seg.header().syn = true;
        _send_segment(seg);
        return;
    }
    // 如果syn已经发送，但还没有被确认，即outstanding的队头是syn数据包，直接返回
    if (!_segments_outstanding.empty() && _segments_outstanding.front().header().syn)
        return;

    // 如果此时sender读数据的_stream已经空了，但_stream后续还有数据输入，则返回等待
    if (!_stream.buffer_size() && !_stream.eof())
        return;
    // 如果已经发送了最有一个数据包fin，则直接返回
    if (_fin_sent)
        return;

    // 如果receiver的window_size大于0，说明其bystream还有空间
    if (_receiver_window_size)
    {
        // 只要receiver的可用空间不为0，就发seg过去
        while (_receiver_free_space)
        {
            TCPSegment seg;
            // 发过去的seg大小，要在sender剩下数据量，seg的最大承载量，以及receiver可用空间去取一个最小值
            size_t payload_size = min({_stream.buffer_size(),
                                       static_cast<size_t>(_receiver_free_space),
                                       static_cast<size_t>(TCPConfig::MAX_PAYLOAD_SIZE)});
            seg.payload() = _stream.read(payload_size);

            // 如果后面不会再有数据输入到_stream，并且当前这一整段数据receiver可以全部存下,否则就算发过去
            // 也会把数据截断，后面还要重发被截断的部分，那当前段就不是fin了
            //_receiver_free_space 要大于 payload_size，不能等于，因为fin也算一个字节
            // 整个seg大小为payload_size+1
            if (_stream.eof() && static_cast<size_t>(_receiver_free_space) > payload_size)
            {
                seg.header().fin = true;
                _fin_sent = true;
            }
            // 将seg发过去
            _send_segment(seg);
            // 如果_stream为空，此时无数据可发，break
            if (_stream.buffer_empty())
                break;
        }
    }
    else if (_receiver_free_space == 0)
    {
        // 会有一个测试数据用于测试window_size是否为0？，此时可用空间也为0（有可能在_send_segment中减成负的），
        // 如果是fin的话则返回不带数据的fin即可，否则如果_stream有数据，返回一个字节回去,发送完后_receiver_free_space
        // 减成-1
        TCPSegment seg;
        if (_stream.eof())
        {
            seg.header().fin = true;
            _fin_sent = true;
            _send_segment(seg);
        }
        else if (!_stream.buffer_empty())
        {
            seg.payload() = _stream.read(1);
            _send_segment(seg);
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size)
{

    // sender收到receiver返回的ack和window_size

    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    // ack不合法直接返回
    if (!_ack_valid(abs_ackno))
    {
        return;
    }

    // 将receiver的window_size记下来
    _receiver_window_size = window_size;
    // 可用空间先初始化为window_size
    _receiver_free_space = window_size;

    // 只要存在已经发送出去但没有收到确认的字符
    while (!_segments_outstanding.empty())
    {
        // 每次获取队头seg
        TCPSegment seg = _segments_outstanding.front();

        // 如果这个seg对应的最后一个字符的编号<=ack，说明该seg已经被全部接受（ack是receiver接受到的所有字符的最后
        // 一个编号）
        if (unwrap(seg.header().seqno, _isn, _next_seqno) + seg.length_in_sequence_space() <= abs_ackno)
        {
            // 此时未确认的数据减少了一个seg
            _bytes_in_flight -= seg.length_in_sequence_space();
            // 将seg弹出
            _segments_outstanding.pop();

            // ps:重传计时是针对outstanding队列的队头seg的，即已经发送出去但未收到确认的最老数据
            // 如果队头已确认并且弹出，此时计时器重置
            _time_elapsed = 0;                      // 时间从0开始累加
            _rto = _initial_retransmission_timeout; // rto回到初始值（对于同一个seg，每重传一次就要翻倍）
            _consecutive_retransmissions = 0;       // 下个seg的连续重传次数归0
        }
        // 如果队头seg没有被确认，则break（后面的seg也不会被确认，编号是递增的）
        else
        {
            break;
        }
    }

    // 若收到ack后，处理完所有已被确认的seg，还有剩下的未确认的seg
    if (!_segments_outstanding.empty())
    {
        // 则重新计算receiver的可用空间（此时sender会按最坏情况计算receiver的剩余空间）
        // 若outstanding为空，说明receiver的bystream中的全部数据都已被确认，空用空间就是window_size，无需重新
        // 计算

        // abs_ackno+window_size=receiver的bytestream的右边界
        // outstanding的队头seg的编号+已发送但没有收到确认的字节数=receiver的bytestream中的数据最大可能右边界
        // 相减为最小可用空间
        _receiver_free_space = static_cast<uint16_t>(
            abs_ackno + static_cast<uint64_t>(window_size) -
            unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno) - _bytes_in_flight);
    }

    // 若全部字符都已经被确认，则关闭重传计时器
    if (!_bytes_in_flight)
        _timer_running = false;

    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
// sender会定期调用该函数，累加 _time_elapsed，若超过了重传时间，则重传消息
void TCPSender::tick(const size_t ms_since_last_tick)
{
    // 如果重传计时器没有开启，则不管
    if (!_timer_running)
        return;

    // 累加时间
    _time_elapsed += ms_since_last_tick;

    // 若超过了重传时间
    if (_time_elapsed >= _rto)
    {
        // 重传最老的没有收到确认的消息
        _segments_out.push(_segments_outstanding.front());

        // 重传时保证receiver的window_size>0即有位置存放消息，或者重传第一个消息（一开始window_size初始化为0）
        if (_receiver_window_size || _segments_outstanding.front().header().syn)
        {
            // 累计连续重传次数
            ++_consecutive_retransmissions;
            // 每连续重传一次,rto翻倍，防止重传的太频繁，导致网络拥塞
            _rto <<= 1;
        }
        // 重传后，时间归0，重新累加
        _time_elapsed = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

// 发送一个空的seg
void TCPSender::send_empty_segment()
{
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}

// 判断ack是否合法
bool TCPSender::_ack_valid(uint64_t abs_ackno)
{
    // ack不能太大，不能超过还没发送的字节的编号
    // 不能太小，不能小于已经发送的还没有确认的字节的最小编号，因为该编号之前的编号一定已经被确认过了，
    return abs_ackno <= _next_seqno &&
           abs_ackno >= unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno);
}

// 发送seg
void TCPSender::_send_segment(TCPSegment &seg)
{
    // seg编号赋值
    seg.header().seqno = wrap(_next_seqno, _isn);
    // 更新还没发送的数据的第一个字节的编号
    _next_seqno += seg.length_in_sequence_space();
    // 累加已经发送但还没有收到确认的字节
    _bytes_in_flight += seg.length_in_sequence_space();
    // 更新receiver的可用空间
    if (_syn_sent)
        _receiver_free_space -= seg.length_in_sequence_space();
    // 将seg加到out和outstanding里面
    _segments_out.push(seg);
    _segments_outstanding.push(seg);
    // 如果重传计时器没有开启（没有被某个seg占用），开启该seg对应的重传计时器
    if (!_timer_running)
    {
        _timer_running = true;
        _time_elapsed = 0;
    }
}
