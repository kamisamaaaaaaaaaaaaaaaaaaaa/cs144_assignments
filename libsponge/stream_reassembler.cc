#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), buffer(capacity, 0), bit_map(capacity, 0), rpos(0), _unassembled_bytes(0), eof_index(-1) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof)
{
    // 传过来的data可能有旧的部分（左端小于rpos）也可能有越界的一部分（右端超过滑窗右端即rpos+_capacity）
    size_t start_index = max(rpos, index);
    size_t end_index = min(rpos + _capacity, index + data.size());

    // 将传过来的data按index放到滑动窗口buffer
    for (size_t i = start_index; i < end_index; i++)
    {
        // 由于用循环队列的形式实现滑动窗口，i对应滑动窗口的坐标为i%_capacity
        const size_t bi = i % _capacity;
        // 将data赋值到对应位置，buffer的某些位置可能已经被赋值过，即传来的data有重复部分
        // 但没有关系，数组重复赋值没有影响，不过由于已经赋值过bit_map[bi] = 1，下面if判断不会
        // 通过，不会累加_unassembled_bytes，符合逻辑
        buffer[bi] = data[i - index];
        if (bit_map[bi] == 0)
        {
            bit_map[bi] = 1;
            _unassembled_bytes++;
        }
    }

    // 获取滑动窗口前面连续一段已经排好序的数据的最右端
    size_t avail_end = rpos;
    while (bit_map[avail_end % _capacity] && avail_end < rpos + _capacity)
        avail_end++;

    // avail_end大于滑动窗口左端，说明存在紧接着的排好序的数据
    if (avail_end > rpos)
    {
        // 将这段数据搞出来，长度为avail_end - rpos
        string data_submit(avail_end - rpos, 0);
        for (size_t i = rpos; i < avail_end; i++)
        {
            data_submit[i - rpos] = buffer[i % _capacity];
        }
        // 将这段数据写到output中，注意有可能output空间不够，不能全部写进去
        // 此时只会写一部分，并且返回写了多少字节
        size_t written_bytes = _output.write(data_submit);
        for (size_t i = rpos; i < rpos + written_bytes; i++)
        {
            // 将滑动窗口已经输出的前面的部分标记为置0
            bit_map[i % _capacity] = 0;
        }
        // 更新滑动窗口的左端
        rpos += written_bytes;
        _unassembled_bytes -= written_bytes;
    }

    // 这里的eof_index不能初始化为0，否则以下两者情况会出错：
    // 1、第一组来的是一个空串，index=0，且不是eof，此时前面的语句全部不会执行，不会往output里写任何东西
    // 下面的if(eof)不会进去，但是最后一个判断会执行，因为此时bytes_written()=0，eof_index等于初始化的0
    // 然后直接结束
    // 2、第一组来的index为非0，且不是eof，此时没有凑够从滑窗左端开始的连续的一段，不会往output写任何东西
    // bytes_written()还是0，等于eof_index，然后结束
    // 解决方法：eof_index初始化为非0的数即可，可初始化为-1
    if (eof)
    {
        // 如果当前数据为于原数据的最后一组，计算滑动窗口右端终点，或者总数据量
        // ps:后面可能还有前面组数据到来（因为是乱序的，所以此时不能end_input()）
        eof_index = index + data.size();
    }
    // 当写进output的总数据量=所有的数据量，此时才结束
    if (_output.bytes_written() == eof_index)
    {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return unassembled_bytes() == 0; }