#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

// sender的stream用来存放准备发送出去的数据，不断从stream中读数据，存到sender的发送队列_segments_out中
size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

// 返回sender已经发出去但还没有收到对方确认的数据量
size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

// 返回receiver收到的乱序的数据中，还没有排好序的字节数(resembler还没放到bytestream中的字节数)
size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

bool TCPConnection::active() const { return _active; }

// 收到另一个endpoint传来的seg
void TCPConnection::segment_received(const TCPSegment &seg)
{
    // 如果连接已断开，传来任何seg都不管
    if (!_active)
        return;

    // 新来了一个seg，到该seg已过了时间0
    _time_since_last_segment_received = 0;

    // STATE:CLOSED（针对server）
    //  一开始两个endpoint处于closed状态
    //  1、receiver的ackno会由于isn没有赋值而返回空（isn只有当对方发送第一个seg来时，才会
    //  将第一个seg的seqno作为isn赋值）
    //  2、sender由于还没有发送过任何数据，下一段发送的数据的第一个字节的编号应该为0
    if (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0)
    {
        // 此时对方发来的除了syn数据包，其它全部忽略
        if (!seg.header().syn)
            return;
        // 将syn数据包给receiver
        _receiver.segment_received(seg);
        connect();
        return;
    }

    // STATE:SYN_SENT（针对client）
    //  当前endpoint已经发送了第一个数据包syn，但还没有收到对方的任何数据包
    //  由于sender已经发送了一个数据包，所以下一个发送的字节编号一定大于0；
    //  由于刚发送了第一个数据包，没有这么快收到这个数据包的ack，所以还没有确认的字节数等于数据包的字节数（或者下个字节的编号）；
    // 并且此时没有收到对方任何的数据包
    if (_sender.next_seqno_absolute() > 0 && _sender.bytes_in_flight() == _sender.next_seqno_absolute() &&
        !_receiver.ackno().has_value())
    {
        // 当处于SYN_SENT状态时，此时只期待收到另一个endpoint(server)的syn

        // syn包不应该带任何数据，发现该包带数据则不是syn，全部忽略
        if (seg.payload().size())
            return;

        // 不带数据的包有可能是ack包，要判断一下
        if (!seg.header().ack)
        {
            // 如果是syn包，将这个包交给receiver处理（交给resembler排序后放到bystream）
            // 同时让sender发送一个空seg
            if (seg.header().syn)
            {
                // simultaneous open
                _receiver.segment_received(seg);
                _sender.send_empty_segment();
            }
            return;
        }

        // 如果reset标志位为真，对方要求重新建立连接，则将当前连接取消
        // receiver和sender的stream都set_error
        if (seg.header().rst)
        {
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _active = false;
            return;
        }
    }

    // 若此时连接已经建立，发送了syn过去，也收到了对方的syn，则按下方逻辑处理seg

    _receiver.segment_received(seg); // 将seg交给receiver

    // 然后将ackno和对方的window_size交给sender处理（根据ackno将已经确认的seg从outstanding队列中pop出来
    // 重设计时器，然后调用fill_window，从sender的bystream中读取准备发送的消息放到out队列中）

    _sender.ack_received(seg.header().ackno, seg.header().win);

    // 如果sender的stream里面没数据了，并且对方发的seg不是空seg，此时特殊处理发送一个空seg过去
    // 如果stream里面没有数据,fill_window不会发送任何数据，会直接return ，所以要单独处理
    if (_sender.stream_in().buffer_empty() && seg.length_in_sequence_space())
        _sender.send_empty_segment();

    // 如果seg的reset为true，则发送一个空seg过去，并且将连接关闭（此时是unclean关闭）
    if (seg.header().rst)
    {
        _sender.send_empty_segment();
        unclean_shutdown();
        return;
    }

    // 最后处理sender的out队列中存放的消息，放到connection的消息队列中
    send_sender_segments();
}

// 应用层往sender的stream写data
size_t TCPConnection::write(const string &data)
{
    // data为空不管
    if (!data.size())
        return 0;
    // 将data写入sender的stream
    size_t write_size = _sender.stream_in().write(data);

    // 写完之后，fill_window将seg加入sender的out队列，然后再 send_sender_segments
    // 加入connection的out队列
    _sender.fill_window();
    send_sender_segments();
    return write_size;
}

// 定时调用该函数，用于计时
//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick)
{
    // 连接断开不管
    if (!_active)
        return;
    // 累加距离最后一个收到的seg过去了的时间
    _time_since_last_segment_received += ms_since_last_tick;

    // 调用sender的tick，也会累加时间，超过一段时间后会重传
    _sender.tick(ms_since_last_tick);

    // 如果重传次数过多，关闭连接
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS)
        unclean_shutdown();

    // 每次调用该函数，也会发消息
    send_sender_segments();
}

void TCPConnection::end_input_stream()
{
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_sender_segments();
}

// 收到client的syn数据包后，此时可以让server的sender发东西了（重点是从stream中获取syn数据包发过去对方从而建立连接）
void TCPConnection::connect()
{
    _sender.fill_window();
    send_sender_segments();
}

// 析构函数
TCPConnection::~TCPConnection()
{
    try
    {
        if (active())
        {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // 析构前，要将消息全部发完
            _sender.send_empty_segment();
            unclean_shutdown();
        }
    }
    catch (const exception &e)
    {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

// 该函数会处理sender中的segments_out队列中的消息
void TCPConnection::send_sender_segments()
{
    TCPSegment seg;
    while (!_sender.segments_out().empty())
    {
        // 获取队列中的消息
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();

        // 如果此时已经收到对方syn(isn被赋值)，即对方请求连接，才处理消息
        if (_receiver.ackno().has_value())
        {
            // 将seg添加上ackno和receiver的window_size
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        // 加入connection的消息队列
        _segments_out.push(seg);
    }
    clean_shutdown();
}

void TCPConnection::unclean_shutdown()
{
    // When this being called, _sender.stream_out() should not be empty.

    // 1、将receiver和sender的stream设成error
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();

    // 2、active标志位设为false
    _active = false;

    // 3、此时还要再发一个seg过去，并且把rst标志位设为true
    TCPSegment seg = _sender.segments_out().front();
    _sender.segments_out().pop();
    seg.header().ack = true;
    if (_receiver.ackno().has_value())
        seg.header().ackno = _receiver.ackno().value();
    seg.header().win = _receiver.window_size();
    seg.header().rst = true;

    _segments_out.push(seg);
}

void TCPConnection::clean_shutdown()
{
    // 该函数当且仅当对方后续没有消息再发过来，才会处理，即receiver的stream不会再有消息输入
    if (_receiver.stream_out().input_ended())
    {
        // 此时如果sender还有东西要发（stream还有后续输入或者里面还有东西）
        // 那么说明这个endpoint是后断开连接的，后断开连接的一方最后发送完之后无需等待
        //(_linger_after_streams_finish = false),在发送完最后一个消息fin之后，对方会发ack，此时可以立刻断开
        // ps:先断开的一方在发送完对fin的ack后，要等待一段时间，防止ack丢失，对方fin重发
        if (!_sender.stream_in().eof())
            _linger_after_streams_finish = false;

        // 如果sender已经将所有数据发完，并且全都被ack了（包括最后一个fin）
        // 此时断开连接（先断开的一方需要等待一定时间）
        else if (_sender.bytes_in_flight() == 0)
        {
            if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout)
            {
                _active = false;
            }
        }
    }
}
