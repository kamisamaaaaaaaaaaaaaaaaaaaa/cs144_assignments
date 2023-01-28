#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender
{
private:
  //! our initial sequence number, the number for our SYN.
  WrappingInt32 _isn;

  //! outbound queue of segments that the TCPSender wants sent
  // 存放所有sender要发送的seg
  std::queue<TCPSegment> _segments_out{};

  //! retransmission timer for the connection
  // 初始化的rto(超过多少时间没收到ack重传)
  unsigned int _initial_retransmission_timeout;

  //! outgoing stream of bytes that have not yet been sent
  // sender会从_stream里面读取要发送的消息然后发出去
  ByteStream _stream;

  //! the (absolute) sequence number for the next byte to be sent
  // 还没发送的数据中的第一个字符的编号
  uint64_t _next_seqno{0};

  // 是否已发送syn
  bool _syn_sent = false;
  // 是否已发送fin
  bool _fin_sent = false;
  // 已经发送出去但还没有收到ack的字符数
  uint64_t _bytes_in_flight = 0;
  // receiver的windown_size（reciver的bytestream的cap-remain_cap）
  uint16_t _receiver_window_size = 0;

  // receiver的最小可用空间，当收到receiver的ack和window_size时有以下几种情况：
  // 1、该ack确认了所有发送过去的数据段，此时可用空间就是receiver的bytestream的剩余空间，即window_size
  // 2、该ack只确认了部分发送过去的数据段，此时又分为两种情况：
  // （1）sender发过去的数据全部接收到了receiver的bytestream里但没有确认
  // （2）sender发过去的数据部分接受到了receiver的bytestream里但没有确认
  // （3）sender发过去的数据没有接受到了receiver的bytestream
  // 这三种情况中(1)的receiver剩余空间最小，由于sender不知道是哪种情况，所有要按照最坏情况考虑，此时最小空用空间
  // 为(1)的剩余空间
  uint16_t _receiver_free_space = 0;

  // 连续的重传次数
  uint16_t _consecutive_retransmissions = 0;
  // 超过多少时间没收到ack重传
  unsigned int _rto = 0;
  // 自上次重传完后，过了多久
  unsigned int _time_elapsed = 0;
  // 重传计时器是否运作
  bool _timer_running = false;
  // 用于存放已经发出去的，但没有收到确认的数据
  std::queue<TCPSegment> _segments_outstanding{};

  // 判断收到的ack编号是否合法
  bool _ack_valid(uint64_t abs_ackno);
  // 将seg发出去
  void _send_segment(TCPSegment &seg);

public:
  //! Initialize a TCPSender
  TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
            const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
            const std::optional<WrappingInt32> fixed_isn = {});

  //! \name "Input" interface for the writer
  //!@{
  ByteStream &stream_in() { return _stream; }
  const ByteStream &stream_in() const { return _stream; }
  //!@}

  //! \name Methods that can cause the TCPSender to send a segment
  //!@{

  //! \brief A new acknowledgment was received
  void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

  //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
  void send_empty_segment();

  //! \brief create and send segments to fill as much of the window as possible
  void fill_window();

  //! \brief Notifies the TCPSender of the passage of time
  void tick(const size_t ms_since_last_tick);
  //!@}

  //! \name Accessors
  //!@{

  //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
  //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
  //! (see TCPSegment::length_in_sequence_space())
  size_t bytes_in_flight() const;

  //! \brief Number of consecutive retransmissions that have occurred in a row
  unsigned int consecutive_retransmissions() const;

  //! \brief TCPSegments that the TCPSender has enqueued for transmission.
  //! \note These must be dequeued and sent by the TCPConnection,
  //! which will need to fill in the fields that are set by the TCPReceiver
  //! (ackno and window size) before sending.
  std::queue<TCPSegment> &segments_out() { return _segments_out; }
  //!@}

  //! \name What is the next sequence number? (used for testing)
  //!@{

  //! \brief absolute seqno for the next byte to be sent
  uint64_t next_seqno_absolute() const { return _next_seqno; }

  //! \brief relative seqno for the next byte to be sent
  WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
  //!@}
};

#endif // SPONGE_LIBSPONGE_TCP_SENDER_HH
