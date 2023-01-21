#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _buffer(), _capacity(capacity), _size(0), _nwritten(0), _nread(0), _input_ended(false) {}

size_t ByteStream::write(const string &data) {
    // 获取管道剩余容量
    size_t remain_capacity = remaining_capacity();

    // 若data的大小大于剩余容量，将其截断
    string data_to_write = data.substr(0, remain_capacity);

    // 将data写入管道
    for (auto chr : data_to_write) {
        _buffer.emplace_back(chr);
    }

    // 累加管道数据量以及总的写进去的字符数
    _nwritten += data_to_write.size();
    _size += data_to_write.size();

    // 返回该次写进去的字符数
    return data_to_write.size();
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    // 获取管道中len个字符（len如果大于size的话，取size个）
    return string(_buffer.begin(), _buffer.begin() + min(_size, len));
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    // 将管道前len个字符读出，并删掉
    size_t len_to_pop = min(_size, len);
    for (size_t i = 0; i < len_to_pop; ++i) {
        _buffer.pop_front();
    }
    // 累加总的读取量
    _nread += len_to_pop;
    // 管道数据量减小
    _size -= len_to_pop;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
// 从管道中读取前len个字符
std::string ByteStream::read(const size_t len) {
    // 先获取前len个字符组成的string
    const auto ret = peek_output(len);
    // 再把len个字符删掉
    pop_output(len);
    // 返回string
    return ret;
}

// 当writer已经完成输入，调用该函数
void ByteStream::end_input() { _input_ended = true; }

// 判断writer是否已经完成输入
bool ByteStream::input_ended() const { return _input_ended; }

// 返回当前管道的数据量
size_t ByteStream::buffer_size() const { return _size; }

// 判断管道是否为空
bool ByteStream::buffer_empty() const { return _size == 0; }

// reader用于判断是否已经读完全部数据（要保证writer已经完成输入，否则即使为空，后面管道中仍有数据输入）
bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

// 返回总的写数据量
size_t ByteStream::bytes_written() const { return _nwritten; }

// 返回总的读数据量
size_t ByteStream::bytes_read() const { return _nread; }

// 返回剩余容量
size_t ByteStream::remaining_capacity() const { return _capacity - _size; }
